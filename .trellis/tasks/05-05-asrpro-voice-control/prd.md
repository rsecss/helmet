# ASRPro voice module controls LED and motor

## Goal

Integrate the ASRPro offline voice module with the STM32 firmware so voice-triggered UART text commands received on USART1 can control the helmet headlight LED and motor speed while preserving the existing debugging and cooperative-scheduler architecture.

## What I Already Know

* Hardware platform is STM32F103C8T6 using STM32 HAL and a bare-metal cooperative scheduler.
* User has already flashed/configured the ASRPro Tianwen offline voice module firmware.
* Voice module command output is configured as newline-terminated text commands.
* Configured command examples include `led_off\n`, `led_on\n`, and repeated `motor_speed_3\n` wake/action conditions.
* User expects examples such as "open headlight" causing the module to speak back and send `led_on\n` over UART.
* ASRPro wiring is described as `ASR_TX(PA_2) -> PA10` and `ASR_RX(PA_3) -> PA9`, meaning ASR TX goes to MCU USART1_RX PA10 and ASR RX goes to MCU USART1_TX PA9.
* USART1 is currently used as the firmware debug/printf port.
* Existing M100PG module uses USART2 DMA idle receive and can forward received data to USART1 for debugging.
* Existing LED control should go through `helmet_alarm_set_base_led()` so local alarm arbitration can keep priority over cloud or manual base LED state.
* Existing motor control is exposed by `pwm_motor_set_speed(uint8_t percent)` and 4G command mapping uses gear `0..3` to percentages `{0, 33, 66, 100}`.

## Assumptions

* The ASRPro sends plain ASCII/UTF-8 command lines ending in `\n`; optional preceding `\r` should be accepted.
* USART1 baud rate and UART parameters already match the ASRPro module or will be confirmed against `helmet.ioc` / `Core/Src/usart.c`.
* USART1 should run as the dedicated ASRPro command UART during normal use.
* A minimal temporary debug path should remain available so a user can restore USART1 debug behavior with small local changes when needed.
* This task is firmware-only; no ASRPro firmware generation changes are required.

## Requirements

* Add a voice/ASR application module under `APP/` instead of mixing parsing logic into CubeMX-generated files.
* Consume ASRPro commands from USART1 and parse complete newline-terminated lines.
* Support at least:
  * `led_on` -> set base LED on, preferably white.
* `led_off` -> set base LED off.
* Voice LED commands must respect existing safety-alarm arbitration: an active fall/collision alarm can override the base LED state with red output.
* `motor_speed_0`, `motor_speed_1`, `motor_speed_2`, `motor_speed_3` -> set motor gear to 0, 33, 66, 100 percent.
* Support the full `motor_speed_0..3` command set even if the current ASRPro firmware only emits `motor_speed_3`, matching the existing 4G command gear model.
* Ignore malformed or unknown commands without blocking the scheduler.
* Parse fixed lowercase command words while tolerating trailing `\r`, `\n`, and leading/trailing ASCII whitespace.
* Do not perform fuzzy matching, Chinese text matching, or case-insensitive matching for actuator commands.
* Keep UART interrupt/receive callback work short; parsing and actuator control should happen in a scheduler task.
* Use USART1 single-byte interrupt receive for ASRPro command input, because commands are short/low-frequency and USART1 currently has no DMA configured.
* Preserve existing 4G USART2 behavior and M100PG protocol behavior.
* Treat USART1 as a pure ASR link by default; do not emit routine `printf` debug noise to the ASRPro RX line during normal firmware operation.
* Keep the USART1 debug plumbing easy to re-enable for temporary diagnostics.
* Provide a compile-time macro switch that defaults to ASR execution enabled; temporary USART1 debug use can disable ASR parsing/execution with a minimal code change.
* Register the new module through project conventions: app include hub, scheduler, main initialization, and Keil project file when a new `.c` file is added.
* Implement a small ASRPro-specific command parser, while keeping command behavior aligned with the existing M100PG command dictionary.

## Acceptance Criteria

