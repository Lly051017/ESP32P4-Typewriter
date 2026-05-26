/**
 * @file        led.h
 * @brief       LED驱动头文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @note        提供LED灯的GPIO控制接口，支持LED0和LED1两个LED灯的控制
 *              LED采用GPIO输出模式，可通过宏定义快速设置LED状态
 */

#ifndef __LED_H
#define __LED_H

#include "driver/gpio.h"

/* GPIO引脚定义 - 定义LED灯连接的GPIO端口号 */
#define LED0_GPIO_PIN    GPIO_NUM_14    /**< LED0连接到GPIO14 */
#define LED1_GPIO_PIN    GPIO_NUM_13    /**< LED1连接到GPIO13 */

/**
 * @brief LED0控制宏
 * @param x: 1-关闭LED0, 0-打开LED0(低电平点亮)
 * @note 使用条件表达式控制LED灯的开关状态
 */
#define LED0(x)          do { x ?                                \
                              gpio_set_level(LED0_GPIO_PIN, 1):  \
                              gpio_set_level(LED0_GPIO_PIN, 0);  \
                            } while(0)

/**
 * @brief LED1控制宏
 * @param x: 1-关闭LED1, 0-打开LED1(低电平点亮)
 * @note 使用条件表达式控制LED灯的开关状态
 */
#define LED1(x)          do { x ?                                \
								gpio_set_level(LED1_GPIO_PIN, 1):  \
								gpio_set_level(LED1_GPIO_PIN, 0);  \
							} while(0)

/**
 * @brief LED0翻转宏
 * @note 读取当前LED状态并取反，实现LED灯的亮灭切换
 */
#define LED0_TOGGLE()    do { gpio_set_level(LED0_GPIO_PIN, !gpio_get_level(LED0_GPIO_PIN)); } while(0)

/**
 * @brief LED1翻转宏
 * @note 读取当前LED状态并取反，实现LED灯的亮灭切换
 */
#define LED1_TOGGLE()    do { gpio_set_level(LED1_GPIO_PIN, !gpio_get_level(LED1_GPIO_PIN)); } while(0)

/**
 * @brief LED初始化函数
 * @note 配置LED引脚为输入输出模式，禁用中断和上下拉电阻
 */
void led_init(void);     /**< 初始化LED GPIO */

#endif
