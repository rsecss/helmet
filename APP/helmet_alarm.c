#include "helmet_alarm.h"
#include "mpu6050.h"

#define HELMET_ALARM_BLINK_MS 160U
#define HELMET_ALARM_MIN_ACTIVE_MS 15000U

static rgb_led_color_t helmet_alarm_base_led = RGB_LED_COLOR_OFF;
static uint8_t helmet_alarm_active = 0U;
static uint8_t helmet_alarm_red_on = 0U;
static uint32_t helmet_alarm_last_toggle = 0UL;
static uint32_t helmet_alarm_active_until = 0UL;

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
 * @brief       本地安全报警输出，报警期间用红灯覆盖普通 LED 状态
 * @param       无
 * @retval      无
 */
void helmet_alarm_task(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t alarm_active;

    if (mpu6050_get_alarm_flags() != 0U) {
        helmet_alarm_active_until = now + HELMET_ALARM_MIN_ACTIVE_MS;
    }
    alarm_active = ((int32_t)(helmet_alarm_active_until - now) > 0) ? 1U : 0U;

    if (alarm_active != 0U) {
        helmet_alarm_active = 1U;
        if ((helmet_alarm_red_on == 0U) ||
            ((uint32_t)(now - helmet_alarm_last_toggle) >= HELMET_ALARM_BLINK_MS)) {
            helmet_alarm_last_toggle = now;
            helmet_alarm_red_on = (helmet_alarm_red_on == 0U) ? 1U : 0U;
        }
        if (helmet_alarm_red_on != 0U) {
            rgb_led_set_red();
        } else {
            rgb_led_off();
        }
        return;
    }

    if (helmet_alarm_active != 0U) {
        helmet_alarm_active = 0U;
        helmet_alarm_red_on = 0U;
        helmet_alarm_last_toggle = now;
        helmet_alarm_apply_base_led();
    }
}