* [ ] Sending `led_on\n` to USART1 causes the base LED state to become white/on through the existing alarm-aware LED path.
* [ ] Sending `led_off\n` to USART1 causes the base LED state to become off unless an active safety alarm overrides output.
* [ ] During an active fall/collision alarm, voice LED commands do not suppress the alarm red indication.
* [ ] Sending `motor_speed_3\n` to USART1 sets the motor PWM speed to 100 percent using the existing motor API.
* [ ] Sending `motor_speed_0\n` to USART1 stops the motor.
* [ ] Unknown USART1 lines are ignored or logged without actuator changes.
* [ ] `led_on\r\n` and ` led_on \n` are accepted as `led_on`.
* [ ] `LED_ON\n`, Chinese text, or partial strings are rejected as unknown commands.
* [ ] A compile-time switch can disable ASR command execution for temporary USART1 debug use.
* [ ] USART1 RX callback work is limited to byte buffering and receive restart; command parsing runs in `asrpro_task()`.
* [ ] USART2 4G receive, forwarding, upload, and command parsing remain untouched except for any required callback dispatch coexistence.
* [ ] Firmware code builds in the existing STM32 HAL / Keil project structure.

## Definition of Done

* Relevant backend spec files are read before implementation.
* Code changes are scoped to the ASRPro integration path.
* Lint/static checks or available build checks are run where feasible.
* Manual hardware validation steps are documented for USART1 command injection.
* README / CLAUDE notes are updated if module list, UART ownership, or workflow changes.

## Out of Scope

* Reflashing or regenerating ASRPro voice firmware.
* Adding cloud/web commands.
* Changing USART2 4G transport protocol.
* Reworking CubeMX peripheral ownership outside protected user-code regions.
* Adding RTOS, dynamic allocation, or blocking command waits.

## Technical Notes

### Existing Patterns Found

* `APP/m100pg.c` shows UART DMA idle receive, ring buffer push in callback, scheduler-side parsing, and optional debug forwarding.
* `APP/m100pg_protocol.c/.h` already parses `led_on`, `led_off`, and `motor_speed_<0..3>` line commands for 4G downlink.
* `APP/m100pg_bsp.c` maps LED and motor semantic commands to board drivers:
  * LED uses `helmet_alarm_set_base_led()`.
  * Motor gear mapping is `{0, 33, 66, 100}` percent.
* `APP/scheduler.c` registers short cooperative tasks with fixed periods.
* `Core/Src/usart.c` currently redirects `printf` to `huart1`, so USART1 is not a clean single-purpose ASR link.
* `Core/Src/usart.c` configures USART1 as 115200 8N1 TX/RX on PA9/PA10.
* Current USART1 configuration has no DMA and no generated `USART1_IRQHandler`; USART2 is the only UART with DMA idle receive configured.

### Likely Files To Modify

* `APP/asrpro.c` / `APP/asrpro.h`: new ASRPro UART receive, line parser, and actuator dispatch.
* `APP/bsp_system.h`: include `asrpro.h`.
* `APP/scheduler.c`: add `asrpro_task`.
* `Core/Src/main.c`: call `asrpro_init()` in a user-code section.
* UART callback file or existing callback owner: route USART1 receive events without breaking USART2 M100PG receive.
* `MDK-ARM/helmet.uvprojx`: add new `APP/asrpro.c`.
* `README.md` / `CLAUDE.md`: update module and UART ownership notes if implementation proceeds.

## Open Questions

* None.

## Decision Log

### USART1 ownership

**Context**: USART1 currently carries `printf` debug output, but ASRPro RX is wired to MCU PA9 and would receive all debug text.

**Decision**: Normal firmware treats USART1 as a pure ASRPro UART. Keep a minimal temporary debug interface/path so users can restore USART1 debugging with small local changes when needed.

**Consequences**:
* Reduces risk of debug text interfering with the voice module.
* Keeps the hardware wiring aligned with ASRPro command input on PA10 / output on PA9.
* Requires existing routine logs that rely on USART1 to be disabled, guarded, or moved out of the normal ASR runtime path.

### LED safety arbitration

**Context**: The project already routes cloud LED commands through `helmet_alarm_set_base_led()` so safety alarms can override normal LED color with red indication.

**Decision**: Voice LED commands must use the same alarm-aware path.

**Consequences**:
* Voice control remains consistent with cloud control.
* `led_off` changes the base LED state but cannot hide an active fall/collision warning.
* Implementation should not directly call `rgb_led_off()` / GPIO writes from the ASR parser.

