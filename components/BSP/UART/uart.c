/**
 * @file        uart.c
 * @brief       UART驱动代码源文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @note        实现UART0的初始化、数据收发、FIFO管理和格式化打印功能
 *              采用环形缓冲区实现异步数据接收，支持多任务并发访问
 */

#include "uart.h"
#include "string.h"
#include "stdarg.h"

/**
 * @brief UART0 事件队列句柄
 * @note 用于接收UART事件，如接收完成、FIFO溢出等
 */
static QueueHandle_t uart0_queue;

/**
 * @brief UART0 软件FIFO接收缓冲区
 * @note 存储从UART接收的数据，供应用程序读取
 */
static uart_fifo_t rx_fifo;

/**
 * @brief FIFO互斥锁
 * @note 用于保护FIFO缓冲区的并发访问，防止数据竞争
 */
static SemaphoreHandle_t fifo_mutex;

/**
 * @brief 初始化FIFO缓冲区
 * @param  fifo: FIFO结构体指针
 * @param  size: 缓冲区大小
 * @retval 无
 */
static void fifo_init(uart_fifo_t *fifo, uint16_t size)
{
    fifo->size = size;
    fifo->head = 0;
    fifo->tail = 0;
}

/**
 * @brief 写入数据到FIFO缓冲区
 * @param  fifo: FIFO结构体指针
 * @param  data: 数据指针
 * @param  len:  数据长度
 * @retval 实际写入的字节数
 * @note  如果缓冲区满，则只写入能容纳的数据量
 */
static uint16_t fifo_write(uart_fifo_t *fifo, const char *data, uint16_t len)
{
    uint16_t i = 0;
    for (i = 0; i < len; i++)
    {
        /** 计算下一个写入位置(环形) */
        uint16_t next = (fifo->head + 1) % fifo->size;
        /** 检查缓冲区是否已满 */
        if (next == fifo->tail)
            break;
        fifo->buffer[fifo->head] = data[i];
        fifo->head = next;
    }
    return i;
}

/**
 * @brief 从FIFO缓冲区读取数据
 * @param  fifo: FIFO结构体指针
 * @param  data: 数据缓存区指针
 * @param  len:  期望读取的长度
 * @retval 实际读取的字节数
 * @note 如果缓冲区数据不足，则只读取有的数据
 */
static uint16_t fifo_read(uart_fifo_t *fifo, char *data, uint16_t len)
{
    uint16_t i = 0;
    for (i = 0; i < len; i++)
    {
        /** 检查缓冲区是否为空 */
        if (fifo->tail == fifo->head)
            break;
        data[i] = fifo->buffer[fifo->tail];
        fifo->tail = (fifo->tail + 1) % fifo->size;
    }
    return i;
}

/**
 * @brief 获取FIFO中有效数据的字节数
 * @param  fifo: FIFO结构体指针
 * @retval 有效数据的字节数
 */
static uint16_t fifo_available(uart_fifo_t *fifo)
{
    /** 处理未回环的情况 */
    if (fifo->head >= fifo->tail)
    {
        return fifo->head - fifo->tail;
    }
    /** 处理已回环的情况 */
    return fifo->size - fifo->tail + fifo->head;
}

/**
 * @brief UART接收任务(中断驱动模式)
 * @param  arg: 任务参数
 * @retval 无
 * @note 通过事件队列等待UART中断事件，收到数据后将数据写入FIFO缓冲区
 */
static void uart_rx_task(void *arg)
{
    uart_event_t event;
    uint8_t data[128];

    /** 任务主循环 */
    while (1)
    {
        /** 等待UART事件 */
        if (xQueueReceive(uart0_queue, &event, portMAX_DELAY) == pdTRUE)
        {
            /** 处理接收到数据的事件 */
            if (event.type == UART_DATA)
            {
                /** 从UART硬件FIFO读取数据 */
                int len = uart_read_bytes(UART_NUM_0, data, sizeof(data), portMAX_DELAY);
                if (len > 0)
                {
                    /** 获取互斥锁保护FIFO */
                    xSemaphoreTake(fifo_mutex, portMAX_DELAY);
                    /** 将数据写入FIFO缓冲区 */
                    fifo_write(&rx_fifo, (char *)data, len);
                    xSemaphoreGive(fifo_mutex);
                }
            }
        }
    }
}

