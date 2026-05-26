/**
 * @file        wifi_config.h
 * @brief       WiFi配置头文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @note        提供WiFi Station模式的初始化和连接管理
 */

#ifndef __WIFI_CONFIG_H
#define __WIFI_CONFIG_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include <netdb.h>
#include "uart.h"

/**
 * @brief 网络连接信息结构体
 * @note  存储WiFi连接状态、IP地址等信息
 */
typedef struct _network_connet_info_t
{
    uint8_t         connet_state;    /**< 网络连接状态标志 */
    char            ip_buf[100];     /**< 获取的IP地址字符串 */
    char            mac_buf[100];   /**< MAC地址字符串(未使用) */
    void (*fun)(uint8_t x);        /**< 状态回调函数指针 */
} network_connet_info;

/** 外部变量声明 */
extern network_connet_info network_connet;

/**
 * @brief  WiFi初始化函数
 * @note   初始化WiFi为Station模式，连接到预设的AP
 */
void wifi_sta_init(void);

#endif
