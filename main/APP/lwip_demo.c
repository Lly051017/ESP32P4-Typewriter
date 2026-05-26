/**
 * @file        lwip_demo.c
 * @brief       TCP服务器源文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @note        实现TCP服务器的初始化、客户端连接管理和数据收发
 *              接收客户端发送的G代码数据
 */

#include "lwip_demo.h"

/** TCP服务器配置参数 */
#define LWIP_DEMO_RX_BUFSIZE         200                        /**< 最大接收数据长度 */
#define LWIP_DEMO_PORT               8080                       /**< TCP服务器监听端口号 */
#define LWIP_SEND_THREAD_PRIO       ( tskIDLE_PRIORITY + 3 )    /**< 发送任务优先级 */

/** 接收数据缓冲区 */
uint8_t g_lwip_demo_recvbuf[LWIP_DEMO_RX_BUFSIZE];

/** 发送数据内容 */
uint8_t g_lwip_demo_sendbuf[] = "WKS SMART DATA \r\n";

/** 数据发送标志位 */
uint8_t g_lwip_send_flag;

/** 客户端连接Socket描述符 */
int g_sock_conn;

/** 连接状态标志 */
int g_lwip_connect_state = 0;

/** 发送任务函数声明 */
static void lwip_send_thread(void *arg);

/**
 * @brief       创建发送任务
 * @param       无
 * @retval      无
 * @note        创建一个后台任务用于发送数据到客户端
 */
void lwip_data_send(void)
{
    xTaskCreate(lwip_send_thread, "lwip_send_thread", 4096, NULL, LWIP_SEND_THREAD_PRIO, NULL);
}

/**
 * @brief       TCP服务器入口函数
 * @param       无
 * @retval      无
 * @note        创建TCP服务器socket，绑定端口，监听客户端连接
 *              接收客户端数据并通过UART输出
 */
void lwip_demo(void)
{
    struct sockaddr_in server_addr; /**< 服务器地址结构 */
    struct sockaddr_in conn_addr;   /**< 客户端地址结构 */
    socklen_t addr_len;             /**< 地址结构长度 */
    int err;                        /**< 错误码 */
    int length;                     /**< 接收数据长度 */
    int sock_fd;                    /**< 服务器socket描述符 */
    char *tbuf;                     /**< 临时缓冲区 */

    /** 创建发送任务 */
    lwip_data_send();

    /** 创建TCP socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    /** 清零服务器地址结构 */
    memset(&server_addr, 0, sizeof(server_addr));

    /** 配置服务器地址 */
    server_addr.sin_family = AF_INET;                      /**< IPv4协议 */
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);       /**< 监听所有网卡 */
    server_addr.sin_port = htons(LWIP_DEMO_PORT);          /**< 端口号8080 */

    /** 分配临时缓冲区并打印端口信息 */
    tbuf = malloc(200);
    sprintf((char *)tbuf, "Port:%d", LWIP_DEMO_PORT);
    uart0_printf("%s\r\n", tbuf);

    /** 绑定socket到指定端口 */
    err = bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    if (err < 0)
    {
        /** 绑定失败，关闭socket */
        closesocket(sock_fd);
        free(tbuf);
    }

    /** 开始监听连接请求 */
    err = listen(sock_fd, 4);

    if (err < 0)
    {
        /** 监听失败，关闭socket */
        closesocket(sock_fd);
    }

    /** 主循环：接受并处理客户端连接 */
    while(1)
    {
        g_lwip_connect_state = 0;
        addr_len = sizeof(struct sockaddr_in);

        /** 接受客户端连接 */
        g_sock_conn = accept(sock_fd, (struct sockaddr *)&conn_addr, &addr_len);

        if (g_sock_conn < 0)
        {
            /** 连接失败，关闭socket */
            closesocket(sock_fd);
        }
        else
        {
            /** 连接成功 */
            uart0_printf("State:Connection...\r\n");
            g_lwip_connect_state = 1;
        }

        /** 与客户端通信循环 */
        while (1)
        {
            /** 清零接收缓冲区 */
            memset(g_lwip_demo_recvbuf,0,LWIP_DEMO_RX_BUFSIZE);

            /** 接收客户端数据 */
            length = recv(g_sock_conn, (unsigned int *)g_lwip_demo_recvbuf, sizeof(g_lwip_demo_recvbuf), 0);

            /** 检查连接状态 */
            if (length <= 0)
            {
                uart0_printf("State:no Connection\r\n");
                break;
            }

            /** 打印接收到的数据 */
            uart0_printf("%s\r\n", g_lwip_demo_recvbuf);
        }

        /** 关闭客户端连接 */
        if (g_sock_conn >= 0)
        {
            closesocket(g_sock_conn);
        }

        g_sock_conn = -1;
    }
}

/**
 * @brief       数据发送任务函数
 * @param       pvParameters: 任务参数(未使用)
 * @retval      无
 * @note        监测发送标志，将数据发送到已连接的客户端
 */
void lwip_send_thread(void *pvParameters)
{
    pvParameters = pvParameters;

    /** 任务主循环 */
    while (1)
    {
        /** 检查发送标志和连接状态 */
        if(((g_lwip_send_flag & LWIP_SEND_DATA) == LWIP_SEND_DATA) && (g_lwip_connect_state == 1))
        {
            /** 发送数据到客户端 */
            send(g_sock_conn, g_lwip_demo_sendbuf, sizeof(g_lwip_demo_sendbuf), 0);

            /** 清除发送标志 */
            g_lwip_send_flag &= ~LWIP_SEND_DATA;
        }

        vTaskDelay(1);
    }
}
