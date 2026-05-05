#include "m100pg_protocol.h"

#include <stdio.h>
#include <string.h>

/* ---------- Helpers -------------------------------------------------- */

static const char *led_state_to_string(helmet_led_state_t s)
{
    switch (s) {
        case HELMET_LED_WHITE: return "white";
        case HELMET_LED_RED:   return "red";
        case HELMET_LED_GREEN: return "green";
        case HELMET_LED_OFF:
        default:               return "off";
    }
}

static int proto_emit(m100pg_proto_t *p, const char *buf, uint16_t len)
{
    if (!p->cb.send_frame) return -1;
    return p->cb.send_frame(buf, len, p->cb.user);
}

/* ---------- Line dispatch ------------------------------------------- *
 *
 * Strict equality / fixed-width prefix match. No strtok, no regex.
 */

static void dispatch_led(m100pg_proto_t *p, helmet_led_state_t state)
{
    p->mirror_led = state;
    if (p->cb.led_set) {
        p->cb.led_set(state, p->cb.user);
    }
}

static void dispatch_motor(m100pg_proto_t *p, uint8_t gear)
{
    p->mirror_motor = gear;
    if (p->cb.motor_set_speed) {
        p->cb.motor_set_speed(gear, p->cb.user);
    }
}

static void dispatch_line(m100pg_proto_t *p, char *line, uint16_t len)
{
    /* Strip trailing CR for CRLF transports. */
    if (len > 0 && line[len - 1] == '\r') {
        line[--len] = '\0';
    }
    if (len == 0) return;

    /* Heartbeat — devices must ignore (architecture.md §6.3). */
    if (len == 4 && (memcmp(line, "ping", 4) == 0 ||
                     memcmp(line, "pong", 4) == 0)) {
        return;
    }

    /* led_on / led_off */
    if (len == 6 && memcmp(line, "led_on", 6) == 0) {
        dispatch_led(p, HELMET_LED_WHITE);    /* default-on = white */
        return;
    }
    if (len == 7 && memcmp(line, "led_off", 7) == 0) {
        dispatch_led(p, HELMET_LED_OFF);
        return;
    }

    /* led_color_<white|red|green> — extension verbs (architecture.md §4.2). */
    {
        static const char kPrefix[]   = "led_color_";
        const uint16_t    kPrefixLen  = (uint16_t)(sizeof(kPrefix) - 1);  /* 10 */

        if (len > kPrefixLen && memcmp(line, kPrefix, kPrefixLen) == 0) {
            const char    *suf = line + kPrefixLen;
            uint16_t       slen = (uint16_t)(len - kPrefixLen);

            if (slen == 5 && memcmp(suf, "white", 5) == 0) {
                dispatch_led(p, HELMET_LED_WHITE);
                return;
            }
            if (slen == 3 && memcmp(suf, "red", 3) == 0) {
                dispatch_led(p, HELMET_LED_RED);
                return;
            }
            if (slen == 5 && memcmp(suf, "green", 5) == 0) {
                dispatch_led(p, HELMET_LED_GREEN);
                return;
            }
            /* Unknown color — fall through to on_unknown. */
        }
    }

    /* motor_speed_<0..3> — exact prefix + one digit. */
    {
        static const char kPrefix[]   = "motor_speed_";
        const uint16_t    kPrefixLen  = (uint16_t)(sizeof(kPrefix) - 1);  /* 12 */

        if (len == (uint16_t)(kPrefixLen + 1u) &&
            memcmp(line, kPrefix, kPrefixLen) == 0) {
            char c = line[kPrefixLen];
            if (c >= '0' && c <= '3') {
                dispatch_motor(p, (uint8_t)(c - '0'));
                return;
            }
            /* Out-of-range digit / non-digit — fall through. */
        }
    }

    /* Unrecognised — let the caller extend the dictionary. */
    if (p->cb.on_unknown) {
        p->cb.on_unknown(line, len, p->cb.user);
    }
}

/* ---------- Public API ---------------------------------------------- */

void m100pg_proto_init(m100pg_proto_t *p, const m100pg_proto_cb_t *cb)
{
    if (!p) return;

    if (cb) {
        p->cb = *cb;
    } else {
        m100pg_proto_cb_t empty = {0};
        p->cb = empty;
    }

    p->rx_len       = 0;
    p->rx_overflow  = 0;
    p->rx_buf[0]    = '\0';

    p->mirror_led   = HELMET_LED_OFF;
    p->mirror_motor = 0;
}

void m100pg_proto_feed(m100pg_proto_t *p, const uint8_t *data, uint16_t len)
{
    if (!p || !data) return;

    for (uint16_t i = 0; i < len; ++i) {
        char c = (char)data[i];

        if (c == '\n') {
            if (p->rx_overflow) {
                p->rx_overflow = 0;
                p->rx_len      = 0;
                p->rx_buf[0]   = '\0';
                continue;
            }
            p->rx_buf[p->rx_len] = '\0';
            dispatch_line(p, p->rx_buf, p->rx_len);
            p->rx_len    = 0;
            p->rx_buf[0] = '\0';
            continue;
        }

        if (p->rx_overflow) continue;   /* skip until next '\n' */

        if ((uint16_t)(p->rx_len + 1u) >= M100PG_PROTO_RX_BUF) {
            p->rx_overflow = 1;
            continue;
        }
        p->rx_buf[p->rx_len++] = c;
    }
}

int m100pg_proto_publish_telemetry(m100pg_proto_t *p)
{
    if (!p) return -1;
    if (!p->cb.send_frame || !p->cb.collect_sample) return -1;

    helmet_telemetry_t t;
    memset(&t, 0, sizeof t);

    /* Pre-fill mirror fields with last applied intent; the caller's
     * collect_sample is free to overwrite them with hardware readback. */
    t.led   = p->mirror_led;
    t.motor = p->mirror_motor;

    p->cb.collect_sample(&t, p->cb.user);

    char buf[M100PG_PROTO_TX_BUF];
    int  n = snprintf(buf, sizeof buf,
                      "temp=%u,hum=%u,mq2=%lu,"
                      "pitch=%.1f,roll=%.1f,yaw=%.1f,"
                      "hr=%ld,spo2=%ld,"
                      "led=%s,motor=%u\n",
                      (unsigned)t.temp,
                      (unsigned)t.hum,
                      (unsigned long)t.mq2,
                      (double)t.pitch,
                      (double)t.roll,
                      (double)t.yaw,
                      (long)t.hr,
                      (long)t.spo2,
                      led_state_to_string(t.led),
                      (unsigned)t.motor);

    if (n < 0 || (size_t)n >= sizeof buf) return -2;
    return proto_emit(p, buf, (uint16_t)n);
}

int m100pg_proto_send_text(m100pg_proto_t *p, const char *line)
{
    if (!p || !line) return -1;
    if (!p->cb.send_frame) return -1;

    size_t len = strlen(line);

    if (len > 0 && line[len - 1] == '\n') {
        return proto_emit(p, line, (uint16_t)len);
    }

    if ((len + 1u) >= M100PG_PROTO_TX_BUF) return -2;

    char buf[M100PG_PROTO_TX_BUF];
    memcpy(buf, line, len);
    buf[len]     = '\n';
    buf[len + 1] = '\0';
    return proto_emit(p, buf, (uint16_t)(len + 1u));
}
