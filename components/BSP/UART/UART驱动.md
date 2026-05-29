# UART 驱动说明

## 概述

基于 ESP32-P4 的 UART0 驱动，采用中断接收模式，支持 FIFO 缓冲区和格式化打印。

## 特性

- 中断接收模式：数据到达时通过事件队列通知，高效低功耗
- FIFO 缓冲区：2048 字节软件缓冲，缓存接收数据
- 线程安全：使用互斥锁保护 FIFO 并发访问
- 格式化打印：支持 `uart0_printf()` 函数

## 硬件连接

| 信号 | 引脚     |
| -- | ------ |
| TX | GPIO37 |
| RX | GPIO38 |

## 配置参数

| 参数      | 默认值    |
| ------- | ------ |
| 波特率     | 115200 |
| 数据位     | 8      |
| 停止位     | 1      |
| 校验位     | 无      |
| FIFO 大小 | 2048   |

## 使用方法

### 1. 初始化

```c
#include "uart.h"

void app_main(void)
{
    uart0_init();  // 初始化UART0
}
```

### 2. 发送数据

```c
// 发送字符串
uart0_write("Hello\r\n", 7);

// 格式化打印
uart0_printf("Value: %d\r\n", 123);
```

### 3. 接收数据

```c
// 检查可读字节数
int available = uart0_available();

// 读取数据
char buf[128];
int len = uart0_read(buf, sizeof(buf) - 1);
if (len > 0) {
    buf[len] = '\0';
    uart0_printf("Received: %s\r\n", buf);
}

// 读取单个字符
int c = uart0_getchar();
if (c != -1) {
    uart0_printf("Char: %c\r\n", c);
}
```

### 4. 完整示例

```c
#include "uart.h"

void app_main(void)
{
    uart0_init();

    uart0_printf("UART0 Test\r\n");

    char buf[64];

    while (1) {
        if (uart0_available() > 0) {
            int len = uart0_read(buf, sizeof(buf) - 1);
            if (len > 0) {
                buf[len] = '\0';
                uart0_printf("Echo: %s", buf);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

## API 参考

| 函数                  | 说明       |
| ------------------- | -------- |
| `uart0_init()`      | 初始化UART0 |
| `uart0_write()`     | 发送数据     |
| `uart0_read()`      | 读取数据     |
| `uart0_printf()`    | 格式化打印    |
| `uart0_available()` | 获取可读字节数  |
| `uart0_getchar()`   | 读取单个字符   |

## 注意事项

- 采用中断接收模式，由硬件事件触发，非轮询
- 接收任务运行在后台，数据自动存入FIFO
- FIFO 满时会丢弃新数据
- 读取数据时无需阻塞，直接从FIFO读取

