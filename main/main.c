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
#include "gcode_parser.h"
#include "plotter.h"
#include "writer_controller.h"
#include "stepper_motor.h"
#include "wifi_config.h"
#include "lwip_demo.h"
#include "log.h"

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
#define Gcode_Write_STK_SIZE 8192          /**< G代码写字任务堆栈大小(字节) */
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
    log_init();   /**< 异步日志任务(需在使用log_print的模块之前初始化) */
    led_init();   /**< LED初始化 */
    key_init();   /**< 按键初始化 */
    stepper_motor_init(UART_NUM_2, GPIO_NUM_11, GPIO_NUM_12); /**< 电机UART: GPIO11=TX, GPIO12=RX */
    motor_tasks_init();  /**< 启动 per-motor 并行控制任务 */

    /** 打印启动信息 */
    printf("ESP32-P4 WiFi Plotter\r\n");
    printf("WiFi TCPServer Test\r\n");
    printf("WKS SMART\r\n");

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
    (void)pvParameters;

    printf("\n========================================\n");
    printf("  G代码写字Demo: 书写 \"刘彭\" (含抬笔)\n");
    printf("========================================\n");

    /** 初始化写字控制器（默认配置: X/Y=80脉冲/mm, Z=100脉冲/mm）
     *  注意: UART与per-motor任务已在app_main中初始化, 此处只初始化运动/G代码层
     *  抬笔/落笔由Z轴(电机3)控制: Z5=抬笔, Z0=落笔 */
    writer_init(NULL);
    writer_set_position(0, 0, 0);

    /** ★★★ 字体大小: 改这个缩放因子即可 ★★★
     *  LETTER_SCALE = 1.0 时约 36mm 高; 0.5≈18mm高, 0.7≈25mm高 */
    const float LETTER_SCALE = 0.5f;

    /** 抬笔高度(mm): Z轴抬到多高才算"离纸"。笔头/纸面不平整时需加大。
     *  steps_per_mm[Z]=100, 所以 Z8=800脉冲≈8mm 抬笔 */
    #define PEN_UP_MM   8.0f
    #define PEN_DOWN_MM 0.0f

    /** "刘彭" 的笔画顶点(未缩放,mm,实际=坐标×LETTER_SCALE), 直线段近似。
     *  pen=0: 抬笔移到该点;  pen=1: 落笔画线到该点。
     *
     *  坐标系(逻辑mm, 经旋转+CoreXY变换后映射到物理纸面):
     *    刘 在左(x=2..32),  彭 在右(x=36..70);  y↑ 为字的上方。
     *  两字等高(≈36mm), 顶部对齐 y≈42。
     *
     *  刘 = 文 + 刂;  彭 = 壴 + 彡(三撇)
     */
    typedef struct { float x, y; int pen; } stroke_pt_t;
    static const stroke_pt_t pts[] =
    {
        /* ===== 刘 (x=2..32, y≈6..42, 等高36) ===== */
        /* 文·点  */ {12, 42, 0}, {13, 39,  1},
        /* 文·横  */ {3,  37, 0}, {18, 37,  1},
        /* 文·撇↙ */ {16, 35, 0}, {2,  6,   1},
        /* 文·捺↘ */ {5,  35, 0}, {19, 6,   1},
        /* 刂·短竖 */ {24, 35, 0}, {24, 16,  1},
        /* 刂·竖钩 */ {30, 42, 0}, {30, 8,   1}, {27, 11,  1},

        /* ===== 彭 (x=36..70, y≈6..42, 等高36) ===== */
        /* 壴·上横 */ {42, 42, 0}, {54, 42,  1},
        /* 壴·短竖 */ {48, 42, 0}, {48, 35,  1},
        /* 壴·中横 */ {37, 34, 0}, {58, 34,  1},
        /* 壴·口   */ {42, 31, 0}, {42, 22,  1}, {54, 22, 1}, {54, 31,  1}, {42, 31,  1},
        /* 壴·下横 */ {37, 19, 0}, {59, 19,  1},
        /* 壴·左腿 */ {44, 19, 0}, {42, 9,   1},
        /* 壴·右腿 */ {52, 19, 0}, {54, 9,   1},
        /* 彡·撇1 */ {67, 42, 0}, {58, 35,  1},
        /* 彡·撇2 */ {68, 35, 0}, {59, 28,  1},
        /* 彡·撇3 */ {69, 28, 0}, {60, 21,  1},
    };
    int n = sizeof(pts) / sizeof(pts[0]);

    char line[64];

    printf("\n[写字] 书写 \"刘彭\" (缩放 %.2f, 约 %.0fmm 高, %d 个点)...\n",
           LETTER_SCALE, 36.0f * LETTER_SCALE, n);

    /** 全部指令一次性入队, 由writer任务连续执行(中途不等待),
     *  消除分段之间的软件停顿(轮询/打印), 书写更连贯 */
    writer_execute_gcode("G21");   /**< 公制单位 */
    writer_execute_gcode("G90");   /**< 绝对坐标 */

    int pen_down = -1;  /**< -1未知, 0抬笔, 1落笔 */
    for (int i = 0; i < n; i++)
    {
        /** 按需切换抬笔/落笔, Z高度按PEN_UP_MM/PEN_DOWN_MM */
        if (pts[i].pen == 1 && pen_down != 1) {
            snprintf(line, sizeof(line), "G0 Z%.1f", PEN_DOWN_MM);
            writer_execute_gcode(line);   /**< 落笔 */
            pen_down = 1;
        } else if (pts[i].pen == 0 && pen_down != 0) {
            snprintf(line, sizeof(line), "G0 Z%.1f", PEN_UP_MM);
            writer_execute_gcode(line);   /**< 抬笔 */
            pen_down = 0;
        }

        /** 生成移动指令(坐标乘以缩放因子)并入队 */
        snprintf(line, sizeof(line), "%s X%.2f Y%.2f F300",
                 pts[i].pen ? "G1" : "G0",
                 pts[i].x * LETTER_SCALE, pts[i].y * LETTER_SCALE);
        writer_execute_gcode(line);
    }
    snprintf(line, sizeof(line), "G0 Z%.1f", PEN_UP_MM);
    writer_execute_gcode(line);   /**< 写完抬笔 */

    /** 全部入队后, 等待整字写完 */
    writer_wait_idle();

    printf("\n========================================\n");
    printf("  写字完成!\n");
    printf("========================================\n");

    while (1) 
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
