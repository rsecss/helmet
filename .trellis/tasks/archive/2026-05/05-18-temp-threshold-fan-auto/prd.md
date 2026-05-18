# brainstorm: temperature threshold fan auto start

## Goal

Add a firmware feature that starts the SmartHelm fan automatically when the DHT11 temperature exceeds a configured threshold, so the helmet can respond locally to high-temperature conditions without waiting for Web or voice commands.

## What I already know

* User wants to align requirements with `$grill-me`, then use `$brainstorm` to create and develop the task.
* Temperature source is DHT11. `dht11_task()` refreshes cached temperature/humidity and valid state every 1000 ms via the scheduler.
* Fan control is implemented by `APP/pwm_motor.c` through `pwm_motor_set_speed(uint8_t percent)`.
* Existing manual fan commands are supported by both Web/M100PG and ASRPro voice paths:
  * Web/M100PG maps `motor_speed_0..3` to `{0, 33, 66, 100}` percent in `APP/m100pg_bsp.c`.
  * ASRPro maps `motor_speed_0..3` to `{0, 33, 66, 100}` percent in `APP/asrpro.c`.
* Current telemetry uploads `temp`, `hum`, and `motor`, but the `motor` field is an intent mirror from protocol state, not guaranteed actual hardware readback.
* There is no existing temperature alarm or fan auto-control arbitration module.

## Assumptions (temporary)

* The threshold can be compile-time configured first, unless the user explicitly needs Web-configurable runtime threshold.
* Automatic fan control should use DHT11 cached values and must not trigger when `dht11_is_valid() == 0`.
* The MVP should avoid persistent storage because the project currently has no Flash config subsystem.
* The fan is the existing PWM motor output driven through TB6612FNG A channel.

## Open Questions

* None.

## Requirements (evolving)

* Add temperature-threshold based automatic fan start logic.
* Use existing DHT11 public getters; do not perform extra DHT11 reads from the new logic.
* Use existing `pwm_motor_set_speed()` / `pwm_motor_stop()` public APIs; do not write GPIO/TIM registers from the new logic.
* Keep scheduler behavior nonblocking.
* Add a small `fan_control` arbitration module as the single owner of fan output policy.
* Web/M100PG and ASRPro voice motor commands must update the fan manual/base gear through `fan_control`, not call `pwm_motor_set_speed()` directly.
* Temperature auto logic must run through `fan_control_task()` and use DHT11 cached readings only.
* `fan_control` is the only application policy layer that decides the final fan speed. It may call `pwm_motor_set_speed()` after calculating the final output.
* Manual Web/voice fan-off commands have the highest priority. `motor_speed_0` must immediately stop the fan even if high-temperature auto mode opened it.
* Manual fan-off must not be immediately overridden by the next high-temperature task. The off override clears only when the user sends `motor_speed_1..3` or temperature recovers to `28°C`.
* If the manual/base fan gear is higher than the automatic high-temperature gear, keep the higher output.
* The MVP threshold parameters are:
  * Auto-on threshold: `30°C`.
  * Auto-release threshold: `28°C`.
  * Auto minimum fan gear: gear 1 (`33%`).
* Hysteresis is required: auto control activates at or above `30°C` and releases only after temperature is at or below `28°C`.
* Telemetry must expose automatic fan state and threshold values so Web can explain automatic takeover:
  * `fan_auto=0|1`
  * `temp_limit=30`
  * `temp_recover=28`
* Thresholds are fixed firmware constants for this MVP. Web/downlink cannot modify them.
* DHT11 invalid handling:
  * If auto high-temperature mode is inactive, invalid DHT11 data must not start the fan.
  * If auto high-temperature mode is already active, invalid DHT11 data may hold the auto state for up to 3 scheduler/task seconds, then release.

## Acceptance Criteria (evolving)

* [x] When DHT11 data is valid and temperature exceeds the selected threshold, the fan starts automatically.
* [x] When DHT11 data is invalid, the automatic controller does not start the fan from stale or unknown temperature data.
* [x] Manual Web/voice fan-off immediately turns the fan off even during high-temperature auto mode.
* [x] After manual fan-off, high-temperature auto mode does not reopen the fan until user sends gear 1..3 or temperature recovers to `28°C`.
* [x] At `temp >= 30` with valid DHT11 data, final fan speed is at least 33%.
* [x] At `temp <= 28` after a high-temperature event, automatic fan override releases.
* [x] Telemetry includes `fan_auto`, `temp_limit`, and `temp_recover`.
* [x] Web/manual fan-off during high temperature results in telemetry showing `fan_auto=0` and `motor=0`.
* [x] A single invalid DHT11 read while auto mode is active does not immediately stop the fan.
* [x] Continuous DHT11 invalid state for about 3 seconds releases automatic fan override.
* [x] Web/M100PG `motor_speed_0..3` updates manual/base fan gear through `fan_control`.
* [x] ASRPro `motor_speed_0..3` updates manual/base fan gear through `fan_control`.
* [x] Direct policy-level calls to `pwm_motor_set_speed()` remain limited to the fan-control module and low-level motor driver.
* [x] `git diff --check` and available project quality checks pass for touched files.

