/**
 * @file        wifi_config.c
 * @brief       WiFi配置源文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @note        实现WiFi Station模式的初始化和连接管理
 */

#include "wifi_config.h"
#include "esp_log.h"

/** WiFi连接配置 - 请根据实际网络修改 */
#define DEFAULT_SSID        "lly"           /**< WiFi名称(SSID) */
#define DEFAULT_PWD         "p7krsest"      /**< WiFi密码 */

/** WiFi事件标志组和标志位 */
static EventGroupHandle_t   wifi_event;      /**< WiFi事件标志组句柄 */
#define WIFI_CONNECTED_BIT  BIT0           /**< WiFi连接成功标志 */
#define WIFI_FAIL_BIT       BIT1           /**< WiFi连接失败标志 */

/** 网络连接信息全局变量 */
network_connet_info network_connet;

/** 日志标签 */
static const char *TAG = "static_ip";

/** LCD显示缓冲区 */
char lcd_buff[100] = {0};

/**
 * @brief WiFi配置结构体初始化宏
 * @note  配置Station模式的SSID和密码
 */
#define WIFICONFIG()   {                            \
    .sta = {                                        \
        .ssid = DEFAULT_SSID,                      \
        .password = DEFAULT_PWD,                   \
        .threshold.authmode = WIFI_AUTH_WPA2_PSK,  \
    },                                              \
}

/**
 * @brief       连接状态显示回调函数
 * @param       flag: 状态标志位
 * @retval      无
 * @note        根据不同状态标志位打印相应的连接信息
 */
void connet_display(uint8_t flag)
{
    /** 显示WiFi连接信息(bit7) */
    if((flag & 0x80) == 0x80)
    {
        uart0_printf("ssid:%s\r\n", DEFAULT_SSID);
        uart0_printf("psw:%s\r\n", DEFAULT_PWD);
        uart0_printf("BOOT:Send data\r\n");
    }
    /** 显示正在连接(bit2) */
    else if ((flag & 0x04) == 0x04)
    {
        uart0_printf("wifi connecting......\r\n");
    }
    /** 显示连接失败(bit1) */
    else if ((flag & 0x02) == 0x02)
    {
        uart0_printf("wifi connecting fail\r\n");
    }
    /** 显示获取的IP地址(bit0) */
    else if ((flag & 0x01) == 0x01)
    {
        uart0_printf("%s\r\n", network_connet.ip_buf);
    }

    /** 清除状态标志 */
    network_connet.connet_state &= 0x00;
}

/**
 * @brief       WiFi事件处理回调函数
 * @param       arg: 网卡控制块
 * @param       event_base: 事件类型
 * @param       event_id: 事件ID
 * @param       event_data: 事件数据
 * @retval      无
 * @note        处理WiFi连接、断开、获取IP等事件
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    static int s_retry_num = 0;                    /**< 重试次数计数器 */

    /** 处理WiFi启动事件 */
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        network_connet.connet_state |= 0x04;       /**< 标记正在连接 */
        network_connet.fun(network_connet.connet_state);
        esp_wifi_connect();                        /**< 开始连接WiFi */
    }
    /** 处理WiFi连接成功事件 */
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)
    {
        network_connet.connet_state |= 0x80;      /**< 标记连接成功 */
        network_connet.fun(network_connet.connet_state);
    }
    /** 处理WiFi断开连接事件 */
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        network_connet.connet_state |= 0x02;      /**< 标记连接失败 */

        /** 尝试重新连接(最多重试20次) */
        if (s_retry_num < 20)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            /** 重试次数用完，设置失败标志 */
            xEventGroupSetBits(wifi_event, WIFI_FAIL_BIT);
            network_connet.fun(network_connet.connet_state);
        }

        ESP_LOGI(TAG,"connect to the AP fail");
    }
    /** 处理获取IP地址事件 */
    else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        network_connet.connet_state |= 0x01;      /**< 标记已获取IP */

        /** 解析并保存IP地址 */
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "static ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;                           /**< 重置重试计数器 */
        sprintf(network_connet.ip_buf, "static ip:" IPSTR, IP2STR(&event->ip_info.ip));
        network_connet.fun(network_connet.connet_state);

        /** 设置连接成功标志 */
        xEventGroupSetBits(wifi_event, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief       WiFi Station模式初始化
 * @param       无
 * @retval      无
 * @note        配置WiFi参数，连接到预设的AP，等待获取IP地址
 */
void wifi_sta_init(void)
{
    static esp_netif_t *sta_netif = NULL;

    /** 初始化网络连接信息 */
    network_connet.connet_state = 0x00;
    network_connet.fun = connet_display;

    /** 创建WiFi事件标志组 */
    wifi_event = xEventGroupCreate();

    /** 初始化TCP/IP协议栈 */
    ESP_ERROR_CHECK(esp_netif_init());

    /** 创建默认事件循环 */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /** 创建默认的WiFi Station网络接口 */
    sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    /** WiFi初始化配置 */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    /** 注册WiFi事件处理函数 */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    /** 初始化WiFi驱动 */
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /** 配置WiFi参数 */
    wifi_config_t  wifi_config = WIFICONFIG();

    /** 设置WiFi模式为Station */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    /** 应用WiFi配置 */
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

    /** 启动WiFi */
    ESP_ERROR_CHECK(esp_wifi_start());

    /** 等待连接结果 */
    EventBits_t bits = xEventGroupWaitBits(wifi_event,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE,
                                            pdFALSE,
                                            portMAX_DELAY);

    /** 判断连接结果 */
    if (bits & WIFI_CONNECTED_BIT)
    {
        /** WiFi连接成功 */
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 DEFAULT_SSID, DEFAULT_PWD);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        /** WiFi连接失败 */
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 DEFAULT_SSID, DEFAULT_PWD);
    }
    else
    {
        /** 未知错误 */
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /** 删除事件标志组 */
    vEventGroupDelete(wifi_event);
}
