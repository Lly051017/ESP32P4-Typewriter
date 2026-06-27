/**
 * @file        log.h
 * @brief       异步日志模块头文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @note        通过消息队列实现的异步日志: 各任务调用log_print()只把
 *              格式化后的字符串投递到队列(非阻塞,队满即丢弃), 由独立的
 *              低优先级日志任务统一打印, 从而不阻塞调用方、不扰动实时任务。
 */

#ifndef __LOG_H
#define __LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 单条日志最大长度(含结束符) */
#define LOG_MSG_MAX_LEN   128

/** 日志队列深度(可缓存的未打印日志条数) */
#define LOG_QUEUE_LEN     32

/**
 * @brief  初始化日志模块(创建队列与日志任务)
 * @note   在创建其他任务前调用一次即可
 */
void log_init(void);

/**
 * @brief  异步打印日志(printf风格)
 * @param  fmt: 格式化字符串
 * @param  ...: 可变参数
 * @note   非阻塞: 仅把字符串投递到队列, 队满则丢弃该条日志,
 *         绝不阻塞调用任务, 适合在电机/中断等实时路径中使用
 */
void log_print(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
