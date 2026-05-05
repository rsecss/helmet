#include "m100pg_bsp.h"
#include "m100pg.h"
#include "rgb_led.h"
#include "helmet_alarm.h"
#include "pwm_motor.h"
#include "dht11.h"
#include "mq2.h"
#include "mpu6050.h"
#include "max30102.h"

m100pg_proto_t g_m100pg_proto;

/* 0..3 档 → PWM 占空比百分比。pwm_motor_set_speed 接受 0..100。 */
static const uint8_t kMotorGearToPercent[4] = { 0U, 33U, 66U, 100U };

static void bsp_led_set(helmet_led_state_t state, void *user)
{
    (void)user;
    switch (state) {
        case HELMET_LED_WHITE: helmet_alarm_set_base_led(RGB_LED_COLOR_WHITE); break;
        case HELMET_LED_RED:   helmet_alarm_set_base_led(RGB_LED_COLOR_RED);   break;
        case HELMET_LED_GREEN: helmet_alarm_set_base_led(RGB_LED_COLOR_GREEN); break;
        case HELMET_LED_OFF:
        default:               helmet_alarm_set_base_led(RGB_LED_COLOR_OFF);   break;
    }
}

static void bsp_motor_set_speed(uint8_t gear, void *user)
{
    (void)user;
    if (gear > 3U) gear = 3U;
    pwm_motor_set_speed(kMotorGearToPercent[gear]);
}

static void bsp_on_unknown(const char *line, uint16_t len, void *user)
{
    (void)user;
    (void)len;
    printf("[4G] unknown: %s\r\n", line);
}

static void bsp_collect_sample(helmet_telemetry_t *out, void *user)
{
    (void)user;

    if (dht11_is_valid() != 0U) {
        out->temp = dht11_get_temperature();
        out->hum  = dht11_get_humidity();
    }

    float ppm = mq2_get_ppm();
    out->mq2 = (ppm > 0.0f) ? (uint32_t)(ppm + 0.5f) : 0UL;

    out->pitch = pitch;
    out->roll  = roll;
    out->yaw   = yaw;
    out->fall = mpu6050_is_fall_alarm();
    out->collision = mpu6050_is_collision_alarm();

    out->hr   = (hr_valid   != 0U) ? heart_rate : 0;
    out->spo2 = (spo2_valid != 0U) ? spo2       : 0;

    if (mpu6050_get_alarm_flags() != 0U) {
        out->led = HELMET_LED_RED;
    }

    /* led / motor 由协议库预填 intent；此处不覆盖。 */
}

static int bsp_send_frame(const char *buf, uint16_t len, void *user)
{
    (void)user;
    return (int)m100pg_send_bytes((const uint8_t *)buf, len);
}

void m100pg_bsp_init(void)
{
    static const m100pg_proto_cb_t cb = {
        .led_set         = bsp_led_set,
        .motor_set_speed = bsp_motor_set_speed,
        .on_unknown      = bsp_on_unknown,
        .collect_sample  = bsp_collect_sample,
        .send_frame      = bsp_send_frame,
        .user            = NULL,
    };
    m100pg_proto_init(&g_m100pg_proto, &cb);
}

void m100pg_bsp_publish_task(void)
{
    (void)m100pg_proto_publish_telemetry(&g_m100pg_proto);
}
