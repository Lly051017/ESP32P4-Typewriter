/**
 * @file        uart.h
 * @brief       UART驱动代码头文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @attention   Waiken-Smart 慧勤智远
 * @note        提供UART0的初始化、数据收发和格式化打印接口
 *              采用FIFO环形缓冲区实现异步数据接收，支持printf风格打印
 */

#ifndef __UART_H
#define __UART_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"

/**
 * @brief UART0 配置参数
 * @{
 */
#define UART0_TX_PIN       GPIO_NUM_12      /**< UART0 TX引脚 - GPIO12 */
    #define UART0_RX_PIN       GPIO_NUM_11      /**< UART0 RX引脚 - GPIO11 */
#define UART0_BAUD_RATE    115200           /**< 波特率 - 115200 */
#define UART0_BUF_SIZE     1024              /**< 硬件FIFO缓冲区大小 */
#define UART0_FIFO_SIZE    2048              /**< 软件环形缓冲区大小 */
/** @} */

/**
 * @brief UART FIFO 环形缓冲区结构体
 * @note  用于缓存接收到的数据，实现异步数据接收
 *        采用头尾指针实现环形队列，支持并发访问
 */
typedef struct {
    char buffer[UART0_FIFO_SIZE];           /**< 缓冲区数组 */
    volatile uint16_t head;                  /**< 写入位置索引 */
    volatile uint16_t tail;                  /**< 读取位置索引 */
    uint16_t size;                           /**< 缓冲区总大小 */
} uart_fifo_t;

/**
 * @brief  初始化UART0
 * @note   配置UART参数、GPIO引脚、安装驱动、创建FIFO缓冲区和接收任务
 */
void uart0_init(void);

/**
 * @brief UART0 发送数据
 * @param  data: 发送数据指针
 * @param  len:  数据长度(字节数)
 * @retval 实际发送的字节数
 */
int uart0_write(const char *data, size_t len);

/**
 * @brief UART0 读取数据
 * @param  data: 数据缓存区指针
 * @param  len:  期望读取的长度
 * @retval 实际读取的字节数
 * @note 从FIFO缓冲区读取数据，非阻塞操作
 */
int uart0_read(char *data, size_t len);

/**
 * @brief UART0 格式化打印
 * @param  format: 格式化字符串(printf风格)
 * @param  ...:  可变参数列表
 * @retval 输出的字符数(不含字符串结束符)
 */
int uart0_printf(const char *format, ...);

/**
 * @brief 获取FIFO中可读取的字节数
 * @retval 可读取的字节数
 */
int uart0_available(void);

/**
 * @brief 从FIFO读取单个字符
 * @retval 读取的字符，失败返回-1
 */
int uart0_getchar(void);

#endif
