#include "m100pg_bsp.h"
#include "m100pg.h"
#include "fan_control.h"
#include "rgb_led.h"
#include "helmet_alarm.h"
#include "dht11.h"
#include "mq2.h"
#include "mpu6050.h"
#include "max30102.h"

m100pg_proto_t g_m100pg_proto;

static const rgb_led_color_t kProtoLedToRgb[] = {
    RGB_LED_COLOR_OFF,
    RGB_LED_COLOR_WHITE,
    RGB_LED_COLOR_RED,
    RGB_LED_COLOR_GREEN,
    RGB_LED_COLOR_YELLOW
};

static void bsp_led_set(helmet_led_state_t state, void *user)
{
    uint8_t index = (uint8_t)state;

    (void)user;
    if (index >= (uint8_t)(sizeof(kProtoLedToRgb) / sizeof(kProtoLedToRgb[0]))) {
        index = (uint8_t)HELMET_LED_OFF;
    }
    helmet_alarm_set_base_led(kProtoLedToRgb[index]);
}

static void bsp_motor_set_speed(uint8_t gear, void *user)
{
    (void)user;
    fan_control_set_manual_gear(gear);
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

    float mq2_index = mq2_get_trend_index();
    out->mq2 = (mq2_index > 0.0f) ? (uint32_t)(mq2_index + 0.5f) : 0UL;
    out->mq2_alarm = mq2_is_trend_alarm();

    out->pitch = pitch;
    out->roll  = roll;
    out->yaw   = yaw;
    out->fall = mpu6050_is_fall_alarm();
    out->collision = mpu6050_is_collision_alarm();

    out->hr   = (hr_valid   != 0U) ? heart_rate : 0;
    out->spo2 = (spo2_valid != 0U) ? spo2       : 0;
    out->motor = fan_control_get_output_gear();
    out->fan_auto = fan_control_is_auto_active();
    out->temp_limit = fan_control_get_temp_on_threshold();
    out->temp_recover = fan_control_get_temp_off_threshold();

    if (mpu6050_get_alarm_flags() != 0U) {
        out->led = HELMET_LED_RED;
    } else if (out->mq2_alarm != 0U) {
        out->led = HELMET_LED_YELLOW;
    }

    /* led 由协议库预填 intent；报警时覆盖为本地状态。 */
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
