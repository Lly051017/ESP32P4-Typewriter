/**
 * @file        lcd.c
 * @brief       LCD显示驱动源文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @note        实现LCD显示驱动的初始化、图形绘制、文字显示等功能
 *              采用双缓冲机制避免显示闪烁，使用DMA传输提高效率
 */

#include "lcd.h"
#include "lcdfont.h"

/** 指向双帧缓冲区的指针 - 使用DRAM存储以提高访问速度 */
DRAM_ATTR void *lcd_buffer[2];              /**< 屏幕双缓存指针数组 */
DRAM_ATTR uint8_t buffer_sw = 0;            /**< 当前使用的缓冲区索引(0或1) */
DRAM_ATTR uint8_t refresh_done_flag = 0;    /**< 缓存刷新完成标志 */
DRAM_ATTR _lcd_dev lcddev;                  /**< LCD设备参数全局变量 */
uint32_t g_back_color  = 0xFFFF;            /**< 背景色全局变量 */

/**
 * @brief       内部缓存刷新完成回调函数
 * @param       panel_io: MIPILCD IO的句柄
 * @param       edata: 事件数据类型
 * @param       user_ctx: 用户参数
 * @retval      false
 * @note        当LCD帧缓冲区刷新完成时会被调用，设置刷新完成标志
 */
IRAM_ATTR static bool lcd_panel_refresh_done_callback(esp_lcd_panel_handle_t panel_io, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    refresh_done_flag = 1;
    return false;
}

/**
 * @brief       初始化LCD显示器
 * @param       无
 * @retval      无
 * @note        配置LCD GPIO参数，初始化MIPI LCD控制器，设置双缓冲
 */
void lcd_init(void)
{
    /** 配置LCD控制和背光GPIO参数 */
    lcddev.ctrl.lcd_rst = LCD_RST_PIN;                          /**< 复位GPIO */
    lcddev.ctrl.lcd_bl = LCD_BL_PIN;                            /**< 背光GPIO */

    gpio_config_t gpio_init_struct = {0};
    gpio_init_struct.intr_type    = GPIO_INTR_DISABLE;          /**< 禁用引脚中断 */
    gpio_init_struct.mode         = GPIO_MODE_OUTPUT;           /**< 输出模式 */
    gpio_init_struct.pull_up_en   = GPIO_PULLUP_DISABLE;        /**< 禁用上拉 */
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;      /**< 禁用下拉 */
    gpio_init_struct.pin_bit_mask = 1ull << lcddev.ctrl.lcd_bl; /**< 背光引脚位掩码 */
    ESP_ERROR_CHECK(gpio_config(&gpio_init_struct));            /**< 配置GPIO */

    LCD_BL(0);      /**< 初始关闭背光 */

    /** 初始化MIPI LCD并获取帧缓冲区 */
    lcddev.lcd_panel_handle = mipi_lcd_init();                  /**< 初始化MIPI LCD */
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(lcddev.lcd_panel_handle, 2, &lcd_buffer[0], &lcd_buffer[1])); /**< 获取双帧缓冲区 */

    /** 注册刷新完成回调函数 */
    const esp_lcd_dpi_panel_event_callbacks_t mipi_cbs = {
        .on_refresh_done = lcd_panel_refresh_done_callback,     /**< 内部缓冲区刷新完成回调 */
    };

    esp_lcd_dpi_panel_register_event_callbacks(lcddev.lcd_panel_handle, &mipi_cbs, NULL);
    lcd_clear(WHITE);                                           /**< 清屏为白色 */
    LCD_BL(1);      /**< 打开背光 */
}

/**
 * @brief       清屏函数
 * @param       color: 清屏颜色(RGB565格式)
 * @retval      无
 * @note        使用指定颜色填充整个屏幕，等待刷新完成
 */
IRAM_ATTR void lcd_clear(uint16_t color)
{
    uint16_t *buffer = (uint16_t *)lcd_buffer[buffer_sw];  /**< 获取当前缓冲区指针 */

    /** 填充颜色值到整个缓冲区 */
    for (uint32_t i = 0; i < lcddev.width * lcddev.height; i++)
    {
        buffer[i] = color;
    }

    /** 发送帧缓冲区到LCD显示 */
    esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, 0, 0, lcddev.width, lcddev.height, buffer);
    refresh_done_flag = 0;                                     /**< 清除刷新完成标志 */

    /** 等待内部缓存刷新完成 */
    do
    {
        vTaskDelay(1);
    }
    while (refresh_done_flag != 1);

    buffer_sw ^= 1;                                            /**< 切换到另一个缓冲区 */
}

