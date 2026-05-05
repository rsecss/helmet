# MPU6050 Fall And Collision Alarm

## Goal

Use the existing MPU6050 posture and motion data to detect helmet fall-down and violent collision events, then expose reliable alarm state for local display and 4G telemetry.

## What I already know

- The project is STM32F103C8T6 firmware using STM32 HAL, Keil MDK-ARM V5, and a bare-metal cooperative scheduler.
- `APP/mpu6050.c` already initializes MPU6050 with DMP and reads pitch/roll/yaw plus accelerometer and gyroscope data every 10 ms.
- `APP/m100pg_bsp.c` already collects MPU6050 pitch/roll/yaw into telemetry.
- `APP/lcd_app.c` likely consumes sensor globals for display and may need alarm display updates.
- No new GPIO or CubeMX peripheral configuration is assumed for this task.

## Assumptions

- A fall-down alarm should not trigger from a single noisy sample; it needs angle and motion evidence over a short confirmation window.
- A violent collision alarm can be detected from acceleration vector magnitude and/or gyroscope vector magnitude spikes.
- Local alarm output can initially be represented through existing RGB LED / telemetry / debug output unless a buzzer driver already exists.

## Open Questions

- Which MVP output path should trigger when an alarm is detected?

## Requirements

- Detect accident-grade fall-down from a sequence of motion evidence, not from posture angle alone.
- Detect violent collision from MPU6050 motion spikes.
- Keep collision alarm independent from fall alarm; both alarms may be active at the same time.
- Auto-clear collision alarms after a short hold window.
- Auto-clear fall alarms only after a minimum hold window plus recovered posture/stability.
- Keep a public clear API for future button or cloud reset integration.
- Use conservative default thresholds to reduce false positives.
- Keep fall/collision thresholds centralized near the top of `APP/mpu6050.c` for hardware tuning.
- Show alarm state on LCD, 4G telemetry, and local RGB LED without adding new hardware pins.
- During any active fall/collision alarm, blink the local RGB LED red at a 500 ms interval.
- Expose module-prefixed public query APIs for fall and collision alarm state.
- Keep the MPU6050 scheduler task non-blocking and suitable for 10 ms polling.
- Reuse existing APP modules and avoid direct GPIO writes outside owning drivers.

## Acceptance Criteria

- [ ] Normal upright posture does not set fall or collision alarm.
- [ ] Helmet being placed sideways does not set fall alarm by posture alone.
- [ ] Fall alarm requires motion evidence plus post-event posture/stability confirmation.
- [ ] Acceleration/gyro spike beyond configured collision threshold sets collision alarm even when fall alarm is not set.
- [ ] A confirmed accident can publish both `fall=1` and `collision=1`.
- [ ] Collision alarm clears automatically after the configured hold window.
- [ ] Fall alarm does not clear until the minimum hold window has elapsed and posture/motion are stable again.
- [ ] Thresholds are defined as module-local macros and can be tuned without touching callers.
- [ ] The 10 ms MPU6050 task does not print periodic debug logs.
- [ ] LCD line 6 shows `OK`, `FALL`, `HIT`, or `FALL+HIT`.
- [ ] 4G telemetry includes `fall` and `collision` fields.
- [ ] RGB LED blinks red every 500 ms while an alarm is active, then restores the cloud-controlled base color after alarms clear.
- [ ] Alarm state is available through public MPU6050 APIs and not only global variables.
- [ ] Existing telemetry or display path can include alarm state without breaking current fields.
- [ ] Quality checks pass for touched APP files.

## Definition of Done

- Focused code changes are implemented in APP-owned modules.
- Relevant Trellis backend specs are loaded into implement/check context.
- Static quality checks available in this repo are run or explicitly reported as unavailable.
- Documentation is updated if telemetry, display, or module behavior changes.

## Out of Scope

- Adding new buzzer hardware, CubeMX pin configuration, or interrupt wiring.
- Changing MPU6050 DMP firmware or I2C peripheral configuration.
- Cloud-side protocol changes beyond existing firmware telemetry formatting unless required by current protocol code.

## Technical Notes

- Candidate files from initial search: `APP/mpu6050.c`, `APP/mpu6050.h`, `APP/m100pg_bsp.c`, `APP/m100pg_protocol.*`, `APP/lcd_app.*`, `README.md`, `CLAUDE.md`.
- Relevant constraints: `APP/` owns application modules; public cross-module access should use APIs; scheduler tasks must stay non-blocking.
- Decision: fall-down means accident-grade fall detection. Pure `pitch/roll` tilt is only supporting evidence and must not trigger the final fall alarm by itself.
- Decision: collision and fall alarms are independent event states. Collision can trigger without fall, fall can trigger after full confirmation, and both can be active together.
- Decision: collision uses short auto-clear; fall uses longer auto-clear with recovered posture/stability. `mpu6050_clear_alarm()` remains available for future manual reset paths.
- Decision: initial thresholds should favor fewer false positives over maximum sensitivity. Hardware testing may tune the macros after real helmet motion data is available.
- Decision: MVP outputs are LCD status, 4G telemetry fields, and RGB red 500 ms blinking. Buzzer, cancel button, and countdown confirmation are out of scope until hardware/driver support exists.
- Research summary: Grok results favored a threshold state machine over ML for MCU firmware: low acceleration or rapid motion, high impact, then posture/gyroscope stability confirmation. DeepWiki did not return usable implementation detail for the checked MPU6050/smart-helmet repositories.
