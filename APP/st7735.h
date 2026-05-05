#ifndef ST7735_H
#define ST7735_H

#include "bsp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ST7735_COLOR_BLACK      0x0000U
#define ST7735_COLOR_WHITE      0xFFFFU
#define ST7735_COLOR_RED        0xF800U
#define ST7735_COLOR_GREEN      0x07E0U
#define ST7735_COLOR_BLUE       0x001FU
#define ST7735_COLOR_YELLOW     0xFFE0U
#define ST7735_COLOR_CYAN       0x07FFU
#define ST7735_COLOR_MAGENTA    0xF81FU

uint8_t st7735_init(void);      // 初始化 ST7735 软件 SPI 显示屏
uint8_t st7735_clear(void);     // 清屏为黑色
uint8_t st7735_fill_screen(uint16_t color); // 填充全屏
uint8_t st7735_fill_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint16_t color); // 填充矩形
uint8_t st7735_draw_string(uint8_t x, uint8_t y, const char *text, uint16_t color, uint16_t bg_color); // 绘制 6x12 ASCII 字符串
uint8_t st7735_draw_text(uint8_t x, uint8_t y, const char *text, uint16_t color, uint16_t bg_color); // 绘制 UTF-8 中文与 ASCII 混排文本
uint8_t st7735_is_ready(void);  // 获取显示模块就绪状态

#ifdef __cplusplus
}
#endif

#endif /* ST7735_H */
