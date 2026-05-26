/**
 * @file        lcd.h
 * @brief       LCD显示驱动头文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @attention   Waiken-Smart 慧勤智远
 * @note        提供LCD显示驱动的接口函数，包括图形绘制、文字显示等功能
 *              支持MIPI接口LCD显示屏，使用双缓冲机制避免闪烁
 */

#ifndef __LCD_H
#define __LCD_H

#include "mipi_lcd.h"
#include <math.h>
#include <string.h>

/** 背光和复位IO定义 */
#define LCD_BL_PIN       (GPIO_NUM_23)  /**< LCD背光控制引脚 - GPIO23 */
#define LCD_RST_PIN      (GPIO_NUM_22)  /**< LCD复位控制引脚 - GPIO22 */

/**
 * @brief LCD背光控制宏
 * @param x: 1-打开背光, 0-关闭背光
 */
#define LCD_BL(x)       do { x ?                                \
                             gpio_set_level(LCD_BL_PIN, 1):     \
                             gpio_set_level(LCD_BL_PIN, 0);     \
                        } while(0)

/**
 * @brief LCD复位控制宏
 * @param x: 1-取消复位, 0-执行复位
 */
#define LCD_RST(x)       do { x ?                                \
                             gpio_set_level(LCD_RST_PIN, 1):     \
                             gpio_set_level(LCD_RST_PIN, 0);     \
                        } while(0)

/** 常用颜色值 - RGB565格式 */
#define WHITE           0xFFFF      /**< 白色 */
#define BLACK           0x0000      /**< 黑色 */
#define RED             0xF800      /**< 红色 */
#define GREEN           0x07E0      /**< 绿色 */
#define BLUE            0x001F      /**< 蓝色 */
#define MAGENTA         0XF81F      /**< 洋红色 */
#define YELLOW          0XFFE0      /**< 黄色 */
#define CYAN            0X07FF      /**< 蓝绿色 */

/** 扩展颜色值 */
#define BROWN           0XBC40      /**< 棕色 */
#define BRRED           0XFC07      /**< 棕红色 */
#define GRAY            0X8430      /**< 灰色 */
#define DARKBLUE        0X01CF      /**< 深蓝色 */
#define LIGHTBLUE       0X7D7C      /**< 浅蓝色 */
#define GRAYBLUE        0X5458      /**< 灰蓝色 */
#define LIGHTGREEN      0X841F      /**< 浅绿色 */
#define LGRAY           0XC618      /**< 浅灰色(窗体背景色) */
#define LGRAYBLUE       0XA651      /**< 浅灰蓝色(中间层颜色) */
#define LBBLUE          0X2B12      /**< 浅棕蓝色(选择条目的反色) */

/**
 * @brief LCD重要参数结构体
 * @note 管理LCD显示器的配置信息和控制句柄
 */
typedef struct
{
    uint32_t id;                                /**< 读取的LCD ID */
    uint32_t width;                             /**< 面板宽度(固定参数，不随显示方向改变) */
    uint32_t height;                            /**< 面板高度(固定参数，不随显示方向改变) */
    uint8_t  dir;                               /**< 显示方向: 0-竖屏(MIPI只能竖屏), 1-横屏 */
    uint8_t  color_byte;                        /**< 颜色格式(字节数) */
    esp_lcd_panel_handle_t lcd_panel_handle;    /**< LCD面板驱动句柄 */
    esp_lcd_panel_io_handle_t lcd_dbi_io;       /**< LCD DBI接口句柄 */
    struct
    {
        int lcd_rst;                            /**< 复位GPIO引脚 */
        int lcd_bl;                             /**< 背光GPIO引脚 */
    } ctrl;
} _lcd_dev;

/** 外部变量声明 */
extern _lcd_dev lcddev; /**< LCD设备参数全局变量 */

/* 函数声明 */

