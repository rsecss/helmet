# brainstorm: OLED and PWM motor parallel development

## Goal

Parallelize development of two independent firmware modules: an OLED display module and a PWM motor driver module. Each module should run in its own Trellis child task and worktree, then merge back to `dev` after hardware-facing conflicts are reviewed.

## What I already know

- User wants to pause the existing 4G cloud UART task for now.
- User wants two worktrees for parallel development: OLED and PWM motor driver.
- Current firmware target is STM32F103C8T6 with bare-metal cooperative scheduler.
- New APP modules must be registered in `APP/bsp_system.h`, `Core/Src/main.c`, `APP/scheduler.c` when periodic, and `MDK-ARM/helmet.uvprojx`.
- `Core/` is CubeMX-generated; peripheral changes should keep `helmet.ioc` as source of truth.
- Current used peripherals:
  - I2C1 PB6/PB7: MPU6050.
  - I2C2 PB10/PB11: MAX30102.
  - TIM1: base timer at 1 MHz, currently not configured as PWM.
  - PA8: DHT11 data line, must not be reused for PWM.
  - PB12/PB13/PB14: RGB LED.

## Assumptions (temporary)

- OLED can be implemented as an I2C OLED, likely SSD1306-compatible, sharing an existing I2C bus if the address does not conflict.
- PWM motor driver needs at least one timer PWM output plus optional direction GPIO pins.
- The two child tasks can proceed independently if they avoid editing the same CubeMX-generated files without explicit pin decisions.

## Open Questions

- Which concrete hardware wiring should be used for OLED bus/address and motor PWM/direction pins?

## Requirements (evolving)

- Create separate child tasks for OLED and PWM motor driver.
- Configure each child task for backend/firmware development.
- Launch each child task in its own worktree only after the user explicitly says to begin implementation.
- Do not start implementation agents during the planning-only phase.
- Keep each module scoped to `APP/` plus required integration points.
- Avoid direct conflicts with the paused 4G task.

## Acceptance Criteria (evolving)

- [x] Parent Trellis task exists for parallel planning.
- [x] OLED child task exists.
- [x] PWM motor child task exists.
- [x] Each child task has PRD, branch, scope, context, and validation configured.
- [ ] OLED worktree is created and assigned to an agent.
- [ ] PWM motor worktree is created and assigned to an agent.
- [ ] Worktree outputs can be reviewed and merged independently.

## Definition of Done (team quality bar)

- Keil build is expected to pass with no new warnings after merging.
- APP headers use include guards and `extern "C"` blocks.
- Files remain UTF-8 without BOM and LF line endings.
- `cppcheck` quality gate remains clean.
- README/CLAUDE/spec notes are updated if peripherals or architecture change.

## Out of Scope (explicit)

- Do not continue 4G cloud UART implementation in this parallel batch.
- Do not implement OLED or PWM motor code until the user explicitly resumes implementation.
- Do not modify cloud/web/mini-program layers.
- Do not use PA8 for motor PWM because it is already DHT11.
- Do not hand-edit vendor `Drivers/`.

## Technical Notes

- Relevant specs:
  - `.trellis/spec/backend/directory-structure.md`: APP module layout and registration steps.
  - `.trellis/spec/backend/quality-guidelines.md`: code style, CubeMX boundary, header guards, no blocking ISR work.
  - `.trellis/spec/backend/database-guidelines.md`: static buffers, shared runtime state, nonblocking scheduler contracts.
- Worktree storage is configured in `.trellis/worktree.yaml` as `../trellis-worktrees`.
- Parallel children:
  - `.trellis/tasks/05-02-oled-display`
  - `.trellis/tasks/05-02-pwm-motor`
- Child task state:
  - OLED: `planning`, backend, scope `firmware-oled`, branch `feature/oled-display`, worktree not created.
  - PWM motor: `planning`, backend, scope `firmware-pwm-motor`, branch `feature/pwm-motor`, worktree not created.
