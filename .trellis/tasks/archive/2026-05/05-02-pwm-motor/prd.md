# PWM motor driver module

## Goal

Add a PWM motor driver module for SmartHelm firmware so the MCU can control one DC motor through the A channel of a TB6612FNG dual motor driver module using timer PWM and safe direction/stop/standby APIs.

## Requirements

- Add an APP-level motor driver module with module-prefixed public APIs.
- Target motor driver: TB6612FNG dual DC motor driver module, A channel only.
- Motor use case: small fan; PWM duty controls fan speed.
- Target wiring:
  - `PWMA` -> `PB4`
  - `AIN1` -> `PA12`
  - `AIN2` -> `PA11`
  - `STBY` -> `PB15`
  - `AO1/AO2` -> DC motor output.
- Provide normalized speed control, e.g. `0..100%`, and clamp invalid input.
- Provide explicit stop behavior.
- Provide forward/reverse APIs using `AIN1/AIN2`.
- Provide standby/enable behavior using `STBY`.
- Default direction is a configured forward airflow direction; callers should not need to choose direction for normal fan speed control.
- Direction changes must be safe: stop/coast first, switch direction pins, then reapply speed.
- Use HAL PWM APIs in scheduler/main context, not interrupts.
- Keep hardware mappings private at the top of `APP/pwm_motor.c`.
- Register new files in `APP/bsp_system.h`, `Core/Src/main.c`, and `MDK-ARM/helmet.uvprojx`.
- Update `helmet.ioc` and CubeMX-generated timer/GPIO files consistently if a new PWM timer/channel is required.
- Boot default must be safe: motor stopped before application code can request movement.
- Public API must hide timer period, compare value, polarity, direction GPIO, and enable GPIO details.
- Keep SWD debugging available; do not consume PA13/PA14.
- PB4 PWM requires explicit CubeMX/AFIO planning because PB4 is a remapped TIM3_CH1 candidate and can overlap legacy JTAG functionality.

## Acceptance Criteria

- [ ] `pwm_motor_init()` starts the PWM channel with duty 0 or safe stopped state.
- [ ] `pwm_motor_set_speed(uint8_t percent)` clamps to `0..100`.
- [ ] `pwm_motor_stop()` reliably disables motor output or sets duty 0.
- [ ] `pwm_motor_set_direction()` controls `AIN1/AIN2` deterministically.
- [ ] Direction change while running performs a stop/coast transition before reversing.
- [ ] After `pwm_motor_init()` succeeds, `STBY` stays high for fast response; `pwm_motor_stop()` uses coast semantics (`AIN1=AIN2=0`, duty `0`) without toggling `STBY`. Hardware standby (driving `STBY` low at runtime) is intentionally deferred — extend with a private helper if a caller later needs full power-down.
- [ ] `pwm_motor_init()` drives `STBY` high only after safe GPIO/PWM state is configured.
- [ ] No occupied pin is reused, especially PA8 because DHT11 owns it and PA13/PA14 because SWD owns them.
- [ ] Keil project includes the new motor source file.
- [ ] README/CLAUDE mention timer channel and pin mapping after hardware wiring is confirmed.

## Definition of Done

- Keil build expected clean.
- `cppcheck` expected clean for APP changes.
- APP header guard and UTF-8 no BOM + LF checks pass.
- Manual test notes include duty 0/50/100, direction if supported, and boot safe state.

## Out of Scope

- No implementation until the user explicitly resumes this child task.
- No closed-loop speed feedback in MVP.
- No encoder input in MVP.
- No PID control in MVP.
- No motor current sensing or stall detection in MVP.
- No changes to 4G cloud UART behavior.

## Technical Approach

- Current TIM1 is only configured as a 1 MHz base timer and PA8 is unavailable due to DHT11.
- Use TB6612FNG channel A only:
  - `PWMA=PB4` for PWM speed control.
  - `AIN1=PA12`, `AIN2=PA11` for direction/brake/stop logic.
  - `STBY=PB15` for driver enable/standby.
