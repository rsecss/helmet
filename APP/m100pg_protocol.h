#ifndef M100PG_PROTOCOL_H
#define M100PG_PROTOCOL_H

/*
 * Helmet Console application-layer protocol library
 *
 * Wire format (matches docs/architecture.md §4 and docs/interface.md):
 *   - Each frame is a UTF-8 text line terminated by '\n'.
 *   - Uplink (device -> browser): a single comma-separated key=value line:
 *
 *       temp=23,hum=60,mq2=120,mq2_alarm=0,pitch=1.2,roll=-0.5,yaw=180.0,
 *       fall=0,collision=0,hr=72,spo2=98,led=white,motor=2\n
 *
 *   - Downlink dictionary (browser -> device):
 *       led_on              -> led_set(WHITE)
 *       led_off             -> led_set(OFF)
 *       led_color_white     -> led_set(WHITE)
 *       led_color_red       -> led_set(RED)
 *       led_color_green     -> led_set(GREEN)
 *       motor_speed_<0..3>  -> motor_set_speed(N)
 *       ping / pong         -> silently ignored (host<->server heartbeat)
 *       anything else       -> on_unknown(line)
 *
 * Scope: this library handles the application payload only. WebSocket
 * frame encoding (RFC 6455) is the 4G module's responsibility; bytes
 * leaving send_frame() go straight to USART2.
 *
 * Memory: no malloc. The struct is caller-owned (declare it once as a
 * static / extern global).
 *
 * Threading: single-threaded. Feed RX bytes from your scheduler task,
 * not from the UART idle ISR (push them through a ring buffer first,
 * which APP/m100pg.c already does).
 *
 * Float printing: snprintf("%.1f") relies on libc printf-float. On
 * newlib-nano enable it via linker flag: -u _printf_float
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Tunables (override via -D if needed) -------------------- */

#ifndef M100PG_PROTO_RX_BUF
#define M100PG_PROTO_RX_BUF 256u   /* longest accepted downlink line */
#endif

#ifndef M100PG_PROTO_TX_BUF
#define M100PG_PROTO_TX_BUF 224u   /* longest produced uplink line   */
#endif

/* ---------- LED state (mirrors a subset of rgb_led_color_t) --------- */

typedef enum {
    HELMET_LED_OFF = 0,
    HELMET_LED_WHITE,
    HELMET_LED_RED,
    HELMET_LED_GREEN,
    HELMET_LED_YELLOW
} helmet_led_state_t;

/* ---------- Telemetry snapshot --------------------------------------
 *
 * Field types align with the existing driver signatures:
 *   temp/hum  -> dht11_get_temperature/humidity (uint8_t)
 *   mq2       -> rounded mq2_get_trend_index()  (uint32_t)
 *   mq2_alarm -> mq2_is_trend_alarm()           (uint8_t)
 *   pitch...  -> mpu6050 dmp globals            (float)
 *   fall/collision -> mpu6050 alarm getters      (uint8_t)
 *   hr/spo2   -> max30102 globals               (int32_t)
 *
 * led/motor are *mirror* fields. The library populates them from the
 * last successfully dispatched downlink command (intent-track); the
 * collect_sample callback can overwrite them if the caller prefers
 * actual hardware readback.
 */
typedef struct {
    /* env */
    uint8_t  temp;
    uint8_t  hum;
    uint32_t mq2;
    uint8_t  mq2_alarm;
    /* imu */
    float    pitch;
    float    roll;
    float    yaw;
    uint8_t  fall;
    uint8_t  collision;
    /* vital (0 when invalid) */
    int32_t  hr;
    int32_t  spo2;
    /* state mirror */
    helmet_led_state_t led;
    uint8_t  motor;     /* 0..3 */
} helmet_telemetry_t;

/* ---------- Hardware-binding callbacks ------------------------------
 *
 * Pass NULL for any callback you don't need. send_frame and
 * collect_sample MUST be set if you intend to call
 * m100pg_proto_publish_telemetry; send_frame alone suffices for
 * m100pg_proto_send_text.
 *
 * `user` is forwarded verbatim — useful for carrying a device handle
 * without globals.
 *
 * send_frame must return 0 on success, non-zero on transport error;
 * the value bubbles up from m100pg_proto_publish_telemetry /
 * m100pg_proto_send_text.
 */
typedef struct {
    void (*led_set)(helmet_led_state_t state, void *user);
    void (*motor_set_speed)(uint8_t gear, void *user);            /* gear in [0,3] */
    void (*on_unknown)(const char *line, uint16_t len, void *user);
    void (*collect_sample)(helmet_telemetry_t *out, void *user);
    int  (*send_frame)(const char *buf, uint16_t len, void *user);
    void *user;
} m100pg_proto_cb_t;

/* ---------- Context (caller-owned) ---------------------------------- */

typedef struct m100pg_proto {
    m100pg_proto_cb_t  cb;

    /* RX line buffer */
    char     rx_buf[M100PG_PROTO_RX_BUF];
    uint16_t rx_len;
    uint8_t  rx_overflow;

    /* Intent-track mirror (updated on dispatch, read on publish) */
    helmet_led_state_t mirror_led;
    uint8_t            mirror_motor;
} m100pg_proto_t;

/* ---------- API ----------------------------------------------------- */

/*
 * Initialize the context. Call once before any other API. If cb is
 * NULL, all callbacks default to NULL — feed() will be silent and
 * publish/send will return -1.
 *
 * Mirror fields are reset to OFF / 0.
 */
void m100pg_proto_init(m100pg_proto_t *p, const m100pg_proto_cb_t *cb);

/*
 * Feed received bytes. The library buffers until '\n' and dispatches
 * one line at a time. CRLF is handled (trailing '\r' stripped).
 * Lines longer than M100PG_PROTO_RX_BUF are dropped; the parser
 * resyncs at the next '\n'.
 */
void m100pg_proto_feed(m100pg_proto_t *p, const uint8_t *data, uint16_t len);

/*
 * Pull one telemetry sample via cb.collect_sample, format it, and
 * emit one frame via cb.send_frame.
 *
 * Returns:
 *    0  on success
 *   -1  if p is NULL, or send_frame / collect_sample is unregistered
 *   -2  if the formatted frame would exceed M100PG_PROTO_TX_BUF
 *   >0  forwarded from send_frame (transport error)
 */
int m100pg_proto_publish_telemetry(m100pg_proto_t *p);

/*
 * Emit a raw text line. Appends '\n' if missing. Useful for ad-hoc
 * events (e.g. "event=button,id=1").
 *
 * Returns the same set of codes as publish_telemetry.
 */
int m100pg_proto_send_text(m100pg_proto_t *p, const char *line);

#ifdef __cplusplus
}
#endif

#endif /* M100PG_PROTOCOL_H */