/**
 * @brief 初始化UART0
 * @param  无
 * @retval 无
 * @note 配置UART参数、安装驱动、初始化FIFO、创建接收任务
 *       - 波特率: 115200
 *       - 数据位: 8位
 *       - 停止位: 1位
 *       - 校验位: 无
 *       - 流控:   无
 */
void uart0_init(void)
{
    uart_config_t uart0_config = {0};
    /** 配置UART参数 */
    uart0_config.baud_rate = UART0_BAUD_RATE;          /**< 设置波特率115200 */
    uart0_config.data_bits = UART_DATA_8_BITS;         /**< 8位数据位 */
    uart0_config.parity = UART_PARITY_DISABLE;         /**< 无奇偶校验 */
    uart0_config.stop_bits = UART_STOP_BITS_1;         /**< 1位停止位 */
    uart0_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE; /**< 无硬件流控 */
    uart0_config.source_clk = UART_SCLK_DEFAULT;       /**< 默认时钟源 */

    /** 安装UART驱动，创建事件队列（必须先于 param_config） */
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, UART0_BUF_SIZE * 2, UART0_BUF_SIZE * 2, 20, &uart0_queue, 0));
    /** 应用UART参数配置 */
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart0_config));
    /** 配置UART引脚 */
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, UART0_TX_PIN, UART0_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    /** 初始化FIFO缓冲区和互斥锁 */
    fifo_init(&rx_fifo, UART0_FIFO_SIZE);
    fifo_mutex = xSemaphoreCreateMutex();
    /** 创建UART接收任务，优先级10，堆栈2048字节 */
    xTaskCreate(uart_rx_task, "uart_rx", 2048, NULL, 10, NULL);
}

/**
 * @brief UART0 发送数据
 * @param  data: 数据指针
 * @param  len:  数据长度
 * @retval 实际发送的字节数
 */
int uart0_write(const char *data, size_t len)
{
    return uart_write_bytes(UART_NUM_0, data, len);
}

/**
 * @brief UART0 读取数据
 * @param  data: 数据缓存区指针
 * @param  len:  期望读取的长度
 * @retval 实际读取的字节数
 * @note 从FIFO缓冲区读取数据，使用互斥锁保护
 */
int uart0_read(char *data, size_t len)
{
    xSemaphoreTake(fifo_mutex, portMAX_DELAY);
    int ret = fifo_read(&rx_fifo, data, len);
    xSemaphoreGive(fifo_mutex);
    return ret;
}

/**
 * @brief 获取FIFO中可读取的字节数
 * @param  无
 * @retval 可读取的字节数
 */
int uart0_available(void)
{
    xSemaphoreTake(fifo_mutex, portMAX_DELAY);
    int ret = fifo_available(&rx_fifo);
    xSemaphoreGive(fifo_mutex);
    return ret;
}

/**
 * @brief 从FIFO读取单个字符
 * @param  无
 * @retval 读取的字符，失败返回-1
 */
int uart0_getchar(void)
{
    char c;
    if (uart0_read(&c, 1) > 0)
    {
        return c;
    }
    return -1;
}

/**
 * @brief UART0 格式化打印
 * @param  format: 格式化字符串
 * @param  ...: 可变参数列表
 * @retval 输出的字符数
 * @note 类似printf，输出到USB串口控制台（不占用UART0电机总线）
 */
int uart0_printf(const char *format, ...)
{
    char buffer[256];
    va_list args;
    int ret;

    va_start(args, format);
    ret = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (ret > 0)
    {
        printf("%s", buffer);
    }

    return ret;
}