### Motor command range

**Context**: Existing 4G downlink supports `motor_speed_<0..3>` and maps gears to PWM percentages `{0, 33, 66, 100}`.

**Decision**: ASRPro command parsing should support the same `motor_speed_0..3` command set.

**Consequences**:
* Current `motor_speed_3` voice command works.
* Future ASRPro phrases for stop/low/medium/high speed do not require STM32 firmware changes.
* Voice and cloud control stay aligned around the same motor gear model.

### ASR command parsing strictness

**Context**: ASRPro firmware is configured to emit fixed command text windows, so STM32 does not need natural-language parsing.

**Decision**: Use fixed lowercase command matching after trimming CR/LF and leading/trailing ASCII whitespace.

**Consequences**:
* Common serial terminal and CRLF differences are tolerated.
* Command execution remains deterministic.
* Uppercase, Chinese command text, partial lines, and fuzzy matches do not trigger actuators.

### USART1 debug fallback

**Context**: USART1 is wired to ASRPro for normal operation, but users may still need temporary USART1 debugging with minimal changes.

**Decision**: Add a compile-time macro that defaults to ASR execution enabled. Temporary USART1 debug builds can disable ASR parsing/execution by changing the macro.

**Consequences**:
* Normal firmware is optimized for ASRPro integration.
* Debug fallback remains available without adding runtime UI or persistent settings.
* The implementation must keep this switch localized and easy to find.

### USART1 receive mechanism

**Context**: USART1 is already configured as 115200 8N1 on PA9/PA10, but only USART2 has DMA idle receive and UART IRQ enabled. ASRPro command lines are short and low frequency.

**Decision**: Use single-byte interrupt receive for USART1 ASRPro input.

**Consequences**:
* Avoids adding USART1 DMA resources.
* Requires enabling `USART1_IRQn` and adding/maintaining its HAL IRQ path.
* Callback must only buffer bytes and restart receive; scheduler task handles parsing and actuator control.

### ASR parser boundary

**Context**: `m100pg_protocol` already parses similar line commands, but it also owns 4G-specific telemetry, mirror state, callbacks, and heartbeat semantics.

**Decision**: Implement a small ASRPro-specific parser with the same command behavior rather than feeding ASR bytes into the M100PG protocol instance.

**Consequences**:
* ASRPro module remains independent from 4G upload/downlink lifecycle.
* Command behavior stays consistent with cloud control for LED and motor.
* Avoids broad refactoring of the existing 4G protocol library in this task.

## Technical Approach

* Add `APP/asrpro.c/.h` as the ASRPro integration module.
* Default build treats USART1 as the ASR UART and starts single-byte interrupt receive during `asrpro_init()`.
* Add a localized compile-time switch, for example `ASRPRO_ENABLE_COMMAND_EXECUTION`, defaulting to enabled. Setting it to `0` prevents ASR command execution for temporary USART1 debug use.
* Add `asrpro_uart_rx_cplt_callback(UART_HandleTypeDef *huart)` and call it from `HAL_UART_RxCpltCallback()`.
* In the UART RX callback, only push the received byte into a small static ring/line buffer and restart `HAL_UART_Receive_IT()`.
* In `asrpro_task()`, consume complete lines, trim CR/LF and ASCII edge whitespace, then dispatch fixed commands.
* Dispatch LED through `helmet_alarm_set_base_led()` and motor through `pwm_motor_set_speed()` with gear mapping `{0, 33, 66, 100}`.
* Avoid normal `printf` logging on USART1 when ASR mode is enabled.
* Register the new module in `APP/bsp_system.h`, `APP/scheduler.c`, `Core/Src/main.c`, and `MDK-ARM/helmet.uvprojx`.

## Implementation Plan

1. Add the ASRPro module files with static buffers, compile-time execution switch, command parser, and actuator dispatch.
2. Wire USART1 RX interrupt support in the existing HAL callback/IRQ path while preserving USART2 M100PG receive behavior.
3. Register initialization and scheduler task using existing project patterns.
4. Add the new source file to the Keil project.
5. Update project documentation for ASRPro UART ownership and supported commands.
6. Run available checks and document hardware validation steps.
