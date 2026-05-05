#ifndef RGB_LED_H
#define RGB_LED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RGB_LED_COLOR_OFF = 0,
    RGB_LED_COLOR_RED,
    RGB_LED_COLOR_GREEN,
    RGB_LED_COLOR_BLUE,
    RGB_LED_COLOR_WHITE
} rgb_led_color_t;

void rgb_led_set_color(rgb_led_color_t color);     // 设置颜色
void rgb_led_set_white(void);                      // 设置白色
void rgb_led_set_red(void);                        // 设置红色
void rgb_led_set_green(void);                      // 设置绿色
void rgb_led_set_enabled(uint8_t enabled);         // 设置开关状态
void rgb_led_off(void);                            // 关闭 LED
rgb_led_color_t rgb_led_get_color(void);           // 获取当前颜色
uint8_t rgb_led_get_enabled(void);                 // 获取开关状态
void rgb_led_init(void);                           // 初始化，默认关闭
void rgb_led_task(void);                           // 手动测试入口

#ifdef __cplusplus
}
#endif

#endif /* RGB_LED_H */