- Context validation:
  - OLED: `implement.jsonl` 15 entries, `check.jsonl` 12 entries, `debug.jsonl` 1 entry.
  - PWM motor: `implement.jsonl` 17 entries, `check.jsonl` 15 entries, `debug.jsonl` 1 entry.

## Alignment Snapshot

### Confirmed from repository

- Firmware is the only implementation scope.
- The active integration style is APP module plus CubeMX-generated peripheral init.
- `APP/bsp_system.h` is the public include hub for application modules.
- `Core/Src/main.c` initialization must stay inside `USER CODE BEGIN 2`.
- `APP/scheduler.c` is cooperative; any OLED or motor periodic task must be short and nonblocking.
- `MDK-ARM/helmet.uvprojx` must include new `.c` files for Keil builds.
- `README.md` and `CLAUDE.md` must be updated once final peripheral mapping changes.

### Confirmed pin/peripheral constraints

- `I2C1 PB6/PB7` is already MPU6050.
- `I2C2 PB10/PB11` is already MAX30102.
- `TIM1` is currently only a 1 MHz base timer, not a PWM output.
- `PA8` is DHT11 and must not be used for PWM output.
- `PB12/PB13/PB14` are RGB LED outputs.
- TB6612FNG motor wiring decision:
  - `PWMA` -> `PB4`
  - `AIN1` -> `PA12`
  - `AIN2` -> `PA11`
  - `STBY` -> `PB15`
  - `AO1/AO2` -> motor output.
- `USART1` is debugging, `USART2` is M100PG 4G DTU.

### Open alignment gaps

- No OLED/ST7735 hardware alignment gaps remain except implementation-time verification of the ST7735 tab/init offset variant.
- Motor use case and stop behavior are aligned: small fan speed control; default `pwm_motor_stop()` uses coast/stop, with a separate brake API if needed.
- Whether the two worktrees should only prepare code patches or also run autonomous agent implementation after final confirmation.

## Proposed Worktree Split

### Worktree A: OLED display module

- Owns `APP/oled.c`, `APP/oled.h`, font/table files if needed, low-frequency display task, and integration docs.
- Likely touches `APP/bsp_system.h`, `APP/scheduler.c`, `Core/Src/main.c`, `MDK-ARM/helmet.uvprojx`, and possibly `README.md` / `CLAUDE.md`.
- Should avoid CubeMX peripheral changes if using existing I2C bus.

### Worktree B: PWM motor driver module

- Owns `APP/pwm_motor.c`, `APP/pwm_motor.h`, motor speed/direction APIs, and integration docs.
- Likely touches `helmet.ioc`, `Core/Inc/tim.h`, `Core/Src/tim.c`, `Core/Src/gpio.c`, `APP/bsp_system.h`, `Core/Src/main.c`, `MDK-ARM/helmet.uvprojx`, and possibly `README.md` / `CLAUDE.md`.
- This task has higher merge-conflict risk because timer/GPIO CubeMX output is shared infrastructure.

## Decision Tree

### OLED

1. Controller and resolution:
   - Current decision: ST7735 1.44-inch SPI TFT color display, `128x128`.
   - Impact: OLED task name remains for continuity, but implementation is a TFT LCD/ST7735 display driver, not an I2C monochrome OLED driver.
2. Bus:
   - Current target wiring: `DC=PB1`, `OLED_SCL=PB0`, `OLED_SDA=PA7`.
   - Constraint: this pin set does not match STM32F103 hardware SPI SCK/MOSI pairs, so the default plan is GPIO software SPI unless wiring changes.
   - Alternative: use hardware SPI only if `SCL` can move to a real SPI SCK pin such as SPI1 SCK (`PA5`) or remapped SPI1 SCK (`PB3`) with MOSI on the matching pin.
