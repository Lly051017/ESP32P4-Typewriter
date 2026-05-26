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
#include "uart.h"

/** UART端口号 */
static uart_port_t s_uart_num;

/** UART事件队列句柄 */
static QueueHandle_t s_uart_queue;

/** 接收数据缓冲区 */
static uint8_t s_rx_buffer[256];

/** 接收缓冲区读写索引 */
static int s_rx_head = 0;
static int s_rx_tail = 0;

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

    /** 创建UART事件处理任务 */
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, NULL);
}

/**
 * @brief       获取接收缓冲区可用字节数
 * @param       无
 * @retval      可用字节数
 */
static int uart_rx_available(void)
{
    return (s_rx_head - s_rx_tail + 256) % 256;
}

/**
 * @brief       从接收缓冲区读取一个字节
 * @param       无
 * @retval      读取的字节
 */
static uint8_t uart_rx_getc(void)
{
    uint8_t c = s_rx_buffer[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1) % 256;
    return c;
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

    /** 等待响应数据 */
    int timeout = 0;
    while (uart_rx_available() < 4 && timeout < 50) {
        vTaskDelay(pdMS_TO_TICKS(10));
        timeout++;
    }

    /** 解析响应数据 */
    int available = uart_rx_available();
    if (available >= 4) {
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
 * @note        轮询电机状态，等待电机运动到位
 */
void motor_wait_reached(uint8_t motor_id)
{
    uint8_t status;
    int timeout = 0;

    /** 首先等待一小段时间让电机开始移动 */
    uart0_printf("[Wait] Waiting for motor %d to start moving...\n", motor_id);
    stepper_delay_ms(100);

    /** 然后等待电机到位 */
    do {
        if (motor_read_status(motor_id, &status) == ESP_OK) {
            if (status & 0x02) {                                  /**< 到位标志 */
                uart0_printf("[Wait] Motor %d reached after %dms\n", motor_id, timeout * 10 + 100);
                break;
            }
        } else {
            uart0_printf("[Wait] Motor %d status read FAILED (timeout=%d)\n", motor_id, timeout);
        }
        stepper_delay_ms(10);
        timeout++;
    } while (timeout < 500);

    if (timeout >= 500) {
        uart0_printf("[Wait] Motor %d TIMEOUT!\n", motor_id);
    }
}
