#include "rgb_led.h"

#define RGB_LED_R_GPIO_PORT    GPIOB
#define RGB_LED_R_PIN          GPIO_PIN_12
#define RGB_LED_G_GPIO_PORT    GPIOB
#define RGB_LED_G_PIN          GPIO_PIN_13
#define RGB_LED_B_GPIO_PORT    GPIOB
#define RGB_LED_B_PIN          GPIO_PIN_14

static rgb_led_color_t rgb_led_current_color = RGB_LED_COLOR_OFF;

/**
 * @brief       按共阴极逻辑写入 RGB 三路 GPIO
 * @param       red 红色通道，1 点亮，0 熄灭
 * @param       green 绿色通道，1 点亮，0 熄灭
 * @param       blue 蓝色通道，1 点亮，0 熄灭
 * @retval      无
 */
static void rgb_led_write(uint8_t red, uint8_t green, uint8_t blue)
{
    /* 共阴极 LED：GPIO 高电平点亮，低电平熄灭。 */
    HAL_GPIO_WritePin(RGB_LED_R_GPIO_PORT, RGB_LED_R_PIN, red ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RGB_LED_G_GPIO_PORT, RGB_LED_G_PIN, green ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RGB_LED_B_GPIO_PORT, RGB_LED_B_PIN, blue ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief       设置三色 LED 颜色
 * @param       color 目标颜色，非法值按关闭处理
 * @retval      无
 */
void rgb_led_set_color(rgb_led_color_t color)
{
    switch (color) {
        case RGB_LED_COLOR_RED:
            rgb_led_write(1U, 0U, 0U);
            rgb_led_current_color = RGB_LED_COLOR_RED;
            break;
        case RGB_LED_COLOR_GREEN:
            rgb_led_write(0U, 1U, 0U);
            rgb_led_current_color = RGB_LED_COLOR_GREEN;
            break;
        case RGB_LED_COLOR_BLUE:
            rgb_led_write(0U, 0U, 1U);
            rgb_led_current_color = RGB_LED_COLOR_BLUE;
            break;
        case RGB_LED_COLOR_WHITE:
            rgb_led_write(1U, 1U, 1U);
            rgb_led_current_color = RGB_LED_COLOR_WHITE;
            break;
        case RGB_LED_COLOR_OFF:
        default:
            rgb_led_off();
            break;
    }
}

/**
 * @brief       设置云端控制用开关状态
 * @param       enabled 1 点亮白色，0 关闭
 * @retval      无
 */
void rgb_led_set_enabled(uint8_t enabled)
{
    if (enabled != 0U) {
        rgb_led_set_color(RGB_LED_COLOR_WHITE);
    } else {
        rgb_led_off();
    }
}

/**
 * @brief       关闭三色 LED
 * @param       无
 * @retval      无
 */
void rgb_led_off(void)
{
    rgb_led_write(0U, 0U, 0U);
    rgb_led_current_color = RGB_LED_COLOR_OFF;
}

/**
 * @brief       获取当前颜色状态
 * @param       无
 * @retval      当前颜色枚举值
 */
rgb_led_color_t rgb_led_get_color(void)
{
    return rgb_led_current_color;
}

/**
 * @brief       获取上传帧使用的开关状态
 * @param       无
 * @retval      1 已点亮，0 已关闭
 */
uint8_t rgb_led_get_enabled(void)
{
    return (rgb_led_current_color == RGB_LED_COLOR_OFF) ? 0U : 1U;
}

/**
 * @brief       初始化三色 LED
 * @param       无
 * @retval      无
 */
void rgb_led_init(void)
{
    rgb_led_off();
}

/**
 * @brief       三色 LED 调度器测试任务
 * @param       无
 * @retval      无
 */
void rgb_led_task(void)
{
    /* 由 scheduler 每 1000ms 调用一次，用白色闪烁验证三路 GPIO。*/
    if (rgb_led_current_color == RGB_LED_COLOR_OFF) {
        rgb_led_set_color(RGB_LED_COLOR_WHITE);
    } else {
        rgb_led_off();
    }
}