## Definition of Done

* Tests or validation steps added/updated where practical.
* Static quality checks pass.
* README/CLAUDE notes updated if telemetry, architecture, or user-visible behavior changes.
* Rollback path is simple: disable/remove the new auto-control task without changing sensor or motor drivers.

## Out of Scope (explicit)

* Runtime threshold persistence in internal Flash.
* Web/downlink threshold setting commands.
* New cloud/Web UI implementation in this firmware repo; only firmware protocol fields are changed.
* New hardware peripherals.
* PID or continuously variable thermal control unless explicitly selected.

## Technical Notes

* Relevant files inspected:
  * `APP/dht11.c` / `APP/dht11.h`: temperature cache and validity.
  * `APP/pwm_motor.c` / `APP/pwm_motor.h`: motor/fan PWM output.
  * `APP/m100pg_bsp.c`: Web command motor gear mapping.
  * `APP/asrpro.c`: voice command motor gear mapping.
  * `APP/scheduler.c`: cooperative task table.
  * `APP/helmet_alarm.c`: existing cross-module arbitration pattern for RGB LED safety priority.
* Relevant specs:
  * `.trellis/spec/backend/directory-structure.md`: new APP modules, scheduler registration, public APIs.
  * `.trellis/spec/backend/database-guidelines.md`: single-writer runtime state and nonblocking scheduler task rules.
  * `.trellis/spec/backend/error-handling.md`: local degradation and no application `Error_Handler()`.
  * `.trellis/spec/backend/quality-guidelines.md`: header guards, no dynamic allocation, no blocking interrupt logic, documentation/check requirements.
  * `工程设计与代码约束.md`: KISS/YAGNI, single-responsibility modules, explicit state transitions, and verifiable success criteria.
* Likely design issue: direct calls to `pwm_motor_set_speed()` already exist from Web and voice paths, so automatic control needs an arbitration rule to avoid fighting manual commands.

## Technical Approach

Add `APP/fan_control.c` and `APP/fan_control.h`:

* Constants:
  * `FAN_CONTROL_TEMP_ON_C = 30U`
  * `FAN_CONTROL_TEMP_OFF_C = 28U`
  * `FAN_CONTROL_AUTO_GEAR = 1U`
  * `FAN_CONTROL_INVALID_HOLD_MS = 3000U`
* Public API:
  * `void fan_control_init(void);`
  * `void fan_control_task(void);`
  * `void fan_control_set_manual_gear(uint8_t gear);`
  * `uint8_t fan_control_get_manual_gear(void);`
  * `uint8_t fan_control_get_output_gear(void);`
  * `uint8_t fan_control_is_auto_active(void);`
  * `uint8_t fan_control_get_temp_on_threshold(void);`
  * `uint8_t fan_control_get_temp_off_threshold(void);`
* Gear mapping remains `{0, 33, 66, 100}` percent.
* `fan_control_task()` reads only `dht11_is_valid()` and `dht11_get_temperature()`; it does not trigger a DHT11 bus read.
* Final gear is `max(manual_gear, auto_gear_when_active)`.
* Register `fan_control_init()` in `Core/Src/main.c`, `fan_control_task()` in `APP/scheduler.c`, and `fan_control.h` in `APP/bsp_system.h`.
* Add `APP/fan_control.c` to `MDK-ARM/helmet.uvprojx`.
* Update `APP/m100pg_bsp.c` and `APP/asrpro.c` to call `fan_control_set_manual_gear()`.
* Extend `helmet_telemetry_t` and `m100pg_protocol.c` to include `fan_auto`, `temp_limit`, and `temp_recover`.
* Update README/CLAUDE protocol notes to document the new telemetry fields and fan behavior.

## Code-Spec Depth Check

This task changes cross-module behavior and telemetry payload contracts.

Target code-spec/docs to update or reference:

* `.trellis/spec/backend/directory-structure.md`
* `.trellis/spec/backend/database-guidelines.md`
* `.trellis/spec/backend/quality-guidelines.md`
* `README.md`
* `CLAUDE.md`

Contract:

```c
void fan_control_init(void);
void fan_control_task(void);
void fan_control_set_manual_gear(uint8_t gear); /* clamps to 0..3 */
uint8_t fan_control_get_manual_gear(void);
uint8_t fan_control_get_output_gear(void);
uint8_t fan_control_is_auto_active(void);
uint8_t fan_control_get_temp_on_threshold(void);
uint8_t fan_control_get_temp_off_threshold(void);
```

Telemetry additions:

```text
fan_auto=<0|1>,temp_limit=30,temp_recover=28
```

Validation matrix:

