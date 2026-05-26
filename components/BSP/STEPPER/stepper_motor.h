/**
 * @file        stepper_motor.h
 * @brief       步进电机驱动头文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @note        提供步进电机的初始化、运动控制、位置读取等功能
 *              支持多轴联动，通过UART与电机驱动器通信
 */

#ifndef __STEPPER_MOTOR_H
#define __STEPPER_MOTOR_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

/* 电机ID定义 */
#define MOTOR_ID_X      1  /**< X轴电机ID */
#define MOTOR_ID_Y      2  /**< Y轴电机ID */
#define MOTOR_ID_Z      3  /**< Z轴电机ID */
#define MOTOR_ID_A      4  /**< A轴电机ID */

#define MOTOR_MAX_ID    4  /**< 最大电机数量 */

#define CHECKSUM_BYTE   0x6B  /**< 校验字节 */

/* 电机控制命令码 */
#define CMD_ENABLE      0xF3  /**< 电机使能命令 */
#define CMD_VELOCITY    0xF6  /**< 速度模式命令 */
#define CMD_POSITION    0xFD  /**< 位置模式命令 */
#define CMD_STOP        0xFE  /**< 停止命令 */
#define CMD_SYNC        0xFF  /**< 同步触发命令 */
#define CMD_SET_ZERO    0x93  /**< 设置零点命令 */
#define CMD_HOMING      0x9A  /**< 回零命令 */
#define CMD_READ_STATUS 0x3A  /**< 读取状态命令 */
#define CMD_READ_POS    0x36  /**< 读取位置命令 */
#define CMD_READ_SPEED  0x35  /**< 读取速度命令 */
#define CMD_CLEAR_POS   0x0A  /**< 清除位置命令 */

/**
 * @brief 电机转动方向枚举
 */
typedef enum {
    DIRECTION_CW = 0x00,   /**< 顺时针方向 */
    DIRECTION_CCW = 0x01    /**< 逆时针方向 */
} motor_direction_t;

/**
 * @brief 位置模式枚举
 */
typedef enum {
    POS_MODE_RELATIVE = 0x00,   /**< 相对位置模式 */
    POS_MODE_ABSOLUTE = 0x01   /**< 绝对位置模式 */
} position_mode_t;

/**
 * @brief 回零模式枚举
 */
typedef enum {
    HOMING_MODE_NEAREST = 0x00,       /**< 最近方向回零 */
    HOMING_MODE_DIRECTION = 0x01,     /**< 指定方向回零 */
    HOMING_MODE_COLLISION = 0x02,     /**< 碰撞检测回零 */
    HOMING_MODE_LIMIT_SWITCH = 0x03   /**< 限位开关回零 */
} homing_mode_t;

/**
 * @brief 电机信息结构体
 */
typedef struct {
    uint8_t id;             /**< 电机ID */
    int32_t current_pos;    /**< 当前脉冲位置 */
    int16_t current_speed;  /**< 当前速度 */
    uint8_t status;         /**< 电机状态 */
    bool enabled;           /**< 使能状态 */
} motor_info_t;

/**
 * @brief 坐标系结构体
 */
typedef struct {
    float x;    /**< X坐标 */
    float y;    /**< Y坐标 */
    float z;    /**< Z坐标 */
} coord_t;

/* 函数声明 */

/**
 * @brief  步进电机初始化
 * @param  uart_num: UART端口号
 * @param  tx_pin: TX引脚
 * @param  rx_pin: RX引脚
 * @retval 无
 */
void stepper_motor_init(uart_port_t uart_num, int tx_pin, int rx_pin);

/**
 * @brief  发送电机命令
 * @param  motor_id: 电机ID
 * @param  data: 命令数据
 * @param  len: 数据长度
 * @retval ESP_OK成功，其他失败
 */
esp_err_t motor_send_command(uint8_t motor_id, uint8_t *data, uint16_t len);

/**
 * @brief  电机使能/失能
 * @param  motor_id: 电机ID
 * @param  enable: true-使能，false-失能
 * @retval ESP_OK成功，其他失败
 */
esp_err_t motor_enable(uint8_t motor_id, bool enable);

/**
 * @brief  电机停止
 * @param  motor_id: 电机ID
 * @retval ESP_OK成功，其他失败
 */
esp_err_t motor_stop(uint8_t motor_id);

/**
 * @brief  速度模式控制
 * @param  motor_id: 电机ID
 * @param  dir: 转动方向
 * @param  speed_rpm: 速度(RPM)
 * @param  accel: 加速度
 * @retval ESP_OK成功，其他失败
 */
esp_err_t motor_velocity_mode(uint8_t motor_id, motor_direction_t dir, uint16_t speed_rpm, uint8_t accel);

/**
 * @brief  位置模式控制
 * @param  motor_id: 电机ID
 * @param  dir: 转动方向
 * @param  speed_rpm: 速度(RPM)
 * @param  accel: 加速度
 * @param  pulses: 脉冲数
 * @param  mode: 位置模式(相对/绝对)
 * @retval ESP_OK成功，其他失败
 */
esp_err_t motor_position_mode(uint8_t motor_id, motor_direction_t dir, uint16_t speed_rpm, uint8_t accel, int32_t pulses, position_mode_t mode);

/**
 * @brief  设置零点位置
 * @param  motor_id: 电机ID
 * @param  save: 是否保存到Flash
 * @retval ESP_OK成功，其他失败
 */
esp_err_t motor_set_zero(uint8_t motor_id, bool save);

/**
 * @brief  回零操作
 * @param  motor_id: 电机ID
 * @param  mode: 回零模式
 * @retval ESP_OK成功，其他失败
 */
esp_err_t motor_homing(uint8_t motor_id, homing_mode_t mode);

/**
 * @brief  清除位置
 * @param  motor_id: 电机ID
 * @retval ESP_OK成功，其他失败
 */
esp_err_t motor_clear_position(uint8_t motor_id);

/**
 * @brief  读取电机状态
 * @param  motor_id: 电机ID
 * @param  status: 状态存储指针
 * @retval ESP_OK成功，其他失败
 */
esp_err_t motor_read_status(uint8_t motor_id, uint8_t *status);

/**
 * @brief  读取电机位置
 * @param  motor_id: 电机ID
 * @param  pos: 位置存储指针
 * @retval ESP_OK成功，其他失败
 */
esp_err_t motor_read_position(uint8_t motor_id, int32_t *pos);

/**
 * @brief  读取电机速度
 * @param  motor_id: 电机ID
 * @param  speed: 速度存储指针
 * @retval ESP_OK成功，其他失败
 */
esp_err_t motor_read_speed(uint8_t motor_id, int16_t *speed);

/**
 * @brief  同步触发多轴运动
 * @retval ESP_OK成功，其他失败
 */
esp_err_t motor_sync_trigger(void);

/**
 * @brief  延时函数
 * @param  ms: 延时毫秒数
 * @retval 无
 */
void stepper_delay_ms(uint32_t ms);

/**
 * @brief  等待电机到位
 * @param  motor_id: 电机ID
 * @retval 无
 */
void motor_wait_reached(uint8_t motor_id);

#endif
