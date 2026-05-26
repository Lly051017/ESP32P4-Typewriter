/**
 * @file        key.h
 * @brief       按键驱动头文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @note        提供按键扫描接口，支持BOOT和KEY0两个按键
 *              按键采用GPIO输入模式，支持软件消抖处理
 */

#ifndef __KEY_H
#define __KEY_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

/* GPIO引脚定义 - 定义按键连接的GPIO端口号 */
#define BOOT_GPIO_PIN   GPIO_NUM_35   /**< BOOT按键连接到GPIO35 */
#define KEY0_GPIO_PIN   GPIO_NUM_12   /**< KEY0按键连接到GPIO12 */

/**
 * @brief 读取BOOT按键状态宏
 * @retval 返回GPIO引脚电平状态，0表示按下，1表示松开
 */
#define BOOT            gpio_get_level(BOOT_GPIO_PIN)

/**
 * @brief 读取KEY0按键状态宏
 * @retval 返回GPIO引脚电平状态，0表示按下，1表示松开
 */
#define KEY0            gpio_get_level(KEY0_GPIO_PIN)

/* 按键键值定义 - 用于标识不同按键的按下状态 */
#define BOOT_PRES       1       /**< BOOT按键按下返回值 */
#define KEY0_PRES       2       /**< KEY0按键按下返回值 */

/* 函数声明 */
void key_init(void);            /**< 按键GPIO初始化函数 */
uint8_t key_scan(uint8_t mode); /**< 按键扫描函数，支持连按/不连按模式 */

#endif