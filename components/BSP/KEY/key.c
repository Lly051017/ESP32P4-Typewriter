/**
 * @file        key.c
 * @brief       按键驱动源文件
 * @author      ESP32写字机项目
 * @date        2026
 * @version     V1.0
 * @note        实现按键的初始化配置和扫描功能，支持软件消抖处理
 */

#include "key.h"

/**
 * @brief       初始化按键GPIO配置
 * @param       无
 * @retval      无
 * @note        配置BOOT和KEY0两个GPIO引脚为输入模式
 *              - 禁用引脚中断功能
 *              - 设置为纯输入模式
 *              - 启用内部上拉电阻(按键未按下时为高电平)
 *              - 禁用内部下拉电阻
 */
void key_init(void)
{
	 gpio_config_t gpio_init_struct = {0};

	 /** 配置BOOT和KEY0两个按键引脚的GPIO参数 */
	 gpio_init_struct.intr_type = GPIO_INTR_DISABLE;         /**< 禁用引脚中断功能 */
	 gpio_init_struct.mode = GPIO_MODE_INPUT;                /**< 设置为纯输入模式 */
	 gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;       /**< 启用上拉电阻(按键默认高电平) */
	 gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;  /**< 禁用下拉电阻 */

	 /** 配置BOOT和KEY0两个GPIO引脚的位掩码 */
	 gpio_init_struct.pin_bit_mask = (1ULL << BOOT_GPIO_PIN) | (1ULL << KEY0_GPIO_PIN);

	 /** 应用GPIO配置 */
	 ESP_ERROR_CHECK(gpio_config(&gpio_init_struct));
}

/**
 * @brief       按键扫描函数
 * @note        按键优先级: BOOT > KEY0
 * @param       mode: 按键扫描模式
 *   @arg       0:  不支持连续按(单次模式)
 *              当按键按下不放时，只有第一次调用会返回键值，
 *              必须松开以后，再次按下才会返回其他键值
 *   @arg       1:  支持连续按(连按模式)
 *              当按键按下不放时，每次调用该函数都会返回键值
 * @retval      返回扫描到的键值:
 *              BOOT_PRES (1) - BOOT按键按下
 *              KEY0_PRES (2) - KEY0按键按下
 *              0          - 无按键按下
 * @note        采用软件消抖处理，检测到按键后延时10ms再次确认
 */
uint8_t key_scan(uint8_t mode)
{
	 static uint8_t key_up = 1;      /**< 按键松开标志，static保持状态 */
	 uint8_t keyval = 0;

	 if (mode) key_up = 1;           /**< mode=1时支持连按，重置松开标志 */

	 /** 检测按键是否按下(电平为0表示按下) */
	 if (key_up && (KEY0 == 0 || BOOT == 0))
    {
        vTaskDelay(pdMS_TO_TICKS(10));           /**< 延时10ms进行软件消抖 */
        key_up = 0;                              /**< 标记按键已按下 */

        if (KEY0 == 0)  keyval = KEY0_PRES;     /**< KEY0按键确认按下 */

        if (BOOT == 0) keyval = BOOT_PRES;      /**< BOOT按键确认按下 */
    }
    else if (KEY0 == 1 && BOOT == 1)            /**< 所有按键都松开 */
    {
        key_up = 1;                              /**< 重置按键松开标志 */
    }

    return keyval;                              /**< 返回扫描到的键值 */
}