/**
 * @brief       画点函数
 * @param       x,y:   写入坐标
 * @param       color: 颜色值
 * @retval      无
 * @note        在指定位置绘制一个像素点
 */
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, x, y, x + 1, y + 1, (uint16_t *)&color);
}

/**
 * @brief       在指定矩形区域内填充单一颜色
 * @param       sx,sy: 起始坐标
 * @param       ex,ey: 结束坐标
 * @param       color: 要填充的颜色
 * @retval      无
 * @note        此函数仅支持RGB565格式的颜色
 */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color)
{
    /** 确保坐标在合法范围内 */
    if (sx >= lcddev.width || sy >= lcddev.height || ex > lcddev.width || ey > lcddev.height || sx >= ex || sy >= ey)
    {
        ESP_LOGE("TAG", "Invalid fill area");
        return;
    }

    /** 计算填充区域的宽度和高度 */
    uint16_t width = ex - sx;
    uint16_t height = ey - sy;

    /** 从内部RAM分配颜色缓冲区 */
    uint16_t *buffer = heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_INTERNAL);

    if (NULL == buffer)
    {
        ESP_LOGE("TAG", "Memory for bitmap is not enough");
    }
    else
    {
        /** 填充颜色 */
        for (uint16_t i = 0; i < width; i++)
        {
            buffer[i] = color;
        }

        /** 逐行绘制填充区域 */
        for (uint16_t y = 0; y < height; y++)
        {
            esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, sx, sy + y, ex, sy + y + 1, buffer);
        }
    }

    heap_caps_free(buffer);                                    /**< 释放内存 */
}

/**
 * @brief       在指定矩形区域内填充颜色块
 * @param       sx,sy,ex,ey: 填充矩形对角坐标
 * @param       color: 要填充的颜色数组首地址
 * @retval      无
 * @note        每个像素的颜色由color数组依次提供
 */
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color)
{
    /** 确保坐标在合法范围内 */
    if (sx >= lcddev.width || sy >= lcddev.height || ex > lcddev.width || ey > lcddev.height || sx >= ex || sy >= ey)
    {
        ESP_LOGE("TAG", "Invalid fill area");
        return;
    }

    /** 计算填充区域的宽度和高度 */
    uint16_t width = ex - sx + 1;
    uint16_t height = ey - sy + 1;
    uint32_t buf_index = 0;

    /** 从内部RAM分配颜色缓冲区 */
    uint16_t *buffer = heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_INTERNAL);

    /** 逐行绘制颜色块 */
    for (uint16_t y_index = 0; y_index < height; y_index++)
    {
        for (uint16_t x_index = 0; x_index < width ; x_index++)
        {
            buffer[x_index] = color[buf_index];
            buf_index++;
        }

        esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, sx, sy + y_index, ex, sy + 1 + y_index, buffer);
    }

    heap_caps_free(buffer);                                    /**< 释放内存 */
}

/**
 * @brief       画线函数(使用Bresenham算法)
 * @param       x1,y1: 起点坐标
 * @param       x2,y2: 终点坐标
 * @param       color: 线条颜色
 * @retval      无
 * @note        使用Bresenham算法绘制任意角度的直线
 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint16_t t;
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, row, col;

    /** 计算坐标增量 */
    delta_x = x2 - x1;
    delta_y = y2 - y1;
    row = x1;
    col = y1;

    /** 确定X方向步进 */
    if (delta_x > 0)
    {
        incx = 1;
    }
    else if (delta_x == 0)
    {
        incx = 0;
    }
    else
    {
        incx = -1;
        delta_x = -delta_x;
    }

    /** 确定Y方向步进 */
    if (delta_y > 0)
    {
        incy = 1;
    }
    else if (delta_y == 0)
    {
        incy = 0;
    }
    else
    {
        incy = -1;
        delta_y = -delta_y;
    }

    /** 选择基本增量坐标轴 */
    if ( delta_x > delta_y)
    {
        distance = delta_x;
    }
    else
    {
        distance = delta_y;
    }

    /** 绘制线条 */
    for (t = 0; t <= distance + 1; t++)
    {
        lcd_draw_point(row, col, color);                       /**< 画点 */
        xerr += delta_x;
        yerr += delta_y;

        if (xerr > distance)
        {
            xerr -= distance;
            row += incx;
        }

        if (yerr > distance)
        {
            yerr -= distance;
            col += incy;
        }
    }
}

