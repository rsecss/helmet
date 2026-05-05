#include "st7735.h"
#include "lcd_font_lib.h"

#define ST7735_DC_GPIO_PORT         GPIOB
#define ST7735_DC_PIN               GPIO_PIN_1
#define ST7735_SCL_GPIO_PORT        GPIOB
#define ST7735_SCL_PIN              GPIO_PIN_0
#define ST7735_SDA_GPIO_PORT        GPIOA
#define ST7735_SDA_PIN              GPIO_PIN_7

#define ST7735_WIDTH                128U
#define ST7735_HEIGHT               128U
#define ST7735_X_OFFSET             2U
#define ST7735_Y_OFFSET             3U
#define ST7735_MADCTL_VALUE         0xC0U
#define ST7735_FONT_WIDTH           6U
#define ST7735_FONT_HEIGHT          12U
#define ST7735_CN12_WIDTH           12U
#define ST7735_CN12_HEIGHT          12U
#define ST7735_CN12_COUNT           ((uint8_t)(sizeof(tfont_cn12) / sizeof(tfont_cn12[0])))

#define ST7735_CMD_SWRESET          0x01U
#define ST7735_CMD_SLPOUT           0x11U
#define ST7735_CMD_DISPON           0x29U
#define ST7735_CMD_CASET            0x2AU
#define ST7735_CMD_RASET            0x2BU
#define ST7735_CMD_RAMWR            0x2CU
#define ST7735_CMD_MADCTL           0x36U
#define ST7735_CMD_COLMOD           0x3AU
#define ST7735_CMD_FRMCTR1          0xB1U
#define ST7735_CMD_FRMCTR2          0xB2U
#define ST7735_CMD_FRMCTR3          0xB3U
#define ST7735_CMD_INVCTR           0xB4U
#define ST7735_CMD_PWCTR1           0xC0U
#define ST7735_CMD_PWCTR2           0xC1U
#define ST7735_CMD_PWCTR3           0xC2U
#define ST7735_CMD_PWCTR4           0xC3U
#define ST7735_CMD_PWCTR5           0xC4U
#define ST7735_CMD_VMCTR1           0xC5U
#define ST7735_CMD_GMCTRP1          0xE0U
#define ST7735_CMD_GMCTRN1          0xE1U

static uint8_t st7735_ready;

/**
 * @brief       判断 UTF-8 字符首字节对应的字符长度
 * @param       lead  UTF-8 首字节
 * @retval      字符字节长度，不支持时返回 0
 */
static uint8_t st7735_utf8_char_len(unsigned char lead)
{
    if (lead < 0x80U) {
        return 1U;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        return 2U;
    }
    if ((lead & 0xF0U) == 0xE0U) {
        return 3U;
    }
    if ((lead & 0xF8U) == 0xF0U) {
        return 4U;
    }
    return 0U;
}

/**
 * @brief       查找项目 12x12 中文字模
 * @param       text      UTF-8 字符起始地址
 * @param       char_len  UTF-8 字符字节长度
 * @retval      字模指针，未找到时返回 NULL
 */
static const typFNT_CN12 *st7735_find_cn12_glyph(const char *text, uint8_t char_len)
{
    uint8_t i;

    if ((text == NULL) || (char_len == 0U) || (char_len >= sizeof(tfont_cn12[0].Index))) {
        return NULL;
    }

    for (i = 0U; i < ST7735_CN12_COUNT; i++) {
        if ((memcmp(tfont_cn12[i].Index, text, char_len) == 0) &&
            (tfont_cn12[i].Index[char_len] == 0U)) {
            return &tfont_cn12[i];
        }
    }

    return NULL;
}

/**
 * @brief       配置 ST7735 软件 SPI 所需 GPIO
 * @param       无
 * @retval      无
 */
static void st7735_gpio_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio_init.Pin = ST7735_SDA_PIN;
    HAL_GPIO_Init(ST7735_SDA_GPIO_PORT, &gpio_init);

    gpio_init.Pin = ST7735_SCL_PIN | ST7735_DC_PIN;
    HAL_GPIO_Init(ST7735_SCL_GPIO_PORT, &gpio_init);

    HAL_GPIO_WritePin(ST7735_SCL_GPIO_PORT, ST7735_SCL_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ST7735_SDA_GPIO_PORT, ST7735_SDA_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ST7735_DC_GPIO_PORT, ST7735_DC_PIN, GPIO_PIN_SET);
}