3. Buffer model:
   - Recommended default: direct draw/windowed writes, not a full 128x160 RGB565 framebuffer.
   - Impact: a full 128x160 RGB565 framebuffer costs about 40 KB, which exceeds the STM32F103C8T6 20 KB SRAM budget.
4. Refresh policy:
   - Recommended default: low-frequency task, about 200-500 ms, with dirty/changed-content refresh.
   - Impact: avoids starving 4G UART ring buffer and I2C sensors.
5. Default visual state:
   - Current decision: black background by default.
   - Impact: init path should clear screen to black and drawing APIs should accept foreground/background colors.
6. Control pins:
   - Current hardware fact and accepted constraint: PCB has already been designed with `BLK`, `CS`, and `RES` floating; this will not be changed for this task.
   - Implementation consequence: firmware must not allocate MCU pins or APIs for `BLK`, `CS`, or `RES`.
   - Verification consequence: hardware test must explicitly confirm this PCB can still show a stable black clear and status text after power-on.

### PWM motor

1. Driver topology:
   - Current decision: TB6612FNG dual DC motor driver module, using only channel A.
   - Impact: public API should support speed, forward/reverse, stop, brake, standby/enable.
2. PWM timer/channel:
   - Current wiring: `PWMA=PB4`.
   - Planning constraint: PB4 is not the default TIM3_CH1 pin; implementation should configure TIM3_CH1 remap in CubeMX while keeping SWD usable.
   - Impact: requires CubeMX `.ioc` regeneration and will touch `Core/Src/tim.c`, `Core/Src/gpio.c`, and possibly AFIO remap setup.
3. Duty API:
   - Recommended default: public API accepts `0..100` percent and clamps internally.
   - Impact: callers do not depend on timer period/compare values.
4. Stop behavior:
   - Current decision: `pwm_motor_stop()` uses coast/stop (`AIN1=0`, `AIN2=0`, duty `0`).
   - Impact: short brake remains a separate explicit API and is not the default fan stop behavior.
5. Standby:
   - Current wiring: `STBY=PB15`.
   - Recommended behavior: boot default is standby/disabled, then drive `STBY` high only after `pwm_motor_init()` has set direction pins and duty to safe state.
6. Direction:
   - Recommended decision for fan: keep direction API, but default to one configured forward airflow direction.
   - Runtime reverse should first stop/coast briefly, then switch `AIN1/AIN2`, then reapply PWM to avoid mechanical/electrical shock.

## Merge and Risk Plan

- Start OLED first if the display can share existing I2C with no CubeMX changes.
- Start PWM in a separate worktree after final timer/channel/pin decision because it likely modifies `helmet.ioc` and generated Core files.
- Merge order recommendation:
  1. OLED if it avoids `.ioc` changes.
  2. PWM after reviewing CubeMX-generated diff.
  3. Resolve shared integration files manually: `APP/bsp_system.h`, `APP/scheduler.c`, `Core/Src/main.c`, `MDK-ARM/helmet.uvprojx`, `README.md`, `CLAUDE.md`.
- Do not let both worktrees independently make broad documentation rewrites; documentation should describe final merged peripheral map.

## Worktree Start Gate

Do not run these commands until the user explicitly resumes implementation:

```powershell
python ./.trellis/scripts/multi_agent/start.py .trellis/tasks/05-02-oled-display --platform codex
python ./.trellis/scripts/multi_agent/start.py .trellis/tasks/05-02-pwm-motor --platform codex
```

## Expansion Sweep

- Future evolution: OLED may later show sensor summaries, 4G status, alerts, and motor state; keep display API data-oriented instead of hardcoding one screen.
- Related scenarios: PWM motor may later support fan speed, vibration motor, or actuator control; keep speed API normalized as percent/duty rather than raw timer compare only.
- Failure and edge cases: OLED absent or I2C NACK must not block scheduler; motor API must clamp duty and define stop/brake/coast behavior clearly.
