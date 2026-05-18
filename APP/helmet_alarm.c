#include "helmet_alarm.h"
#include "mpu6050.h"
#include "mq2.h"

#define HELMET_ALARM_BLINK_MS 160U
#define HELMET_ALARM_MIN_ACTIVE_MS 15000U
#define HELMET_ALARM_MQ2_MIN_ACTIVE_MS 5000U

typedef struct {
    uint8_t (*is_active)(void);
    uint32_t hold_ms;
    rgb_led_color_t color;
    uint8_t blink_on;
    uint32_t active_until;
} helmet_alarm_source_t;

static rgb_led_color_t helmet_alarm_base_led = RGB_LED_COLOR_OFF;
static uint8_t helmet_alarm_active = 0U;
static uint32_t helmet_alarm_last_toggle = 0UL;

static uint8_t helmet_alarm_mpu_active(void)
{
    return (mpu6050_get_alarm_flags() != 0U) ? 1U : 0U;
}

static helmet_alarm_source_t helmet_alarm_sources[] = {
    {helmet_alarm_mpu_active, HELMET_ALARM_MIN_ACTIVE_MS,     RGB_LED_COLOR_RED,    0U, 0UL},
    {mq2_is_trend_alarm,      HELMET_ALARM_MQ2_MIN_ACTIVE_MS, RGB_LED_COLOR_YELLOW, 0U, 0UL}
};

/**
 * @brief       应用非报警状态下的 LED 颜色
 * @param       无
 * @retval      无
 */
static void helmet_alarm_apply_base_led(void)
{
    rgb_led_set_color(helmet_alarm_base_led);
}

/**
 * @brief       查找当前最高优先级报警源
 * @param       now 当前系统 tick
 * @retval      当前报警源指针，无报警返回 NULL
 */
static helmet_alarm_source_t *helmet_alarm_select_source(uint32_t now)
{
    uint8_t i;
    helmet_alarm_source_t *source;

    for (i = 0U; i < (uint8_t)(sizeof(helmet_alarm_sources) / sizeof(helmet_alarm_sources[0])); i++) {
        source = &helmet_alarm_sources[i];

        if (source->is_active() != 0U) {
            source->active_until = now + source->hold_ms;
        }
        if ((int32_t)(source->active_until - now) > 0) {
            return source;
        }
    }

    return NULL;
}

/**
 * @brief       输出当前报警源的快闪颜色
 * @param       source 当前报警源
 * @param       now    当前系统 tick
 * @retval      无
 */
static void helmet_alarm_apply_source(helmet_alarm_source_t *source, uint32_t now)
{
    helmet_alarm_active = 1U;
    if ((source->blink_on == 0U) ||
        ((uint32_t)(now - helmet_alarm_last_toggle) >= HELMET_ALARM_BLINK_MS)) {
        helmet_alarm_last_toggle = now;
        source->blink_on = (source->blink_on == 0U) ? 1U : 0U;
    }
    if (source->blink_on != 0U) {
        rgb_led_set_color(source->color);
    } else {
        rgb_led_off();
    }
}

/**
 * @brief       清除报警输出状态并恢复基础颜色
 * @param       now 当前系统 tick
 * @retval      无
 */
static void helmet_alarm_restore_base(uint32_t now)
{
    uint8_t i;

    if (helmet_alarm_active == 0U) {
        return;
    }
    helmet_alarm_active = 0U;
    for (i = 0U; i < (uint8_t)(sizeof(helmet_alarm_sources) / sizeof(helmet_alarm_sources[0])); i++) {
        helmet_alarm_sources[i].blink_on = 0U;
    }
    helmet_alarm_last_toggle = now;
    helmet_alarm_apply_base_led();
}

/**
 * @brief       设置非报警状态下的 LED 颜色
 * @param       color 目标颜色
 * @retval      无
 */
void helmet_alarm_set_base_led(rgb_led_color_t color)
{
    helmet_alarm_base_led = color;
    if (helmet_alarm_active == 0U) {
        helmet_alarm_apply_base_led();
    }
}

/**
 * @brief       本地安全报警输出，按报警源优先级快闪并覆盖普通 LED 状态
 * @param       无
 * @retval      无
 */
void helmet_alarm_task(void)
{
    uint32_t now = HAL_GetTick();
    helmet_alarm_source_t *source = helmet_alarm_select_source(now);

    if (source != NULL) {
        helmet_alarm_apply_source(source, now);
        return;
    }
    helmet_alarm_restore_base(now);
}