/**
 * @brief       通过 GPIO 软件 SPI 写入一个字节
 * @param       data  待写入字节
 * @retval      无
 */
static void st7735_write_byte(uint8_t data)
{
    uint8_t mask;

    for (mask = 0x80U; mask != 0U; mask >>= 1U) {
        HAL_GPIO_WritePin(ST7735_SCL_GPIO_PORT, ST7735_SCL_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(ST7735_SDA_GPIO_PORT, ST7735_SDA_PIN,
                          ((data & mask) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(ST7735_SCL_GPIO_PORT, ST7735_SCL_PIN, GPIO_PIN_SET);
    }
}

/**
 * @brief       写入 ST7735 命令字节
 * @param       command  ST7735 命令
 * @retval      无
 */
static void st7735_write_command(uint8_t command)
{
    HAL_GPIO_WritePin(ST7735_DC_GPIO_PORT, ST7735_DC_PIN, GPIO_PIN_RESET);
    st7735_write_byte(command);
}

/**
 * @brief       写入 ST7735 数据字节
 * @param       data  ST7735 数据
 * @retval      无
 */
static void st7735_write_data(uint8_t data)
{
    HAL_GPIO_WritePin(ST7735_DC_GPIO_PORT, ST7735_DC_PIN, GPIO_PIN_SET);
    st7735_write_byte(data);
}

/**
 * @brief       写入 RGB565 像素数据
 * @param       color  RGB565 颜色值
 * @retval      无
 */
static void st7735_write_color(uint16_t color)
{
    st7735_write_data((uint8_t)(color >> 8U));
    st7735_write_data((uint8_t)(color & 0xFFU));
}

/**
 * @brief       写入一条带参数的 ST7735 命令
 * @param       command  ST7735 命令
 * @param       data     参数数组
 * @param       length   参数长度
 * @retval      无
 */
static void st7735_write_command_data(uint8_t command, const uint8_t *data, uint8_t length)
{
    uint8_t i;

    st7735_write_command(command);
    for (i = 0U; i < length; i++) {
        st7735_write_data(data[i]);
    }
}

/**
 * @brief       设置 ST7735 写入窗口
 * @param       x0  起始 X 坐标
 * @param       y0  起始 Y 坐标
 * @param       x1  结束 X 坐标
 * @param       y1  结束 Y 坐标
 * @retval      无
 */
static void st7735_set_address_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    uint16_t sx0 = (uint16_t)x0 + ST7735_X_OFFSET;
    uint16_t sx1 = (uint16_t)x1 + ST7735_X_OFFSET;
    uint16_t sy0 = (uint16_t)y0 + ST7735_Y_OFFSET;
    uint16_t sy1 = (uint16_t)y1 + ST7735_Y_OFFSET;

    st7735_write_command(ST7735_CMD_CASET);
    st7735_write_data((uint8_t)(sx0 >> 8U));
    st7735_write_data((uint8_t)(sx0 & 0xFFU));
    st7735_write_data((uint8_t)(sx1 >> 8U));
    st7735_write_data((uint8_t)(sx1 & 0xFFU));

    st7735_write_command(ST7735_CMD_RASET);
    st7735_write_data((uint8_t)(sy0 >> 8U));
    st7735_write_data((uint8_t)(sy0 & 0xFFU));
    st7735_write_data((uint8_t)(sy1 >> 8U));
    st7735_write_data((uint8_t)(sy1 & 0xFFU));

    st7735_write_command(ST7735_CMD_RAMWR);
}

/**
 * @brief       执行常见 128x128 ST7735 初始化序列
 * @param       无
 * @retval      无
 */
static void st7735_write_init_sequence(void)
{
    const uint8_t frmctr1[] = {0x01U, 0x2CU, 0x2DU};
    const uint8_t frmctr3[] = {0x01U, 0x2CU, 0x2DU, 0x01U, 0x2CU, 0x2DU};
    const uint8_t invctr[] = {0x07U};
    const uint8_t pwctr1[] = {0xA2U, 0x02U, 0x84U};
    const uint8_t pwctr2[] = {0xC5U};
    const uint8_t pwctr3[] = {0x0AU, 0x00U};
    const uint8_t pwctr4[] = {0x8AU, 0x2AU};
    const uint8_t pwctr5[] = {0x8AU, 0xEEU};
    const uint8_t vmctr1[] = {0x0EU};
    const uint8_t madctl[] = {ST7735_MADCTL_VALUE};
    const uint8_t colmod[] = {0x05U};
    const uint8_t gmctrp1[] = {
        0x02U, 0x1CU, 0x07U, 0x12U, 0x37U, 0x32U, 0x29U, 0x2DU,
        0x29U, 0x25U, 0x2BU, 0x39U, 0x00U, 0x01U, 0x03U, 0x10U
    };
    const uint8_t gmctrn1[] = {
        0x03U, 0x1DU, 0x07U, 0x06U, 0x2EU, 0x2CU, 0x29U, 0x2DU,
        0x2EU, 0x2EU, 0x37U, 0x3FU, 0x00U, 0x00U, 0x02U, 0x10U
    };

    st7735_write_command(ST7735_CMD_SWRESET);
    HAL_Delay(150U);
    st7735_write_command(ST7735_CMD_SLPOUT);
    HAL_Delay(120U);

    st7735_write_command_data(ST7735_CMD_FRMCTR1, frmctr1, sizeof(frmctr1));
    st7735_write_command_data(ST7735_CMD_FRMCTR2, frmctr1, sizeof(frmctr1));
    st7735_write_command_data(ST7735_CMD_FRMCTR3, frmctr3, sizeof(frmctr3));
    st7735_write_command_data(ST7735_CMD_INVCTR, invctr, sizeof(invctr));
    st7735_write_command_data(ST7735_CMD_PWCTR1, pwctr1, sizeof(pwctr1));
    st7735_write_command_data(ST7735_CMD_PWCTR2, pwctr2, sizeof(pwctr2));
    st7735_write_command_data(ST7735_CMD_PWCTR3, pwctr3, sizeof(pwctr3));
    st7735_write_command_data(ST7735_CMD_PWCTR4, pwctr4, sizeof(pwctr4));
    st7735_write_command_data(ST7735_CMD_PWCTR5, pwctr5, sizeof(pwctr5));
    st7735_write_command_data(ST7735_CMD_VMCTR1, vmctr1, sizeof(vmctr1));
    st7735_write_command_data(ST7735_CMD_MADCTL, madctl, sizeof(madctl));
    st7735_write_command_data(ST7735_CMD_COLMOD, colmod, sizeof(colmod));
    st7735_write_command_data(ST7735_CMD_GMCTRP1, gmctrp1, sizeof(gmctrp1));
    st7735_write_command_data(ST7735_CMD_GMCTRN1, gmctrn1, sizeof(gmctrn1));

    st7735_write_command(ST7735_CMD_DISPON);
    HAL_Delay(100U);
}

/**
 * @brief       初始化 ST7735 并显示启动自检画面
 * @param       无
 * @retval      0 成功，非 0 表示内部参数失败
 */
uint8_t st7735_init(void)
{
    if ((ST7735_WIDTH != 128U) || (ST7735_HEIGHT != 128U)) {
        st7735_ready = 0U;
        return 1U;
    }

    st7735_ready = 0U;
    st7735_gpio_init();
    st7735_write_init_sequence();
    st7735_ready = 1U;

    (void)st7735_fill_screen(ST7735_COLOR_BLACK);
    (void)st7735_fill_rect(4U, 4U, 24U, 16U, ST7735_COLOR_RED);
    (void)st7735_fill_rect(32U, 4U, 24U, 16U, ST7735_COLOR_GREEN);
    (void)st7735_fill_rect(60U, 4U, 24U, 16U, ST7735_COLOR_BLUE);
    (void)st7735_fill_rect(88U, 4U, 24U, 16U, ST7735_COLOR_WHITE);
    (void)st7735_draw_string(4U, 32U, "ST7735 OK", ST7735_COLOR_WHITE, ST7735_COLOR_BLACK);
    HAL_Delay(500U);
    (void)st7735_fill_screen(ST7735_COLOR_BLACK);

    return 0U;
}

/**
 * @brief       清屏为黑色
 * @param       无
 * @retval      0 成功，非 0 表示显示未就绪
 */
uint8_t st7735_clear(void)
{
    return st7735_fill_screen(ST7735_COLOR_BLACK);
}

/**
 * @brief       使用指定颜色填充全屏
 * @param       color  RGB565 颜色
 * @retval      0 成功，非 0 表示显示未就绪
 */
uint8_t st7735_fill_screen(uint16_t color)
{
    return st7735_fill_rect(0U, 0U, ST7735_WIDTH, ST7735_HEIGHT, color);
}

/**
 * @brief       使用指定颜色填充矩形区域
 * @param       x       起始 X 坐标
 * @param       y       起始 Y 坐标
 * @param       width   矩形宽度
 * @param       height  矩形高度
 * @param       color   RGB565 颜色
 * @retval      0 成功，非 0 表示参数错误或显示未就绪
 */
uint8_t st7735_fill_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint16_t color)
{
    uint16_t pixel_count;
    uint16_t i;
    uint8_t x1;
    uint8_t y1;

    if ((st7735_ready == 0U) || (width == 0U) || (height == 0U) ||
        (x >= ST7735_WIDTH) || (y >= ST7735_HEIGHT)) {
        return 1U;
    }

    if (((uint16_t)x + width) > ST7735_WIDTH) {
        width = (uint8_t)(ST7735_WIDTH - x);
    }
    if (((uint16_t)y + height) > ST7735_HEIGHT) {
        height = (uint8_t)(ST7735_HEIGHT - y);
    }

    x1 = (uint8_t)(x + width - 1U);
    y1 = (uint8_t)(y + height - 1U);
    pixel_count = (uint16_t)width * (uint16_t)height;

    st7735_set_address_window(x, y, x1, y1);
    for (i = 0U; i < pixel_count; i++) {
        st7735_write_color(color);
    }

    return 0U;
}

/**
 * @brief       绘制 6x12 ASCII 字符串
 * @param       x         起始 X 坐标
 * @param       y         起始 Y 坐标
 * @param       text      以 NUL 结尾的 ASCII 字符串
 * @param       color     前景色
 * @param       bg_color  背景色
 * @retval      0 成功，非 0 表示参数错误或显示未就绪
 */
uint8_t st7735_draw_string(uint8_t x, uint8_t y, const char *text, uint16_t color, uint16_t bg_color)
{
    uint8_t cursor_x = x;
    uint8_t row;
    uint8_t col;
    uint8_t glyph_row;
    unsigned char ch;

    if ((st7735_ready == 0U) || (text == NULL) ||
        (x >= ST7735_WIDTH) || (y >= ST7735_HEIGHT) ||
        (((uint16_t)y + ST7735_FONT_HEIGHT) > ST7735_HEIGHT)) {
        return 1U;
    }

    while (*text != '\0') {
        if (((uint16_t)cursor_x + ST7735_FONT_WIDTH) > ST7735_WIDTH) {
            break;
        }

        ch = (unsigned char)*text;
        if ((ch < 32U) || (ch > 126U)) {
            ch = (unsigned char)'?';
        }

        st7735_set_address_window(cursor_x, y,
                                  (uint8_t)(cursor_x + ST7735_FONT_WIDTH - 1U),
                                  (uint8_t)(y + ST7735_FONT_HEIGHT - 1U));
        for (row = 0U; row < ST7735_FONT_HEIGHT; row++) {
            glyph_row = asc2_1206[ch - 32U][row];
            for (col = 0U; col < ST7735_FONT_WIDTH; col++) {
                if ((glyph_row & (uint8_t)(0x01U << col)) != 0U) {
                    st7735_write_color(color);
                } else {
                    st7735_write_color(bg_color);
                }
            }
        }

        cursor_x = (uint8_t)(cursor_x + ST7735_FONT_WIDTH);
        text++;
    }

    return 0U;
}

/**
 * @brief       绘制 12x12 中文字模
 * @param       x         起始 X 坐标
 * @param       y         起始 Y 坐标
 * @param       glyph     字模指针
 * @param       color     前景色
 * @param       bg_color  背景色
 * @retval      0 成功，非 0 表示参数错误或显示未就绪
 */
static uint8_t st7735_draw_cn12(uint8_t x, uint8_t y, const typFNT_CN12 *glyph,
                                uint16_t color, uint16_t bg_color)
{
    uint8_t row;
    uint8_t col;
    uint8_t glyph_row;

    if ((st7735_ready == 0U) || (glyph == NULL) ||
        (((uint16_t)x + ST7735_CN12_WIDTH) > ST7735_WIDTH) ||
        (((uint16_t)y + ST7735_CN12_HEIGHT) > ST7735_HEIGHT)) {
        return 1U;
    }

    st7735_set_address_window(x, y,
                              (uint8_t)(x + ST7735_CN12_WIDTH - 1U),
                              (uint8_t)(y + ST7735_CN12_HEIGHT - 1U));

    for (row = 0U; row < ST7735_CN12_HEIGHT; row++) {
        glyph_row = glyph->Msk[(uint8_t)(row * 2U)];
        for (col = 0U; col < 8U; col++) {
            if ((glyph_row & (uint8_t)(0x80U >> col)) != 0U) {
                st7735_write_color(color);
            } else {
                st7735_write_color(bg_color);
            }
        }

        glyph_row = glyph->Msk[(uint8_t)((row * 2U) + 1U)];
        for (col = 0U; col < 4U; col++) {
            if ((glyph_row & (uint8_t)(0x80U >> col)) != 0U) {
                st7735_write_color(color);
            } else {
                st7735_write_color(bg_color);
            }
        }
    }

    return 0U;
}

/**
 * @brief       绘制 UTF-8 中文与 6x12 ASCII 混排文本
 * @param       x         起始 X 坐标
 * @param       y         起始 Y 坐标
 * @param       text      以 NUL 结尾的 UTF-8 文本
 * @param       color     前景色
 * @param       bg_color  背景色
 * @retval      0 成功，非 0 表示参数错误或显示未就绪
 */
uint8_t st7735_draw_text(uint8_t x, uint8_t y, const char *text, uint16_t color, uint16_t bg_color)
{
    uint8_t cursor_x = x;
    uint8_t char_len;
    char ascii_text[2];
    const typFNT_CN12 *glyph;

    if ((st7735_ready == 0U) || (text == NULL) ||
        (x >= ST7735_WIDTH) || (y >= ST7735_HEIGHT)) {
        return 1U;
    }

    while (*text != '\0') {
        char_len = st7735_utf8_char_len((unsigned char)*text);
        if (char_len == 0U) {
            if (((uint16_t)cursor_x + ST7735_FONT_WIDTH) > ST7735_WIDTH) {
                break;
            }
            (void)st7735_draw_string(cursor_x, y, "?", color, bg_color);
            cursor_x = (uint8_t)(cursor_x + ST7735_FONT_WIDTH);
            text++;
        } else if (char_len == 1U) {
            if (((uint16_t)cursor_x + ST7735_FONT_WIDTH) > ST7735_WIDTH) {
                break;
            }
            ascii_text[0] = *text;
            ascii_text[1] = '\0';
            (void)st7735_draw_string(cursor_x, y, ascii_text, color, bg_color);
            cursor_x = (uint8_t)(cursor_x + ST7735_FONT_WIDTH);
            text++;
        } else {
            glyph = st7735_find_cn12_glyph(text, char_len);
            if (((uint16_t)cursor_x + ST7735_CN12_WIDTH) > ST7735_WIDTH) {
                break;
            }
            if (glyph != NULL) {
                (void)st7735_draw_cn12(cursor_x, y, glyph, color, bg_color);
                cursor_x = (uint8_t)(cursor_x + ST7735_CN12_WIDTH);
            } else {
                (void)st7735_draw_string(cursor_x, y, "?", color, bg_color);
                cursor_x = (uint8_t)(cursor_x + ST7735_FONT_WIDTH);
            }
            text += char_len;
        }
    }

    return 0U;
}

/**
 * @brief       获取 ST7735 显示模块就绪状态
 * @param       无
 * @retval      1 已就绪，0 未就绪
 */
uint8_t st7735_is_ready(void)
{
    return st7735_ready;
}
