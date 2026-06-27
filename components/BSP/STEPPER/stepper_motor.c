/**
 * @file        stepper_motor.c
 * @brief       步进电机驱动源文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @note        实现步进电机的初始化、运动控制、状态读取等功能
 *              通过UART与电机驱动器通信，支持多轴联动
 */

#include "stepper_motor.h"
#include "string.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "uart.h"
#include "log.h"
#include <stdio.h>

/** 调试打印开关: 改为1可打印[TX]/[RX]/[Motor]等收发与状态日志(用于排查),
 *  改为0(默认)关闭。日志走异步日志任务, 不再阻塞总线时序 */
#define STEPPER_DEBUG 1

#if STEPPER_DEBUG
#define SDBG(...) log_print(__VA_ARGS__)
#else
#define SDBG(...) ((void)0)
#endif

/** 指令间最小间隔(ms): 预留从机ACK应答(~0.35ms)+处理+半双工总线回转时间,
 *  防止下一条指令与本条应答在共线上冲突丢包(电机不动)。
 *  取值权衡:  太小→可能丢指令(电机不动);  太大→分段之间停顿明显(写字卡)。
 *  5ms 已足够覆盖应答(~2.5ms)又较流畅; 若出现电机不动可适当加大(6~8)。 */
#define CMD_GAP_MS 5

/** UART端口号 */
static uart_port_t s_uart_num;

/** UART事件队列句柄 */
static QueueHandle_t s_uart_queue;

/** RX信号量，数据到达时发出 */
static SemaphoreHandle_t s_rx_sem;

/** 接收数据缓冲区 */
static uint8_t s_rx_buffer[256];

/** 接收缓冲区读写索引 */
static int s_rx_head = 0;
static int s_rx_tail = 0;

/* ==================== 多电机并行控制 ==================== */

#define MOTOR_TASK_COUNT 4

typedef struct {
    uint8_t motor_id;
    TaskHandle_t task_handle;
    QueueHandle_t cmd_queue;
    volatile bool moving;
    volatile int ack_skip;       /**< Both模式下需要跳过的ACK响应数 */
} motor_task_ctx_t;

static motor_task_ctx_t s_mtctx[MOTOR_TASK_COUNT];
static SemaphoreHandle_t s_uart_mutex = NULL;
static EventGroupHandle_t s_motor_done_evt = NULL;

/** 电机到位事件组（由UART接收中断驱动，驱动器自动返回到位响应） */
static EventGroupHandle_t s_motor_reached_evt = NULL;

/** UART帧解析缓冲区 */
static uint8_t s_frame_buf[16];
static int s_frame_pos = 0;

static EventBits_t motor_to_bit(uint8_t id)
{
    switch (id) {
        case MOTOR_ID_X: return MOTOR_MASK_X;
        case MOTOR_ID_Y: return MOTOR_MASK_Y;
        case MOTOR_ID_Z: return MOTOR_MASK_Z;
        case MOTOR_ID_A: return MOTOR_MASK_A;
        default: return 0;
    }
}

static int uart_rx_available(void)
{
    return (s_rx_head - s_rx_tail + 256) % 256;
}

static uint8_t uart_rx_getc(void)
{
    uint8_t c = s_rx_buffer[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1) % 256;
    return c;
}

/**
 * @brief       主动查询电机状态（持有uart_mutex时调用）
 * @param       motor_id: 电机ID
 * @retval      true=到位, false=未到位或查询失败
 */
/**
 * @brief       主动查询电机到位（需等待到位位从0→1跳变，或连续确认）
 * @note        中断等待已耗尽运动时间后才进入此函数，
 *              因此连续3次确认到位=1也视为有效（电机已在中断等待期间完成）
 */