- Plan PB4 as TIM3_CH1 with remap if CubeMX supports the selected mapping; otherwise stop and report the pin conflict before implementation.
- Keep duty conversion local to the module using `__HAL_TIM_SET_COMPARE`.
- Initialize to safe stop before enabling application-level movement:
  1. Configure `AIN1/AIN2` to stop/brake-safe state.
  2. Set PWM duty to 0.
  3. Start PWM output.
  4. Drive `STBY` high only when the module is ready.
- For fan behavior, keep `pwm_motor_set_speed(percent)` independent from direction. Direction is set once during init to `PWM_MOTOR_DIR_FORWARD` unless explicitly changed.
- Direction change sequence:
  1. Save requested speed.
  2. Set duty to 0.
  3. Set `AIN1=0`, `AIN2=0` for coast/stop.
  4. Wait or defer for a short configurable settle period if implemented.
  5. Set target direction pins.
  6. Restore duty.

## Planned Public Contract

```c
typedef enum {
    PWM_MOTOR_DIR_FORWARD = 0,
    PWM_MOTOR_DIR_REVERSE
} pwm_motor_dir_t;

uint8_t pwm_motor_init(void);
void pwm_motor_stop(void);
void pwm_motor_set_speed(uint8_t percent);
void pwm_motor_set_direction(pwm_motor_dir_t direction);
void pwm_motor_set_signed_speed(int16_t percent);
```

Contract notes:

- `pwm_motor_init()` starts or prepares PWM output in stopped state.
- `pwm_motor_set_speed()` clamps values to `0..100`.
- `pwm_motor_stop()` uses coast/stop: `AIN1=0`, `AIN2=0`, duty `0`.
- Direction API is required because TB6612FNG channel A uses `AIN1/AIN2`.
- `pwm_motor_set_signed_speed()` accepts `-100..100`; positive values use forward direction, negative values use reverse direction, and `0` stops.
- Normal fan control should use `pwm_motor_set_speed()` only; direction defaults to forward.
- Reversing a spinning fan must not directly flip `AIN1/AIN2` at nonzero duty.
- STBY enable/standby and short-brake details stay private unless a caller has a concrete use case.

## Decision Tree

- Driver topology: confirmed TB6612FNG A channel.
- PWM output: `PB4`, planned as TIM3_CH1 remap if CubeMX can generate it safely.
- Direction outputs: `AIN1=PA12`, `AIN2=PA11`.
- Standby output: `STBY=PB15`.
- Frequency: default motor PWM should be high enough to reduce audible noise if driver supports it; exact value depends on timer/channel selection.
- Stop mode: confirmed coast/stop for `pwm_motor_stop()`; short brake remains private for now.
- Direction mode: default forward for fan use; reverse API exists for wiring correction/testing but is not part of normal speed-control workflow.
- Control source: local API first; 4G/cloud command integration is out of scope for this child task.

## Manual Verification Plan

- Boot with motor connected: motor remains stopped until API call.
- Set speed 0/50/100 and confirm duty changes as expected.
- Set speed above 100 through test call and confirm clamping.
- Verify `AIN1/AIN2` forward/reverse GPIO states before enabling nonzero speed.
- Verify normal fan workflow: init -> forward direction -> set speed 30/60/100 -> stop/coast.
- Verify reverse workflow only during bench test: stop first, switch direction, then apply low duty.
- Verify `STBY` low disables output and `STBY` high enables output only after safe init.
- Verify `PB4` PWM waveform with oscilloscope or logic analyzer before connecting a motor.
- Search confirms no `HAL_MAX_DELAY` or blocking loops in motor path.

## TB6612FNG Behavior Notes