/**
 * @brief       画水平线函数
 * @param       x,y:   起点坐标
 * @param       len:   线长度
 * @param       color: 线条颜色
 * @retval      无
 * @note        优化的水平线绘制函数，比通用画线函数效率更高
 */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
    /** 确保坐标在LCD范围内 */
    if (len == 0 || x >= lcddev.width || y >= lcddev.height) return;

    uint16_t ex = fmin(lcddev.width - 1, x + len - 1);
    uint16_t ey = y;

    /** 计算填充区域的宽度和高度 */
    uint32_t width = ex - x + 1;
    uint32_t h = ey - y + 1;

    /** 分配颜色缓冲区 */
    uint16_t *color_buffer = malloc(width * h * sizeof(uint16_t));
    if (color_buffer == NULL) return;

    /** 填充颜色值 */
    for (uint32_t i = 0; i < width * h; i++)
    {
        color_buffer[i] = color;
    }

    /** 绘制水平线 */
    esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, x, y, ex + 1, ey + 1, color_buffer);
    free(color_buffer);
}

/**
 * @brief       画矩形函数
 * @param       x0,y0: 矩形左上角坐标
 * @param       x1,y1: 矩形右下角坐标
 * @param       color: 矩形边框颜色
 * @retval      无
 * @note        使用四条直线绘制矩形边框
 */
void lcd_draw_rectangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,uint16_t color)
{
    lcd_draw_line(x0, y0, x1, y0,color);                       /**< 上边框 */
    lcd_draw_line(x0, y0, x0, y1,color);                       /**< 左边框 */
    lcd_draw_line(x0, y1, x1, y1,color);                       /**< 下边框 */
    lcd_draw_line(x1, y0, x1, y1,color);                       /**< 右边框 */
}

/**
 * @brief       画圆函数(使用Bresenham算法)
 * @param       x0,y0: 圆心坐标
 * @param       r:     半径
 * @param       color: 圆的颜色
 * @retval      无
 * @note        使用Bresenham算法绘制空心圆
 */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color)
{
    int a, b;
    int di;

    a = 0;
    b = r;
    di = 3 - (r << 1);                                       /**< 判断下个点位置的标志 */

    /** 绘制圆的八个对称点 */
    while (a <= b)
    {
        lcd_draw_point(x0 + a, y0 - b, color);               /**< 1 */
        lcd_draw_point(x0 + b, y0 - a, color);               /**< 2 */
        lcd_draw_point(x0 + b, y0 + a, color);               /**< 3 */
        lcd_draw_point(x0 + a, y0 + b, color);               /**< 4 */
        lcd_draw_point(x0 - a, y0 + b, color);               /**< 5 */
        lcd_draw_point(x0 - b, y0 + a, color);              /**< 6 */
        lcd_draw_point(x0 - a, y0 - b, color);              /**< 7 */
        lcd_draw_point(x0 - b, y0 - a, color);              /**< 8 */
        a++;

        /** Bresenham圆算法 */
        if (di < 0)
        {
            di += 4 * a + 6;
        }
        else
        {
            di += 10 + 4 * (a - b);
            b--;
        }
    }
}

/**
 * @brief       画实心圆函数
 * @param       center_x,center_y: 圆心坐标
 * @param       radius: 半径
 * @param       color: 颜色值
 * @retval      无
 * @note        使用水平线填充圆内部
 */
void lcd_fill_circle(uint16_t center_x, uint16_t center_y, uint16_t radius, uint16_t color)
{
    uint32_t i;
    uint32_t imax = ((uint32_t)radius * 707) / 1000 + 1;
    uint32_t sqmax = (uint32_t)radius * (uint32_t)radius + (uint32_t)radius / 2;
    uint32_t xr = radius;

    /** 绘制最顶部水平线 */
    lcd_draw_hline(center_x - radius, center_y, 2 * radius, color);

    /** 逐行绘制圆内部 */
    for (i = 1; i <= imax; i++)
    {
        if ((i * i + xr * xr) > sqmax)
        {
            /** 从外向内绘制 */
            if (xr > imax)
            {
                lcd_draw_hline (center_x - i + 1, center_y + xr, 2 * (i - 1), color);
                lcd_draw_hline (center_x - i + 1, center_y - xr, 2 * (i - 1), color);
            }

            xr--;
        }

        /** 从内向外绘制 */
        lcd_draw_hline(center_x - xr, center_y + i, 2 * xr, color);
        lcd_draw_hline(center_x - xr, center_y - i, 2 * xr, color);
    }
}

/**
 * @brief       显示字符函数
 * @param       x,y:   显示位置
 * @param       chr:   要显示的字符
 * @param       size:  字体大小(12/16/24/32)
 * @param       mode:  0-非叠加模式, 1-叠加模式
 * @param       color: 字体颜色
 * @retval      无
 * @note        根据字体大小选择字模，支持多种字体
 */