static bool motor_poll_reached(uint8_t motor_id)
{
    bool saw_moving = false;
    int consecutive_reached = 0;

    for (int round = 0; round < 120; round++) {
        s_rx_head = s_rx_tail = 0;

        uint8_t data[1] = {CMD_READ_STATUS};
        if (motor_send_command(motor_id, data, 1) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        for (int i = 0; i < 10; i++) {
            if (uart_rx_available() >= 4) {
                uint8_t rx_buf[16];
                int len = 0;
                while (uart_rx_available() > 0 && len < 16) {
                    rx_buf[len++] = uart_rx_getc();
                }
                for (int j = 0; j < len - 3; j++) {
                    if (rx_buf[j] == motor_id && rx_buf[j+1] == CMD_READ_STATUS) {
                        uint8_t st = rx_buf[j+2];
                        bool reached = (st & 0x02) != 0;
                        if (!reached) {
                            if (!saw_moving) {
                                SDBG("[Poll] Motor %d moving (st=0x%02X)\n",
                                       motor_id, st);
                            }
                            saw_moving = true;
                            consecutive_reached = 0;
                        } else if (saw_moving) {
                            /** 到位位 0→1 跳变，确认到位 */
                            SDBG("[Poll] Motor %d reached (st=0x%02X)\n",
                                   motor_id, st);
                            return true;
                        } else {
                            /** 一直到位=1，可能是中断等待期间已完成运动 */
                            consecutive_reached++;
                            if (consecutive_reached >= 3) {
                                SDBG("[Poll] Motor %d reached (st=0x%02X, round=%d)\n",
                                       motor_id, st, round);
                                return true;
                            }
                        }
                        goto next_round;
                    }
                }
                goto next_round;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    next_round:
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}

/**
 * @brief       Per-motor 任务函数
 * @note        从命令队列取命令→发送位置命令→先尝试中断驱动等待→
 *              超时后回退为主动查询（解决多机共线总线碰撞问题）
 */
static void motor_task_fn(void *arg)
{
    uint8_t motor_id = (uint8_t)(uintptr_t)arg;
    motor_task_ctx_t *ctx = &s_mtctx[motor_id - 1];
    motor_move_cmd_t cmd;
    EventBits_t my_bit = motor_to_bit(motor_id);
    EventBits_t my_reached_bit = motor_to_bit(motor_id);

    while (1) {
        if (xQueueReceive(ctx->cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) continue;
        if (cmd.pulses == 0) {
            xEventGroupSetBits(s_motor_done_evt, my_bit);
            continue;
        }

        /** 计算超时时间 */
        uint32_t timeout_ms = 5000;
        if (cmd.speed_rpm > 0 && cmd.pulses != 0) {
            timeout_ms = (uint32_t)((float)abs(cmd.pulses) * 60000.0f
                                    / (200.0f * (float)cmd.speed_rpm)) + 500;
        }
        if (timeout_ms < 1000) timeout_ms = 1000;
        if (timeout_ms > 15000) timeout_ms = 15000;

        /** 清除到位事件位 */
        xEventGroupClearBits(s_motor_reached_evt, my_reached_bit);
        ctx->ack_skip = 1;
        ctx->moving = true;

        /** 发送位置命令 */
        xSemaphoreTake(s_uart_mutex, portMAX_DELAY);
        motor_position_mode(motor_id, cmd.dir, cmd.speed_rpm,
                           cmd.accel, cmd.pulses, cmd.mode);
        xSemaphoreGive(s_uart_mutex);

        /** 阶段1：等待中断驱动的到位事件（适用于单轴无碰撞场景） */
        EventBits_t bits = xEventGroupWaitBits(
            s_motor_reached_evt, my_reached_bit,
            pdTRUE, pdTRUE,
            pdMS_TO_TICKS(timeout_ms)
        );

        if (bits & my_reached_bit) {
            SDBG("[Motor %d] reached! (interrupt)\n", motor_id);
        } else {
            /** 阶段2：中断等待超时，回退为主动查询（解决多机总线碰撞） */
            SDBG("[Motor %d] interrupt timeout, polling...\n", motor_id);
            TickType_t poll_start = xTaskGetTickCount();
            bool reached = false;

            while ((xTaskGetTickCount() - poll_start) < pdMS_TO_TICKS(timeout_ms)) {
                if (xSemaphoreTake(s_uart_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                    reached = motor_poll_reached(motor_id);
                    xSemaphoreGive(s_uart_mutex);
                }
                if (reached) {
                    SDBG("[Motor %d] reached! (poll fallback)\n", motor_id);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            if (!reached) {
                SDBG("[Motor %d] timeout (%lums)\n", motor_id, (unsigned long)timeout_ms);
            }
        }

        ctx->moving = false;
        xEventGroupSetBits(s_motor_done_evt, my_bit);
    }
}

/**
 * @brief       解析UART接收帧，检测电机到位响应
 * @param       byte: 新接收的字节
 * @note        驱动器到位响应格式: [ID] [CMD] [0x02] [0x6B]
 *              需要驱动器Response模式设为Reached或Both
 */
static void uart_frame_parse(uint8_t byte)
{
    s_frame_buf[s_frame_pos++] = byte;

    /** 检测完整4字节响应帧: [ID][CMD][STATUS][0x6B] */
    if (s_frame_pos >= 4 && byte == CHECKSUM_BYTE) {
        int start = s_frame_pos - 4;
        uint8_t id  = s_frame_buf[start];
        uint8_t cmd = s_frame_buf[start + 1];
        uint8_t st  = s_frame_buf[start + 2];
        (void)st;   /**< 仅调试打印用, 关闭调试时避免未使用告警 */

        /** 验证帧有效性：合法电机ID + 位置/速度命令响应 */
        if (id >= 1 && id <= MOTOR_MAX_ID &&
            (cmd == CMD_POSITION || cmd == CMD_VELOCITY))
        {
            /** 该电机正在运动中，检查是否需要跳过ACK帧（Both模式） */
            if (s_mtctx[id - 1].moving && s_motor_reached_evt) {
                if (s_mtctx[id - 1].ack_skip > 0) {
                    s_mtctx[id - 1].ack_skip--;
                    SDBG("[UART] Motor %d ACK skipped (st=0x%02X)\n", id, st);
                } else {
                    EventBits_t bit = motor_to_bit(id);
                    xEventGroupSetBits(s_motor_reached_evt, bit);
                    SDBG("[UART] Motor %d REACHED event set (st=0x%02X)\n", id, st);
                }
            }
        }
        s_frame_pos = 0;
        return;
    }

    /** 防止缓冲区溢出 */
    if (s_frame_pos >= 16) {
        s_frame_pos = 0;
    }
}

static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t *dtmp = (uint8_t *)malloc(256);

    for (;;) {
        if (xQueueReceive(s_uart_queue, (void *)&event, portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:                                    /**< 接收到数据 */
                    uart_read_bytes(s_uart_num, dtmp, event.size, portMAX_DELAY);
                    /** 打印接收帧（调试用，异步日志） */
#if STEPPER_DEBUG
                    {
                        char buf[80];
                        int p = snprintf(buf, sizeof(buf), "[RX] %d bytes:", event.size);
                        for (int i = 0; i < (int)event.size && p < (int)sizeof(buf) - 4; i++)
                            p += snprintf(buf + p, sizeof(buf) - p, " %02X", dtmp[i]);
                        log_print("%s\n", buf);
                    }
#endif
                    for (int i = 0; i < event.size; i++) {
                        /** 存入环形缓冲区（供主动读取状态/位置等使用） */
                        int next_head = (s_rx_head + 1) % 256;
                        if (next_head != s_rx_tail) {
                            s_rx_buffer[s_rx_head] = dtmp[i];
                            s_rx_head = next_head;
                        }
                        /** 解析帧，检测到位响应并触发事件 */
                        uart_frame_parse(dtmp[i]);
                    }
                    if (s_rx_sem && event.size > 0) {
                        xSemaphoreGive(s_rx_sem);
                    }
                    break;
                case UART_FIFO_OVF:                               /**< FIFO溢出 */
                    uart_flush_input(s_uart_num);
                    xQueueReset(s_uart_queue);
                    break;
                case UART_BUFFER_FULL:                            /**< 缓冲区满 */
                    uart_flush_input(s_uart_num);
                    xQueueReset(s_uart_queue);
                    break;
                default:
                    break;
            }
        }
    }

    free(dtmp);
    vTaskDelete(NULL);
}

/**
 * @brief       初始化步进电机驱动
 * @param       uart_num: UART端口号
 * @param       tx_pin: TX引脚
 * @param       rx_pin: RX引脚
 * @retval      无
 * @note        配置UART参数并创建事件处理任务
 */
void stepper_motor_init(uart_port_t uart_num, int tx_pin, int rx_pin)
{
    s_uart_num = uart_num;

    /** 配置UART参数 */
    uart_config_t uart_config = {
        .baud_rate = 115200,                                     /**< 波特率115200 */
        .data_bits = UART_DATA_8_BITS,                         /**< 8位数据位 */
        .parity = UART_PARITY_DISABLE,                         /**< 无校验 */
        .stop_bits = UART_STOP_BITS_1,                         /**< 1位停止位 */
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,                  /**< 无流控 */
        .source_clk = UART_SCLK_DEFAULT,
    };

    /** 应用UART配置 */
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(uart_num, 256, 256, 10, &s_uart_queue, 0));

    /** 创建RX信号量（中断驱动） */
    s_rx_sem = xSemaphoreCreateBinary();
    configASSERT(s_rx_sem);

    /** 创建电机到位事件组（UART接收中断驱动） */
    if (s_motor_reached_evt == NULL) {
        s_motor_reached_evt = xEventGroupCreate();
        configASSERT(s_motor_reached_evt);
    }

    /** 创建UART TX互斥锁 */
    if (s_uart_mutex == NULL) {
        s_uart_mutex = xSemaphoreCreateMutex();
        configASSERT(s_uart_mutex);
    }

    /** 创建UART事件处理任务 */
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, NULL);
}

/**
 * @brief       发送电机控制命令
 * @param       motor_id: 电机ID
 * @param       data: 命令数据
 * @param       len: 数据长度
 * @retval      ESP_OK成功，其他失败
 * @note        组装命令帧并发送到电机驱动器
 */
esp_err_t motor_send_command(uint8_t motor_id, uint8_t *data, uint16_t len)
{
    uint8_t tx_buf[64];
    uint16_t tx_len = 0;

    /** 添加电机ID */
    tx_buf[tx_len++] = motor_id;

    /** 添加命令数据 */
    for (uint16_t i = 0; i < len; i++) {
        tx_buf[tx_len++] = data[i];
    }

    /** 添加校验字节 */
    tx_buf[tx_len++] = CHECKSUM_BYTE;

    /** 打印发送帧（调试用，异步日志，不阻塞总线时序） */
#if STEPPER_DEBUG
    {
        char buf[80];
        int p = snprintf(buf, sizeof(buf), "[TX] motor=%d:", motor_id);
        for (int i = 0; i < tx_len && p < (int)sizeof(buf) - 4; i++)
            p += snprintf(buf + p, sizeof(buf) - p, " %02X", tx_buf[i]);
        log_print("%s\n", buf);
    }
#endif

    /** 发送数据 */
    int ret = uart_write_bytes(s_uart_num, (const char *)tx_buf, tx_len);

    if (ret != tx_len) {
        return ESP_FAIL;
    }

    /** 等待TX完成，确保数据全部发出 */
    uart_wait_tx_done(s_uart_num, pdMS_TO_TICKS(100));

    /** 指令间最小间隔: Emm42协议要求连续指令间隔>10ms, 且需等待从机
     *  应答与半双工总线回转完成, 否则下一条指令会与本条的应答在共线
     *  上冲突导致丢包(电机不动)。这里固定保证间隔, 不再依赖调试打印的延时。 */
    vTaskDelay(pdMS_TO_TICKS(CMD_GAP_MS));

    return ESP_OK;
}

/**
 * @brief       电机使能/失能
 * @param       motor_id: 电机ID
 * @param       enable: true-使能，false-失能
 * @retval      ESP_OK成功，其他失败
 */
esp_err_t motor_enable(uint8_t motor_id, bool enable)
{
    uint8_t data[4] = {CMD_ENABLE, 0xAB, enable ? 0x01 : 0x00, 0x00};
    return motor_send_command(motor_id, data, 4);
}

/**
 * @brief       电机停止
 * @param       motor_id: 电机ID
 * @retval      ESP_OK成功，其他失败
 */
esp_err_t motor_stop(uint8_t motor_id)
{
    uint8_t data[3] = {CMD_STOP, 0x98, 0x00};
    return motor_send_command(motor_id, data, 3);
}

/**
 * @brief       速度模式控制
 * @param       motor_id: 电机ID
 * @param       dir: 转动方向
 * @param       speed_rpm: 速度(RPM)
 * @param       accel: 加速度
 * @retval      ESP_OK成功，其他失败
 */
esp_err_t motor_velocity_mode(uint8_t motor_id, motor_direction_t dir, uint16_t speed_rpm, uint8_t accel)
{
    uint8_t data[6] = {
        CMD_VELOCITY,
        (uint8_t)dir,
        (uint8_t)(speed_rpm >> 8),
        (uint8_t)(speed_rpm & 0xFF),
        accel,
        0x00
    };
    return motor_send_command(motor_id, data, 6);
}

/**
 * @brief       位置模式控制
 * @param       motor_id: 电机ID
 * @param       dir: 转动方向
 * @param       speed_rpm: 速度(RPM)
 * @param       accel: 加速度
 * @param       pulses: 脉冲数
 * @param       mode: 位置模式
 * @retval      ESP_OK成功，其他失败
 */
esp_err_t motor_position_mode(uint8_t motor_id, motor_direction_t dir, uint16_t speed_rpm, uint8_t accel, int32_t pulses, position_mode_t mode)
{
    uint8_t data[11] = {
        CMD_POSITION,
        (uint8_t)dir,
        (uint8_t)(speed_rpm >> 8),
        (uint8_t)(speed_rpm & 0xFF),
        accel,
        (uint8_t)((pulses >> 24) & 0xFF),
        (uint8_t)((pulses >> 16) & 0xFF),
        (uint8_t)((pulses >> 8) & 0xFF),
        (uint8_t)(pulses & 0xFF),
        (uint8_t)mode,
        0x00
    };
    return motor_send_command(motor_id, data, 11);
}

/**
 * @brief       设置零点位置
 * @param       motor_id: 电机ID
 * @param       save: 是否保存到Flash
 * @retval      ESP_OK成功，其他失败
 */
esp_err_t motor_set_zero(uint8_t motor_id, bool save)
{
    uint8_t data[3] = {CMD_SET_ZERO, 0x88, save ? 0x01 : 0x00};
    return motor_send_command(motor_id, data, 3);
}

/**
 * @brief       回零操作
 * @param       motor_id: 电机ID
 * @param       mode: 回零模式
 * @retval      ESP_OK成功，其他失败
 */
esp_err_t motor_homing(uint8_t motor_id, homing_mode_t mode)
{
    uint8_t data[3] = {CMD_HOMING, (uint8_t)mode, 0x00};
    return motor_send_command(motor_id, data, 3);
}

/**
 * @brief       清除位置
 * @param       motor_id: 电机ID
 * @retval      ESP_OK成功，其他失败
 */
esp_err_t motor_clear_position(uint8_t motor_id)
{
    uint8_t data[2] = {CMD_CLEAR_POS, 0x6D};
    return motor_send_command(motor_id, data, 2);
}

/**
 * @brief       读取电机状态
 * @param       motor_id: 电机ID
 * @param       status: 状态存储指针
 * @retval      ESP_OK成功，其他失败
 */
esp_err_t motor_read_status(uint8_t motor_id, uint8_t *status)
{
    uint8_t data[1] = {CMD_READ_STATUS};

    s_rx_head = s_rx_tail = 0;

    if (motor_send_command(motor_id, data, 1) != ESP_OK) {
        return ESP_FAIL;
    }

    int timeout = 0;
    while (uart_rx_available() < 4 && timeout < 30) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout++;
    }

    int available = uart_rx_available();
    if (available > 0) {
        uint8_t rx_buf[64];
        int len = (available > 64) ? 64 : available;
        for (int i = 0; i < len; i++) {
            rx_buf[i] = uart_rx_getc();
        }

        for (int i = 0; i < len - 3; i++) {
            if (rx_buf[i] == motor_id && rx_buf[i+1] == CMD_READ_STATUS) {
                *status = rx_buf[i+2];
                return ESP_OK;
            }
        }
    }

    return ESP_FAIL;
}

/**
 * @brief       读取电机位置
 * @param       motor_id: 电机ID
 * @param       pos: 位置存储指针
 * @retval      ESP_OK成功，其他失败
 */
esp_err_t motor_read_position(uint8_t motor_id, int32_t *pos)
{
    uint8_t data[1] = {CMD_READ_POS};

    s_rx_head = s_rx_tail = 0;

    if (motor_send_command(motor_id, data, 1) != ESP_OK) {
        return ESP_FAIL;
    }

    /** 等待响应: [ID][0x36][sign][pos3][pos2][pos1][pos0][0x6B] = 8字节 */
    int timeout = 0;
    while (uart_rx_available() < 8 && timeout < 50) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout++;
    }

    /** 解析响应数据 */
    int available = uart_rx_available();
    if (available >= 8) {
        uint8_t rx_buf[64];
        int len = (available > 64) ? 64 : available;
        for (int i = 0; i < len; i++) {
            rx_buf[i] = uart_rx_getc();
        }

        /** 格式: [ID][0x36][sign][pos3][pos2][pos1][pos0][0x6B] */
        for (int i = 0; i < len - 7; i++) {
            if (rx_buf[i] == motor_id && rx_buf[i+1] == CMD_READ_POS) {
                uint32_t abs_pos = ((uint32_t)rx_buf[i+3] << 24) |
                                   ((uint32_t)rx_buf[i+4] << 16) |
                                   ((uint32_t)rx_buf[i+5] << 8) |
                                   rx_buf[i+6];
                *pos = (rx_buf[i+2] == 0x01) ? -(int32_t)abs_pos : (int32_t)abs_pos;
                SDBG("[RD_POS] motor=%d sign=0x%02X abs=%lu pos=%ld\n",
                       motor_id, rx_buf[i+2], (unsigned long)abs_pos, (long)*pos);
                return ESP_OK;
            }
        }
    }

    return ESP_FAIL;
}

/**
 * @brief       读取电机速度
 * @param       motor_id: 电机ID
 * @param       speed: 速度存储指针
 * @retval      ESP_OK成功，其他失败
 */
esp_err_t motor_read_speed(uint8_t motor_id, int16_t *speed)
{
    uint8_t data[1] = {CMD_READ_SPEED};

    s_rx_head = s_rx_tail = 0;

    if (motor_send_command(motor_id, data, 1) != ESP_OK) {
        return ESP_FAIL;
    }

    /** 等待响应: [ID][0x35][sign][spd_H][spd_L][0x6B] = 6字节 */
    int timeout = 0;
    while (uart_rx_available() < 6 && timeout < 50) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout++;
    }

    /** 解析响应数据 */
    int available = uart_rx_available();
    if (available >= 6) {
        uint8_t rx_buf[64];
        int len = (available > 64) ? 64 : available;
        for (int i = 0; i < len; i++) {
            rx_buf[i] = uart_rx_getc();
        }

        /** 格式: [ID][0x35][sign][spd_H][spd_L][0x6B] */
        for (int i = 0; i < len - 5; i++) {
            if (rx_buf[i] == motor_id && rx_buf[i+1] == CMD_READ_SPEED) {
                uint16_t abs_spd = ((uint16_t)rx_buf[i+3] << 8) | rx_buf[i+4];
                *speed = (rx_buf[i+2] == 0x01) ? -(int16_t)abs_spd : (int16_t)abs_spd;
                return ESP_OK;
            }
        }
    }

    return ESP_FAIL;
}

/**
 * @brief       同步触发多轴运动
 * @retval      ESP_OK成功，其他失败
 * @note        同时触发多个电机的运动
 */
esp_err_t motor_sync_trigger(void)
{
    uint8_t data[2] = {CMD_SYNC, 0x66};
    return motor_send_command(0x00, data, 2);
}

/**
 * @brief       延时函数
 * @param       ms: 延时毫秒数
 * @retval      无
 */
void stepper_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/**
 * @brief       等待电机到位（中断驱动）
 * @param       motor_id: 电机ID
 * @retval      无
 * @note        等待UART接收中断驱动的到位事件，固定超时5秒
 */
void motor_wait_reached(uint8_t motor_id)
{
    EventBits_t bit = motor_to_bit(motor_id);
    if (!bit || !s_motor_reached_evt) return;

    xEventGroupClearBits(s_motor_reached_evt, bit);

    EventBits_t result = xEventGroupWaitBits(
        s_motor_reached_evt, bit,
        pdTRUE, pdTRUE,
        pdMS_TO_TICKS(5000)
    );

    if (result & bit) {
        uart0_printf("[Wait] Motor %d reached! (interrupt-driven)\n", motor_id);
    } else {
        uart0_printf("[Wait] Motor %d timeout (5000ms)\n", motor_id);
    }
}

/**
 * @brief       等待电机到位(带脉冲时间估算，中断驱动)
 * @param       motor_id: 电机ID
 * @param       pulses: 脉冲数
 * @param       speed_rpm: 速度(RPM)
 * @retval      无
 * @note        用脉冲数/速度估算超时时间，等待UART接收中断到位事件
 */
void motor_wait_reached_ex(uint8_t motor_id, int32_t pulses, uint16_t speed_rpm)
{
    if (pulses == 0) return;

    EventBits_t bit = motor_to_bit(motor_id);
    if (!bit || !s_motor_reached_evt) return;

    uint32_t timeout_ms = 5000;
    if (speed_rpm > 0) {
        timeout_ms = (uint32_t)((float)abs(pulses) * 60000.0f / (200.0f * (float)speed_rpm));
        timeout_ms += 500;
    }
    if (timeout_ms < 1000) timeout_ms = 1000;
    if (timeout_ms > 15000) timeout_ms = 15000;

    uart0_printf("[Wait] Motor %d: %d pulses @ %d RPM, max wait %lu ms\n",
                 motor_id, pulses, speed_rpm, (unsigned long)timeout_ms);

    xEventGroupClearBits(s_motor_reached_evt, bit);

    EventBits_t result = xEventGroupWaitBits(
        s_motor_reached_evt, bit,
        pdTRUE, pdTRUE,
        pdMS_TO_TICKS(timeout_ms)
    );

    if (result & bit) {
        uart0_printf("[Wait] Motor %d reached! (interrupt-driven)\n", motor_id);
    } else {
        uart0_printf("[Wait] Motor %d timeout after %lu ms\n", motor_id, (unsigned long)timeout_ms);
    }
}

/**
 * @brief       等待X和Y轴同时到位（中断驱动）
 * @param       x_pulses: X轴脉冲数
 * @param       x_rpm: X轴速度
 * @param       y_pulses: Y轴脉冲数
 * @param       y_rpm: Y轴速度
 * @retval      无
 * @note        同时等待X和Y轴的UART接收中断到位事件
 */
void motor_wait_reached_xy(int32_t x_pulses, uint16_t x_rpm, int32_t y_pulses, uint16_t y_rpm)
{
    if (!s_motor_reached_evt) return;

    uint32_t x_time = 0, y_time = 0;
    EventBits_t mask = 0;

    if (x_pulses != 0) {
        mask |= MOTOR_MASK_X;
        if (x_rpm > 0) {
            x_time = (uint32_t)((float)abs(x_pulses) * 60000.0f / (200.0f * (float)x_rpm)) + 300;
        }
    }
    if (y_pulses != 0) {
        mask |= MOTOR_MASK_Y;
        if (y_rpm > 0) {
            y_time = (uint32_t)((float)abs(y_pulses) * 60000.0f / (200.0f * (float)y_rpm)) + 300;
        }
    }

    if (mask == 0) return;

    uint32_t max_time = (x_time > y_time) ? x_time : y_time;
    if (max_time < 1000) max_time = 1000;
    if (max_time > 15000) max_time = 15000;

    uart0_printf("[Wait] XY: X=%dpls@%dRPM, Y=%dpls@%dRPM, max_wait=%lums\n",
                 x_pulses, x_rpm, y_pulses, y_rpm, (unsigned long)max_time);

    xEventGroupClearBits(s_motor_reached_evt, mask);

    EventBits_t result = xEventGroupWaitBits(
        s_motor_reached_evt, mask,
        pdTRUE, pdTRUE,
        pdMS_TO_TICKS(max_time)
    );

    if ((result & mask) == mask) {
        uart0_printf("[Wait] XY both done! (interrupt-driven)\n");
    } else {
        if (!(result & MOTOR_MASK_X) && (mask & MOTOR_MASK_X))
            uart0_printf("[Wait] X timeout after %lu ms\n", (unsigned long)max_time);
        if (!(result & MOTOR_MASK_Y) && (mask & MOTOR_MASK_Y))
            uart0_printf("[Wait] Y timeout after %lu ms\n", (unsigned long)max_time);
    }
}

/* ==================== 多电机并行控制 API ==================== */

esp_err_t motor_tasks_init(void)
{
    s_motor_done_evt = xEventGroupCreate();
    configASSERT(s_motor_done_evt);

    for (int i = 0; i < MOTOR_TASK_COUNT; i++) {
        s_mtctx[i].motor_id = i + 1;
        s_mtctx[i].cmd_queue = xQueueCreate(4, sizeof(motor_move_cmd_t));
        s_mtctx[i].moving = false;
        s_mtctx[i].ack_skip = 0;

        char name[16];
        snprintf(name, sizeof(name), "mt%d", i + 1);
        xTaskCreate(motor_task_fn, name, 3072,
                    (void *)(uintptr_t)(i + 1), 12, &s_mtctx[i].task_handle);

        printf("[Motor] Task mt%d created\n", i + 1);
    }
    return ESP_OK;
}

bool motor_is_moving(uint8_t motor_id)
{
    if (motor_id < 1 || motor_id > MOTOR_TASK_COUNT) return false;
    return s_mtctx[motor_id - 1].moving;
}

esp_err_t motor_move_submit(uint8_t motor_id, motor_direction_t dir,
                            uint16_t speed_rpm, uint8_t accel,
                            int32_t pulses, position_mode_t mode)
{
    if (motor_id < 1 || motor_id > MOTOR_TASK_COUNT) return ESP_FAIL;

    motor_move_cmd_t cmd = {
        .dir = dir, .speed_rpm = speed_rpm, .accel = accel,
        .pulses = pulses, .mode = mode,
    };

    if (xQueueSend(s_mtctx[motor_id - 1].cmd_queue, &cmd, 0) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t motor_wait_done(uint32_t motor_mask, uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(s_motor_done_evt, motor_mask,
                                           pdTRUE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    if ((bits & motor_mask) != motor_mask) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void motor_clear_done(uint32_t motor_mask)
{
    xEventGroupClearBits(s_motor_done_evt, motor_mask);
}