- TB6612FNG modules use `VM` for motor voltage, `VCC` for logic voltage, and common `GND`; these are hardware wiring requirements outside firmware scope.
- `STBY` must be high for the H-bridge to operate; common breakout boards pull it low by default, so firmware or hardware must actively enable it.
- For channel A, `PWMA`, `AIN1`, and `AIN2` control `AO1/AO2`.
- Common truth table from module guides:
  - `AIN1=H`, `AIN2=L`, `PWMA=H/PWM`: one direction.
  - `AIN1=L`, `AIN2=H`, `PWMA=H/PWM`: opposite direction.
  - `AIN1=H`, `AIN2=H`: short brake.
  - `AIN1=L`, `AIN2=L`: stop/coast.
- Sources: [SparkFun, TB6612FNG Hookup Guide](https://learn.sparkfun.com/tutorials/tb6612fng-hookup-guide/all), [Pololu, TB6612FNG Dual Motor Driver Carrier](https://www.pololu.com/product-info-merged/713), [DigiKey/SparkFun Hookup Guide PDF, TB6612FNG truth table](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/925/TB6612FNG_Hookup_Guide_Web.pdf).

## Relevant Context

- `.trellis/spec/backend/directory-structure.md`: new module registration.
- `.trellis/spec/backend/quality-guidelines.md`: APP module style and CubeMX boundaries.
- `.trellis/spec/backend/database-guidelines.md`: scheduler nonblocking rules.
- Existing timer config: `Core/Src/tim.c`, `Core/Inc/tim.h`, `helmet.ioc`.
- Existing hardware mapping style: `APP/rgb_led.c`.
- STM32F103 remap reference: `Drivers/STM32F1xx_HAL_Driver/Inc/stm32f1xx_hal_gpio_ex.h` documents SPI remap/JTAG-related AFIO patterns; CubeMX should remain the source of truth for final TIM3 remap.

## Open Questions

- None for PWM/TB6612FNG planning; CubeMX feasibility for `PB4` as TIM3_CH1 remap must be verified before implementation.

## Audit Notes — 2026-05-04 (Maple)

Code-vs-PRD audit completed before bench verification.

- AC for runtime STBY behavior reworded above to match the implemented coast-only stop semantics. Runtime STBY toggling is deferred and can be added later as a private helper without breaking the public contract.
- `helmet.ioc` has no explicit `TIM3.RemapMode` field; CubeMX inserts `__HAL_AFIO_REMAP_TIM3_PARTIAL()` automatically because `PB4` is selected as `S_TIM3_CH1` in the pinout. Manual ioc edits are not required.
- `USB` peripheral is not enabled in `helmet.ioc`; `PA11/PA12` are safe to use as plain GPIO outputs for `AIN2/AIN1`.
- `pwm_motor_set_signed_speed()` clamp + sign-flip path (`APP/pwm_motor.c:188-210`) is functionally correct, including `INT16_MIN` input (the early `< -100` branch absorbs the would-be `-(-32768)` UB). Readability refactor deferred — not blocking.
- `pwm_motor_enabled` flag (`APP/pwm_motor.c:20`) tracks STBY level, not motor motion. `pwm_motor_stop()` keeps it `ENABLED` because STBY stays high. Rename / inline-comment deferred — not blocking.
- After `pwm_motor_init()` the H-bridge is enabled, direction selected forward, duty `0`. This is "ready to spin" rather than "fully powered down". Acceptable per Decision Tree (`Stop mode: coast`); revisit only if product policy changes.
- No caller drives the motor yet (`scheduler_task[]` has no motor entry; no other module references `pwm_motor_*`). Intentional per `## Out of Scope`. Future 4G downlink integration must call public `pwm_motor_*` APIs only — never write `PB4 / PA11 / PA12 / PB15` directly.

Bench verification still required (see `## Manual Verification Plan`). Pending items:

- Confirm boot-time safe state with motor connected (motor stays still until first `set_speed > 0`).
- Sweep duty 0 / 30 / 60 / 100 and verify clamp at 200.
- Optional: reverse direction under nonzero duty and check VM rail for current spikes; add a short settle delay inside `pwm_motor_set_direction()` only if a spike is observed.