void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t temp, t1, t;
    uint16_t y0 = y;
    uint8_t csize = 0;
    uint8_t *pfont = 0;

    /** 计算字体一个字符占用的字节数 */
    csize = (size / 8 + ((size % 8) ? 1 : 0)) * (size / 2);

    /** ASCII字库从空格开始取模，减去空格得到偏移 */
    chr = (char)chr - ' ';

    /** 根据字体大小选择字模 */
    switch (size)
    {
        case 12:
            pfont = (uint8_t *)asc2_1206[(uint8_t)chr];
            break;

        case 16:
            pfont = (uint8_t *)asc2_1608[(uint8_t)chr];
            break;

        case 24:
            pfont = (uint8_t *)asc2_2412[(uint8_t)chr];
            break;

        case 32:
            pfont = (uint8_t *)asc2_3216[(uint8_t)chr];
            break;

        default:
            return ;
    }

    /** 逐字节绘制字符点阵 */
    for (t = 0; t < csize; t++)
    {
        temp = pfont[t];

        /** 逐位绘制像素 */
        for (t1 = 0; t1 < 8; t1++)
        {
            if (temp & 0x80)                                    /**< 有效点 */
            {
                lcd_draw_point(x, y, color);
            }
            else if (mode == 0)                                /**< 无效点且非叠加模式 */
            {
                lcd_draw_point(x, y, g_back_color);
            }

            temp <<= 1;                                        /**< 移位获取下一位 */
            y++;

            if (y >= lcddev.height) return;                    /**< 超出高度 */

            if ((y - y0) == size)                              /**< 换列 */
            {
                y = y0;
                x++;

                if (x >= lcddev.width) return;                 /**< 超出宽度 */

                break;
            }
        }
    }
}

/**
 * @brief       平方函数
 * @param       m: 底数
 * @param       n: 指数
 * @retval      m的n次方
 */
static uint32_t lcd_pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;

    while (n--)
    {
        result *= m;
    }

    return result;
}

/**
 * @brief       显示数字函数
 * @param       x,y:   起始坐标
 * @param       num:   数值(0~2^32)
 * @param       len:   显示位数
 * @param       size:  字体大小
 * @param       color: 数字颜色
 * @retval      无
 * @note        自动处理高位零的显示
 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    /** 逐位处理数字 */
    for (t = 0; t < len; t++)
    {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;          /**< 获取当前位数字 */

        if (enshow == 0 && t < (len - 1))                      /**< 高位零处理 */
        {
            if (temp == 0)
            {
                lcd_show_char(x + (size / 2) * t, y, ' ', size, 0, color);
                continue;
            }
            else
            {
                enshow = 1;
            }
        }

        lcd_show_char(x + (size / 2) * t, y, temp + '0', size, 0, color);
    }
}

/**
 * @brief       扩展显示数字函数
 * @param       x,y:   起始坐标
 * @param       num:   数值(0~2^32)
 * @param       len:   显示位数
 * @param       size:  字体大小
 * @param       mode:  显示模式
 * @param       color: 数字颜色
 * @retval      无
 * @note        mode参数设置是否填充0和是否叠加显示
 */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    /** 逐位处理数字 */
    for (t = 0; t < len; t++)
    {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;

        if (enshow == 0 && t < (len - 1))
        {
            if (temp == 0)
            {
                if (mode & 0x80)                                /**< 高位填充0 */
                {
                    lcd_show_char(x + (size / 2) * t, y, '0', size, mode & 0x01, color);
                }
                else
                {
                    lcd_show_char(x + (size / 2) * t, y, ' ', size, mode & 0x01, color);
                }

                continue;
            }
            else
            {
                enshow = 1;
            }

        }

        lcd_show_char(x + (size / 2) * t, y, temp + '0', size, mode & 0x01, color);
    }
}

/**
 * @brief       显示字符串函数
 * @param       x,y:       起始坐标
 * @param       width:     显示区域宽度
 * @param       height:    显示区域高度
 * @param       size:      字体大小
 * @param       p:         字符串指针
 * @param       color:     字符串颜色
 * @retval      无
 * @note        支持自动换行，超出显示区域自动停止
 */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color)
{
    uint8_t x0 = x;

    width += x;
    height += y;

    /** 逐字符显示 */
    while ((*p <= '~') && (*p >= ' '))
    {
        /** 自动换行 */
        if (x >= width)
        {
            x = x0;
            y += size;
        }

        if (y >= height)
        {
            break;
        }

        lcd_show_char(x, y, *p, size, 0, color);
        x += size / 2;
        p++;
    }
}
