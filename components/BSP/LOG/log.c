/**
 * @file        log.c
 * @brief       异步日志模块源文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @note        生产者(各任务)->队列->消费者(日志任务)->控制台。
 *              生产者投递非阻塞(队满丢弃), 消费者为最低优先级任务,
 *              统一调用printf输出, 不占用实时任务的时间片。
 */

#include "log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdio.h>

/** 单条日志消息结构 */
typedef struct {
    char msg[LOG_MSG_MAX_LEN];
} log_msg_t;

/** 日志消息队列句柄 */
static QueueHandle_t s_log_queue = NULL;

/**
 * @brief       日志消费任务: 从队列取出日志并打印
 * @param       arg: 未使用
 * @note        最低优先级, 阻塞等待队列, 空闲时不占CPU
 */
static void log_task(void *arg)
{
    (void)arg;
    log_msg_t m;

    for (;;) {
        if (xQueueReceive(s_log_queue, &m, portMAX_DELAY) == pdTRUE) {
            printf("%s", m.msg);
        }
    }
}

void log_init(void)
{
    if (s_log_queue != NULL) {
        return;  /**< 已初始化 */
    }

    s_log_queue = xQueueCreate(LOG_QUEUE_LEN, sizeof(log_msg_t));
    configASSERT(s_log_queue);

    /** 最低优先级(1), 仅在有日志时被唤醒打印 */
    xTaskCreate(log_task, "log_task", 4096, NULL, 1, NULL);
}

void log_print(const char *fmt, ...)
{
    if (s_log_queue == NULL) {
        return;
    }

    log_msg_t m;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m.msg, sizeof(m.msg), fmt, ap);
    va_end(ap);

    /** 非阻塞投递: 队满则丢弃, 绝不阻塞调用任务 */
    xQueueSend(s_log_queue, &m, 0);
}