* Base: `temp=29`, manual gear `0` -> output gear `0`, `fan_auto=0`.
* Good: `temp=30`, manual gear `0` -> output gear `1`, `fan_auto=1`.
* Good: `temp=31`, manual gear `3` -> output gear `3`, `fan_auto=1`.
* Good: high-temp active, manual gear set `0` -> output gear `0`, `fan_auto=0`, and auto does not reopen until gear 1..3 or temp <= 28.
* Good: after active, `temp=28` -> auto releases and output returns to manual gear.
* Bad/edge: DHT11 invalid while inactive -> no auto start.
* Bad/edge: DHT11 invalid while active for <3s -> auto remains active.
* Bad/edge: DHT11 invalid while active for about 3s -> auto releases.

## Implementation Plan

1. [x] Add `fan_control` module and register it in the app include hub, main init, scheduler, and Keil project.
2. [x] Route Web/M100PG and ASRPro motor commands through `fan_control_set_manual_gear()`.
3. [x] Extend telemetry struct/format and BSP sampling with `fan_auto`, `temp_limit`, `temp_recover`, and actual final motor gear.
4. [x] Update README/CLAUDE protocol and architecture notes.
5. [x] Run static checks available in this repo and inspect diffs for protocol buffer size, header guards, encoding, and line endings.

## Verification Notes

* `git diff --check` passed.
* Touched APP files are UTF-8 without BOM and LF line endings.
* APP header guard check passed.
* `MDK-ARM/helmet.uvprojx` parses as XML after adding `fan_control.c/.h`.
* GCC syntax checks passed for `APP/fan_control.c`, `APP/asrpro.c`, `APP/m100pg_bsp.c`, `APP/m100pg_protocol.c`, `APP/scheduler.c`, and `Core/Src/main.c`.
* Telemetry worst-case length estimate is 192 bytes, below `M100PG_PROTO_TX_BUF=256`.
* `cppcheck` and Keil command-line Build were not run because `cppcheck` and `UV4.exe` are not available in this shell.

## Decision (ADR-lite)

**Context**: Web/M100PG and ASRPro voice currently call `pwm_motor_set_speed()` directly. A temperature auto-start feature would fight manual commands unless there is a single arbitration rule.

**Decision**: Use an automatic high-temperature priority model that does not permanently steal manual control. Manual commands update the base fan gear. When high temperature is active, final fan output is `max(base_manual_speed, auto_temperature_speed)`. When temperature recovers, the final output returns to the base manual gear.

**Consequences**: The implementation likely needs a small fan-control arbitration layer or wrapper API so Web, voice, and temperature logic share one final writer to `pwm_motor_set_speed()`.

## Decision (ADR-lite): Fan Control Arbitration Module

**Context**: Existing Web/M100PG and ASRPro paths directly call `pwm_motor_set_speed()`. Adding temperature auto-control without refactoring those paths would create competing writers and make restore-to-manual behavior unreliable.

**Decision**: Add a `fan_control` module. Web and voice commands update the manual/base gear through this module. The temperature task updates auto state inside this module. The module computes final output and calls `pwm_motor_set_speed()`.

**Consequences**: More files are touched, but the behavior becomes explicit and testable. Future Web-configurable thresholds or other fan policies can extend `fan_control` without rewriting low-level motor code.

## Decision (ADR-lite): Thresholds

**Context**: The first version should be easy to demo and avoid rapid fan toggling around the threshold.

**Decision**: Use `30°C` as the auto-on threshold, `28°C` as the auto-release threshold, and gear 1 (`33%`) as the automatic minimum fan speed.

**Consequences**: The feature is intentionally conservative and low-noise. A manual command can still set gear 2 or 3 while high-temperature auto mode is active.

## Decision (ADR-lite): Telemetry

**Context**: If automatic high-temperature control overrides a manual fan-off command, Web needs enough state to explain why the fan remains on.

**Decision**: Extend telemetry with `fan_auto=0|1`, `temp_limit=<uint8>`, and `temp_recover=<uint8>`.

**Consequences**: Firmware protocol formatting, `helmet_telemetry_t`, README/CLAUDE protocol notes, and front-end parser expectations must be kept in sync. `M100PG_PROTO_TX_BUF` size must be rechecked after adding fields.

## Decision (ADR-lite): Threshold Configuration

**Context**: Runtime Web-configurable thresholds require downlink parsing, validation, state management, and likely future persistence. The repository currently has no Flash-backed configuration subsystem.

**Decision**: Keep temperature thresholds as firmware constants for this MVP and expose them through telemetry only.

**Consequences**: The feature remains deterministic and easy to validate. Users must update firmware to change thresholds until a future configuration task adds runtime/persistent settings.

## Decision (ADR-lite): DHT11 Invalid Handling

**Context**: DHT11 can fail individual reads. Releasing automatic high-temperature control immediately on one failed read could turn off the fan while the helmet is still hot.

**Decision**: If auto mode is inactive, invalid DHT11 data never starts it. If auto mode is already active, keep it for up to 3 seconds of continuous invalid readings, then release.

**Consequences**: The fan is stable across transient DHT11 failures while still recovering from long sensor outages.
