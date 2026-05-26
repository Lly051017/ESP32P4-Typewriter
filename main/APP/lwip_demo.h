/**
 * @file        lwip_demo.h
 * @brief       TCP服务器头文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @attention   Waiken-Smart 慧勤智远
 * @note        提供TCP服务器的初始化和Socket通信接口
 */

#ifndef __LWIP_DEMO_H
#define __LWIP_DEMO_H

#include <string.h>
#include <sys/socket.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "uart.h"

/** 数据发送标志定义 */
#define LWIP_SEND_DATA              0X80    /**< 标记有数据要发送 */

/** 外部变量声明 */
extern uint8_t g_lwip_send_flag;            /**< 数据发送标志位 */

/**
 * @brief  TCP服务器初始化
 * @note   创建TCP服务器并监听客户端连接
 */
void lwip_demo(void);

#endif