/**
 * @brief  LCD初始化函数
 * @retval 无
 * @note   初始化LCD控制器、背光GPIO，配置MIPI接口和双缓冲机制
 */
void lcd_init(void);

/**
 * @brief  清屏函数
 * @param  color: 要填充的颜色值
 * @retval 无
 * @note   使用指定颜色填充整个屏幕
 */
void lcd_clear(uint16_t color);

/**
 * @brief  画点函数
 * @param  x:     X坐标
 * @param  y:     Y坐标
 * @param  color: 点的颜色
 * @retval 无
 */
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief  在指定矩形区域内填充单一颜色
 * @param  sx:    起始X坐标
 * @param  sy:    起始Y坐标
 * @param  ex:    结束X坐标
 * @param  ey:    结束Y坐标
 * @param  color: 要填充的颜色
 * @retval 无
 * @note   填充区域为(sx,sy)到(ex,ey)的矩形
 */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color);

/**
 * @brief  在指定矩形区域内填充颜色块
 * @param  sx:    起始X坐标
 * @param  sy:    起始Y坐标
 * @param  ex:    结束X坐标
 * @param  ey:    结束Y坐标
 * @param  color: 颜色数组指针
 * @retval 无
 * @note   每个像素的颜色由color数组依次提供
 */
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color);

/**
 * @brief  画线函数
 * @param  x1,y1: 起点坐标
 * @param  x2,y2: 终点坐标
 * @param  color:  线条颜色
 * @retval 无
 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);

/**
 * @brief  画水平线函数
 * @param  x:     起点X坐标
 * @param  y:     Y坐标
 * @param  len:   线条长度
 * @param  color: 线条颜色
 * @retval 无
 */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color);

/**
 * @brief  画矩形函数
 * @param  x0,y0: 矩形左上角坐标
 * @param  x1,y1: 矩形右下角坐标
 * @param  color: 矩形边框颜色
 * @retval 无
 */
void lcd_draw_rectangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,uint16_t color);

/**
 * @brief  画圆函数
 * @param  x0,y0: 圆心坐标
 * @param  r:     半径
 * @param  color: 圆的颜色
 * @retval 无
 */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color);

/**
 * @brief  画实心圆函数
 * @param  center_x: 圆心X坐标
 * @param  center_y: 圆心Y坐标
 * @param  radius:   半径
 * @param  color:    圆的颜色
 * @retval 无
 */
void lcd_fill_circle(uint16_t center_x, uint16_t center_y, uint16_t radius, uint16_t color);

/**
 * @brief  显示字符函数
 * @param  x,y:   显示位置坐标
 * @param  chr:   要显示的字符(' '~'~')
 * @param  size:  字体大小(12/16/24/32)
 * @param  mode:  显示模式: 0-非叠加模式, 1-叠加模式
 * @param  color: 字符颜色
 * @retval 无
 */
void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color);

/**
 * @brief  显示数字函数
 * @param  x,y:   显示位置坐标
 * @param  num:   要显示的数字(0~2^32)
 * @param  len:   显示位数
 * @param  size:  字体大小(12/16/24/32)
 * @param  color: 数字颜色
 * @retval 无
 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color);

/**
 * @brief  扩展显示数字函数
 * @param  x,y:   显示位置坐标
 * @param  num:   要显示的数字(0~2^32)
 * @param  len:   显示位数
 * @param  size:  字体大小(12/16/24/32)
 * @param  mode:  显示模式
 * @param  color: 数字颜色
 * @retval 无
 * @note   mode参数可设置是否填充0和是否叠加显示
 */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color);

/**
 * @brief  显示字符串函数
 * @param  x,y:       显示区域起始坐标
 * @param  width:     显示区域宽度
 * @param  height:    显示区域高度
 * @param  size:      字体大小(12/16/24/32)
 * @param  p:         字符串指针
 * @param  color:     字符串颜色
 * @retval 无
 * @note   字符串超出显示区域宽度会自动换行
 */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color);

#endif
