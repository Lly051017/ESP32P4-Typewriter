/**
 * @file        main.c
 * @brief       ESP32写字机主程序入口
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @attention   Waiken-Smart 慧勤智远
 * @note        项目主程序入口，初始化各外设组件，创建FreeRTOS任务
 *              实现WiFi连接、TCP服务器、G代码执行等功能
 */

#include "esp_system.h"
#include "nvs_flash.h"
#include "key.h"
#include "led.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uart.h"
#include "wifi_config.h"
#include "lwip_demo.h"

/* FreeRTOS相关头文件 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

/**
 * @brief LED任务配置
 * @{
 */
#define LED_TASK_PRIO 10           /**< LED任务优先级 */
#define LED_STK_SIZE 2048          /**< LED任务堆栈大小(字节) */
TaskHandle_t LEDTask_Handler;      /**< LED任务句柄 */
/** @} */

/**
 * @brief 按键任务配置
 * @{
 */
#define KEY_TASK_PRIO 11           /**< 按键任务优先级 */
#define KEY_STK_SIZE 2048          /**< 按键任务堆栈大小(字节) */
TaskHandle_t KEYTask_Handler;     /**< 按键任务句柄 */
/** @} */

/**
 * @brief G代码写字任务配置
 * @{
 */
#define Gcode_Write_TASK_PORT 12           /**< G代码写字任务优先级 */
#define Gcode_Write_STK_SIZE 2048          /**< G代码写字任务堆栈大小(字节) */
TaskHandle_t GcodeWriteTask_Handler;      /**< G代码写字任务句柄 */
/** @} */

/**
 * @brief 临界段保护的自旋锁
 * @note  用于保护多任务间共享资源
 */
static portMUX_TYPE my_spinlock = portMUX_INITIALIZER_UNLOCKED;

/* 任务函数声明 */
void led_task(void *pvParameters);     /**< LED任务函数 */
void key_task(void *pvParameters);     /**< 按键任务函数 */
void Gcode_Write_Task(void *pvParameters); /**< G代码写字任务函数 */

/**
 * @brief       程序入口函数
 * @param       无
 * @retval      无
 * @note        初始化NVS存储、LED、按键、UART等外设
 *              创建FreeRTOS任务，初始化WiFi和TCP服务器
 */
void app_main(void)
{
    esp_err_t ret;

    /** 初始化NVS(非易失性存储) */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        /** NVS分区有问题，擦除后重新初始化 */
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /** 初始化各外设 */
    led_init();   /**< LED初始化 */
    key_init();   /**< 按键初始化 */
    uart0_init(); /**< UART0初始化(用于调试输出) */

    /** 打印启动信息 */
    uart0_printf("ESP32-P4 WiFi Plotter\r\n");
    uart0_printf("WiFi TCPServer Test\r\n");
    uart0_printf("WKS SMART\r\n");

    /** 初始化WiFi(Station模式) */
    wifi_sta_init();

    /** 进入临界区保护 */
    taskENTER_CRITICAL(&my_spinlock);

    /** 创建按键任务 - 优先级11 */
    xTaskCreate((TaskFunction_t)key_task,
                (const char *)"key_task",
                (uint16_t)KEY_STK_SIZE,
                (void *)NULL,
                (UBaseType_t)KEY_TASK_PRIO,
                (TaskHandle_t *)&KEYTask_Handler);

    /** 创建LED任务 - 优先级10 */
    xTaskCreate((TaskFunction_t)led_task,
                (const char *)"led_task",
                (uint16_t)LED_STK_SIZE,
                (void *)NULL,
                (UBaseType_t)LED_TASK_PRIO,
                (TaskHandle_t *)&LEDTask_Handler);

    /** 创建G代码写字任务 - 优先级12 */
    xTaskCreate((TaskFunction_t)Gcode_Write_Task,
                (const char *)"Gcode_write_task",
                (uint16_t)Gcode_Write_STK_SIZE,
                (void *)NULL,
                (UBaseType_t)Gcode_Write_TASK_PORT,
                (TaskHandle_t *)&GcodeWriteTask_Handler);

    /** 退出临界区保护 */
    taskEXIT_CRITICAL(&my_spinlock);

    /** 启动TCP服务器 */
    lwip_demo();
}

/**
 * @brief       按键任务函数
 * @param       pvParameters: 传入参数(未使用)
 * @retval      无
 * @note        轮询检测按键状态，支持BOOT按键触发数据发送
 */
void key_task(void *pvParameters)
{
    pvParameters = pvParameters;

    uint8_t key;

    /** 任务主循环 */
    while (1)
    {
        /** 扫描按键状态(单次模式) */
        key = key_scan(0);

        /** BOOT按键按下时触发数据发送 */
        if (BOOT_PRES == key)
        {
            g_lwip_send_flag |= LWIP_SEND_DATA; /**< 设置发送标志 */
        }

        /** 延时10ms再次扫描 */
        vTaskDelay(10);
    }
}

/**
 * @brief       LED任务函数
 * @param       pvParameters: 传入参数(未使用)
 * @retval      无
 * @note        LED1每500ms翻转一次，用于指示系统运行状态
 */
void led_task(void *pvParameters)
{
    pvParameters = pvParameters;

    /** 任务主循环 */
    while (1)
    {
        LED1_TOGGLE(); /**< 翻转LED1状态 */
        vTaskDelay(500); /**< 延时500ms */
    }
}

/**
 * @brief       G代码写字任务函数
 * @param       pvParameters: 传入参数(未使用)
 * @retval      无
 * @note        初始化G代码控制器，等待并处理G代码命令
 */
void Gcode_Write_Task(void *pvParameters)
{
    pvParameters = pvParameters;

    /** 任务主循环 */
    while (1)
    {
        vTaskDelay(10); /**< 延时10ms */
    }
}
