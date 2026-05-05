#include "lcd_app.h"

#define LCD_APP_TEXT_X              4U
#define LCD_APP_LINE_HEIGHT         20U
#define LCD_APP_FONT_HEIGHT         16U
#define LCD_APP_TEXT_WIDTH          120U
#define LCD_APP_LINE_COUNT          6U
#define LCD_APP_LINE_BUFFER_SIZE    32U

static char lcd_app_last_lines[LCD_APP_LINE_COUNT][LCD_APP_LINE_BUFFER_SIZE];
static uint8_t lcd_app_ready;

/**
 * @brief       按行号计算显示 Y 坐标
 * @param       line  行号，从 0 开始
 * @retval      Y 坐标
 */
static uint8_t lcd_app_line_y(uint8_t line)
{
    return (uint8_t)(4U + ((uint16_t)line * LCD_APP_LINE_HEIGHT));
}

/**
 * @brief       清空缓存，让下一次刷新重绘所有行
 * @param       无
 * @retval      无
 */
static void lcd_app_invalidate_cache(void)
{
    uint8_t i;

    for (i = 0U; i < LCD_APP_LINE_COUNT; i++) {
        lcd_app_last_lines[i][0] = '\0';
    }
}

/**
 * @brief       限制烟雾浓度显示范围，避免文本越界
 * @param       ppm  MQ2 浓度值
 * @retval      限幅后的整数 ppm
 */
static uint32_t lcd_app_clamp_mq2(float ppm)
{
    if (ppm <= 0.0f) {
        return 0UL;
    }
    if (ppm > 999999.0f) {
        return 999999UL;
    }
    return (uint32_t)(ppm + 0.5f);
}

/**
 * @brief       重绘一行变化后的文本
 * @param       line  行号，从 0 开始
 * @param       text  以 NUL 结尾的 UTF-8 文本
 * @retval      无
 */
static void lcd_app_draw_line(uint8_t line, const char *text)
{
    uint8_t y;

    if ((line >= LCD_APP_LINE_COUNT) || (text == NULL)) {
        return;
    }
    if (strcmp(lcd_app_last_lines[line], text) == 0) {
        return;
    }

    y = lcd_app_line_y(line);
    (void)st7735_fill_rect(LCD_APP_TEXT_X, y,
                           LCD_APP_TEXT_WIDTH, LCD_APP_FONT_HEIGHT,
                           ST7735_COLOR_BLACK);
    (void)st7735_draw_text(LCD_APP_TEXT_X, y, text,
                           ST7735_COLOR_WHITE, ST7735_COLOR_BLACK);
    (void)strncpy(lcd_app_last_lines[line], text, LCD_APP_LINE_BUFFER_SIZE - 1U);
    lcd_app_last_lines[line][LCD_APP_LINE_BUFFER_SIZE - 1U] = '\0';
}

/**
 * @brief       初始化传感器数据显示页面
 * @param       无
 * @retval      无
 */
void lcd_app_init(void)
{
    lcd_app_invalidate_cache();
    lcd_app_ready = st7735_is_ready();
    if (lcd_app_ready != 0U) {
        (void)st7735_clear();
    }
}

/**
 * @brief       周期刷新 6 行传感器数据显示
 * @param       无
 * @retval      无
 */
void lcd_app_task(void)
{
    char line[LCD_APP_LINE_BUFFER_SIZE];

    if (st7735_is_ready() == 0U) {
        lcd_app_ready = 0U;
        lcd_app_invalidate_cache();
        return;
    }
    if (lcd_app_ready == 0U) {
        lcd_app_ready = 1U;
        (void)st7735_clear();
        lcd_app_invalidate_cache();
    }

    if (dht11_is_valid() != 0U) {
        (void)snprintf(line, sizeof(line), "\xE6\xB8\xA9\xE5\xBA\xA6:%uC", dht11_get_temperature());
    } else {
        (void)snprintf(line, sizeof(line), "\xE6\xB8\xA9\xE5\xBA\xA6:--C");
    }
    lcd_app_draw_line(0U, line);

    if (dht11_is_valid() != 0U) {
        (void)snprintf(line, sizeof(line), "\xE6\xB9\xBF\xE5\xBA\xA6:%u%%", dht11_get_humidity());
    } else {
        (void)snprintf(line, sizeof(line), "\xE6\xB9\xBF\xE5\xBA\xA6:--%%");
    }
    lcd_app_draw_line(1U, line);

    (void)snprintf(line, sizeof(line), "\xE7\x83\x9F\xE9\x9B\xBE:%lu",
                   (unsigned long)lcd_app_clamp_mq2(mq2_get_ppm()));
    lcd_app_draw_line(2U, line);

    if (hr_valid != 0U) {
        (void)snprintf(line, sizeof(line), "\xE5\xBF\x83\xE7\x8E\x87:%ld", (long)heart_rate);
    } else {
        (void)snprintf(line, sizeof(line), "\xE5\xBF\x83\xE7\x8E\x87:--");
    }
    lcd_app_draw_line(3U, line);

    if (spo2_valid != 0U) {
        (void)snprintf(line, sizeof(line), "\xE8\xA1\x80\xE6\xB0\xA7:%ld%%", (long)spo2);
    } else {
        (void)snprintf(line, sizeof(line), "\xE8\xA1\x80\xE6\xB0\xA7:--%%");
    }
    lcd_app_draw_line(4U, line);

    (void)snprintf(line, sizeof(line), "\xE5\xA7\xBF\xE6\x80\x81:%.0f,%.0f,%.0f", pitch, roll, yaw);
    lcd_app_draw_line(5U, line);
}
