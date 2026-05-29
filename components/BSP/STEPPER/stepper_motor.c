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
#include "esp_timer.h"
#include "uart.h"
#include <stdio.h>

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
} motor_task_ctx_t;

static motor_task_ctx_t s_mtctx[MOTOR_TASK_COUNT];
static SemaphoreHandle_t s_uart_mutex = NULL;
static EventGroupHandle_t s_motor_done_evt = NULL;

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
 * @brief       快速读取电机状态(短超时，供 per-motor 任务用)
 * @param       motor_id: 电机ID
 * @param       status: 状态存储指针
 * @retval      ESP_OK成功，其他失败
 * @note        调用者必须持有 s_uart_mutex
 */
static esp_err_t motor_read_status_fast(uint8_t motor_id, uint8_t *status)
{
    s_rx_head = s_rx_tail = 0;

    uint8_t data[1] = {CMD_READ_STATUS};
    if (motor_send_command(motor_id, data, 1) != ESP_OK) return ESP_FAIL;

    for (int i = 0; i < 8; i++) {
        if (uart_rx_available() > 0) {
            *status = uart_rx_getc();
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_FAIL;
}

/**
 * @brief       Per-motor 任务函数
 * @note        从命令队列取命令→发送位置命令→轮询状态→标记完成
 *              每个电机完全独立运行
 */
static void motor_task_fn(void *arg)
{
    uint8_t motor_id = (uint8_t)(uintptr_t)arg;
    motor_task_ctx_t *ctx = &s_mtctx[motor_id - 1];
    motor_move_cmd_t cmd;
    EventBits_t my_bit = motor_to_bit(motor_id);

    while (1) {
        if (xQueueReceive(ctx->cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) continue;
        if (cmd.pulses == 0) {
            xEventGroupSetBits(s_motor_done_evt, my_bit);
            continue;
        }

        ctx->moving = true;

        xSemaphoreTake(s_uart_mutex, portMAX_DELAY);
        motor_position_mode(motor_id, cmd.dir, cmd.speed_rpm,
                           cmd.accel, cmd.pulses, cmd.mode);
        xSemaphoreGive(s_uart_mutex);

        uint32_t timeout_ms = 5000;
        if (cmd.speed_rpm > 0 && cmd.pulses != 0) {
            timeout_ms = (uint32_t)((float)abs(cmd.pulses) * 60000.0f
                                    / (200.0f * (float)cmd.speed_rpm)) + 500;
        }
        if (timeout_ms < 1000) timeout_ms = 1000;
        if (timeout_ms > 15000) timeout_ms = 15000;

        TickType_t start_tick = xTaskGetTickCount();
        bool reached = false;

        vTaskDelay(pdMS_TO_TICKS(50));

        while (!reached) {
            uint8_t status = 0;
            if (xSemaphoreTake(s_uart_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                motor_read_status_fast(motor_id, &status);
                xSemaphoreGive(s_uart_mutex);
            }

            if (status & 0x02) {
                reached = true;
                printf("[Motor %d] reached! status=0x%02X\n", motor_id, status);
                break;
            }

            if ((xTaskGetTickCount() - start_tick) >= pdMS_TO_TICKS(timeout_ms)) {
                printf("[Motor %d] timeout (%dms)\n", motor_id, timeout_ms);
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(80));
        }

        ctx->moving = false;
        xEventGroupSetBits(s_motor_done_evt, my_bit);
    }
}

/**
 * @brief       UART事件处理任务
 * @param       pvParameters: 任务参数
 * @retval      无
 * @note        处理UART接收事件，将数据存入接收缓冲区
 */
static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t *dtmp = (uint8_t *)malloc(256);

    for (;;) {
        if (xQueueReceive(s_uart_queue, (void *)&event, portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:                                    /**< 接收到数据 */
                    uart_read_bytes(s_uart_num, dtmp, event.size, portMAX_DELAY);
                    for (int i = 0; i < event.size; i++) {
                        int next_head = (s_rx_head + 1) % 256;
                        if (next_head != s_rx_tail) {
                            s_rx_buffer[s_rx_head] = dtmp[i];
                            s_rx_head = next_head;
                        }
                    }
                    if (s_rx_sem && event.size > 0) {
                        BaseType_t higher_priority_woken = pdFALSE;
                        xSemaphoreGiveFromISR(s_rx_sem, &higher_priority_woken);
                        if (higher_priority_woken == pdTRUE) {
                            portYIELD_FROM_ISR();
                        }
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

    printf("[SEND] UART%d motor=%d, bytes=%d:", s_uart_num, motor_id, tx_len);
    for (int i = 0; i < tx_len; i++) printf(" %02X", tx_buf[i]);
    printf("\n");

    /** 发送数据 */
    int ret = uart_write_bytes(s_uart_num, (const char *)tx_buf, tx_len);

    if (ret != tx_len) {
        return ESP_FAIL;
    }

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
    uint8_t data[2] = {CMD_SET_ZERO, save ? 0x01 : 0x00};
    return motor_send_command(motor_id, data, 2);
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
    uint8_t data[1] = {CMD_CLEAR_POS};
    return motor_send_command(motor_id, data, 1);
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

    /** 等待响应数据 */
    int timeout = 0;
    while (uart_rx_available() < 7 && timeout < 50) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout++;
    }

    /** 解析响应数据 */
    int available = uart_rx_available();
    if (available >= 7) {
        uint8_t rx_buf[64];
        int len = (available > 64) ? 64 : available;
        for (int i = 0; i < len; i++) {
            rx_buf[i] = uart_rx_getc();
        }

        for (int i = 0; i < len - 6; i++) {
            if (rx_buf[i] == motor_id && rx_buf[i+1] == CMD_READ_POS) {
                *pos = (int32_t)((uint32_t)rx_buf[i+2] << 24) |
                       ((uint32_t)rx_buf[i+3] << 16) |
                       ((uint32_t)rx_buf[i+4] << 8) |
                       rx_buf[i+5];
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

    /** 等待响应数据 */
    int timeout = 0;
    while (uart_rx_available() < 5 && timeout < 50) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout++;
    }

    /** 解析响应数据 */
    int available = uart_rx_available();
    if (available >= 5) {
        uint8_t rx_buf[64];
        int len = (available > 64) ? 64 : available;
        for (int i = 0; i < len; i++) {
            rx_buf[i] = uart_rx_getc();
        }

        for (int i = 0; i < len - 4; i++) {
            if (rx_buf[i] == motor_id && rx_buf[i+1] == CMD_READ_SPEED) {
                *speed = (int16_t)((uint16_t)rx_buf[i+2] << 8) | rx_buf[i+3];
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
 * @brief       等待电机到位
 * @param       motor_id: 电机ID
 * @retval      无
 * @note        先尝试状态轮询，失败后用固定延时兜底
 */
void motor_wait_reached(uint8_t motor_id)
{
    uint8_t status;
    int timeout = 0;

    uart0_printf("[Wait] Waiting for motor %d to start moving...\n", motor_id);
    stepper_delay_ms(100);

    do {
        if (motor_read_status(motor_id, &status) == ESP_OK) {
            if (status & 0x02) {
                uart0_printf("[Wait] Motor %d reached after %dms\n", motor_id, timeout * 10 + 100);
                break;
            }
        }
        stepper_delay_ms(10);
        timeout++;
    } while (timeout < 100);

    if (timeout >= 100) {
        uart0_printf("[Wait] Motor %d status not confirmed, using time fallback\n", motor_id);
    }
}

/**
 * @brief       等待电机到位(带脉冲时间估算)
 * @param       motor_id: 电机ID
 * @param       pulses: 脉冲数
 * @param       speed_rpm: 速度(RPM)
 * @retval      无
 * @note        用脉冲数/速度估算时间作为最大等待时间，状态轮询提前退出
 */
void motor_wait_reached_ex(uint8_t motor_id, int32_t pulses, uint16_t speed_rpm)
{
    if (pulses == 0) return;

    uint32_t timeout_ms = 5000;
    if (speed_rpm > 0) {
        timeout_ms = (uint32_t)((float)abs(pulses) * 60000.0f / (200.0f * (float)speed_rpm));
        timeout_ms += 500;
    }
    if (timeout_ms < 1000) timeout_ms = 1000;
    if (timeout_ms > 15000) timeout_ms = 15000;

    uart0_printf("[Wait] Motor %d: %d pulses @ %d RPM, max wait %d ms\n",
                 motor_id, pulses, speed_rpm, timeout_ms);

    int64_t start = esp_timer_get_time() / 1000;

    while (1) {
        if (uart_rx_available() >= 4) {
            int saved_head = s_rx_head;
            int count = 0;
            uint8_t tmp[256];
            while (s_rx_tail != saved_head && count < 256) {
                tmp[count++] = s_rx_buffer[s_rx_tail];
                s_rx_tail = (s_rx_tail + 1) % 256;
            }

            for (int i = 0; i < count - 3; i++) {
                if (tmp[i] == motor_id && tmp[i+1] == CMD_READ_STATUS) {
                    uint8_t st = tmp[i+2];
                    if (st & 0x02) {
                        uart0_printf("[Wait] Motor %d reached! status=0x%02X\n", motor_id, st);
                        return;
                    }
                }
            }
        }

        int64_t elapsed = esp_timer_get_time() / 1000 - start;
        if (elapsed >= (int64_t)timeout_ms) {
            uart0_printf("[Wait] Motor %d timeout after %lld ms\n", motor_id, elapsed);
            return;
        }

        if (s_rx_sem) {
            BaseType_t woke = pdFALSE;
            uint32_t rem = timeout_ms - elapsed;
            if (rem > 100) rem = 100;
            if (xSemaphoreTake(s_rx_sem, pdMS_TO_TICKS(rem)) != pdTRUE) {
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void motor_wait_reached_xy(int32_t x_pulses, uint16_t x_rpm, int32_t y_pulses, uint16_t y_rpm)
{
    bool x_done = (x_pulses == 0);
    bool y_done = (y_pulses == 0);

    uint32_t timeout_ms = 5000;
    uint32_t x_time = 0, y_time = 0;
    if (x_pulses != 0 && x_rpm > 0) {
        x_time = (uint32_t)((float)abs(x_pulses) * 60000.0f / (200.0f * (float)x_rpm));
        x_time += 300;
    }
    if (y_pulses != 0 && y_rpm > 0) {
        y_time = (uint32_t)((float)abs(y_pulses) * 60000.0f / (200.0f * (float)y_rpm));
        y_time += 300;
    }
    uint32_t max_time = (x_time > y_time) ? x_time : y_time;
    if (max_time < 1000) max_time = 1000;
    if (max_time > 15000) max_time = 15000;

    s_rx_head = s_rx_tail = 0;

    uart0_printf("[Wait] XY: X=%dpls@%dRPM, Y=%dpls@%dRPM, max_wait=%ums\n",
                 x_pulses, x_rpm, y_pulses, y_rpm, max_time);

    int64_t start = esp_timer_get_time() / 1000;

    while (!x_done || !y_done) {
        int available = uart_rx_available();
        if (available >= 4) {
            int saved_head = s_rx_head;
            int count = 0;
            uint8_t tmp[256];
            while (s_rx_tail != saved_head && count < 256) {
                tmp[count++] = s_rx_buffer[s_rx_tail];
                s_rx_tail = (s_rx_tail + 1) % 256;
            }

            for (int i = 0; i < count - 3; i++) {
                if (!x_done && tmp[i] == MOTOR_ID_X && tmp[i+1] == CMD_READ_STATUS) {
                    uint8_t st = tmp[i+2];
                    if (st & 0x02) {
                        x_done = true;
                        uart0_printf("[Wait] X reached! status=0x%02X\n", st);
                    }
                }
                if (!y_done && tmp[i] == MOTOR_ID_Y && tmp[i+1] == CMD_READ_STATUS) {
                    uint8_t st = tmp[i+2];
                    if (st & 0x02) {
                        y_done = true;
                        uart0_printf("[Wait] Y reached! status=0x%02X\n", st);
                    }
                }
            }
        }

        int64_t elapsed = esp_timer_get_time() / 1000 - start;
        if (elapsed >= (int64_t)max_time) {
            if (!x_done) uart0_printf("[Wait] X timeout after %lld ms\n", elapsed);
            if (!y_done) uart0_printf("[Wait] Y timeout after %lld ms\n", elapsed);
            return;
        }

        if (s_rx_sem) {
            uint32_t rem = max_time - elapsed;
            if (rem > 100) rem = 100;
            xSemaphoreTake(s_rx_sem, pdMS_TO_TICKS(rem));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    uart0_printf("[Wait] XY both done!\n");
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
