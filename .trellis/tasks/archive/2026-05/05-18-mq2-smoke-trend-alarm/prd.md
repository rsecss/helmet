# MQ2 Smoke Trend Alarm

## Goal

Add an MQ2 smoke trend abnormal state that participates in local alarm arbitration, 4G telemetry, and LCD status display.

## Requirements

- Add `mq2_is_trend_alarm()` as the public MQ2 trend alarm getter.
- Use the MQ2 normalized trend index as the alarm scale; clean air is about 100.
- Trigger alarm after 3 consecutive samples at or above trend index 180.
- Recover alarm after 10 consecutive samples at or below trend index 130.
- Ignore the first 5 MQ2 task samples for alarm counting while still updating the numeric value.
- Treat invalid or non-positive MQ2 values as non-triggering; when already alarmed, they only contribute to the recovery counter.
- Let MQ2 alarm enter `helmet_alarm_task()` with yellow fast blink for at least 5 seconds.
- Keep MPU6050 fall/collision red blink above MQ2 yellow blink.
- Add `RGB_LED_COLOR_YELLOW` for local alarm output and telemetry mirroring.
- Add telemetry field `mq2_alarm=0/1` immediately after `mq2`.
- Support uplink `led=yellow` but do not add `led_color_yellow` downlink.
- Append `ALM` to the LCD smoke line while MQ2 alarm is active.
- Update `README.md` and `engineering-function-implementation-checklist.md` to reflect the implemented engineering evidence.

## Acceptance Criteria

- [ ] `APP/mq2.c/.h` expose and maintain MQ2 trend alarm state.
- [ ] `APP/helmet_alarm.c` prioritizes red MPU6050 alarm over yellow MQ2 alarm and restores base LED after alarm hold windows.
- [ ] `APP/m100pg_protocol.c/.h` emits `mq2_alarm` and supports `led=yellow` in uplink only.
- [ ] `APP/m100pg_bsp.c` fills `mq2_alarm` and mirrors MQ2 alarm as yellow when no higher red alarm is active.
- [ ] `APP/lcd_app.c` shows `ALM` on the smoke line when `mq2_is_trend_alarm()` is active.
- [ ] README and implementation checklist describe MQ2 trend alarm participation in arbitration.

## Technical Notes

- No CubeMX, pin, ADC, DMA, or scheduler period changes are required.
- No new dynamic allocation or blocking calls are allowed.
- The MQ2 telemetry value is a normalized trend index, not a precise gas concentration claim.
