/**
 * @file        led.c
 * @brief       LED驱动源文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @note        实现LED灯的初始化配置和基本控制功能
 */

#include "led.h"

/**
 * @brief       初始化LED灯的GPIO配置
 * @param       无
 * @retval      无
 * @note        配置LED0和LED1两个GPIO引脚为输出模式
 *              - 禁用引脚中断功能
 *              - 设置为输入输出通用模式
 *              - 禁用内部上拉和下拉电阻
 *              - 初始化完成后关闭所有LED灯
 */
void led_init(void)
{
    gpio_config_t gpio_init_struct = {0};

    /** 配置GPIO参数 */
    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;         /**< 禁用引脚中断功能 */
    gpio_init_struct.mode = GPIO_MODE_INPUT_OUTPUT;         /**< 设置为通用输入输出模式 */
    gpio_init_struct.pull_up_en = GPIO_PULLUP_DISABLE;      /**< 禁用上拉电阻 */
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;  /**< 禁用下拉电阻 */
	
    /** 配置LED0和LED1的GPIO引脚位掩码 */
	gpio_init_struct.pin_bit_mask = (1ull << LED0_GPIO_PIN) | (1ull << LED1_GPIO_PIN);
    
    /** 应用GPIO配置 */
    ESP_ERROR_CHECK(gpio_config(&gpio_init_struct));

    /** 初始化后关闭所有LED灯(高电平关闭，低电平点亮) */
    LED0(1);     /**< 关闭LED0 */
	LED1(1);     /**< 关闭LED1 */
}