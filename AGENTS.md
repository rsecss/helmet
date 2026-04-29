<!-- TRELLIS:START -->
# Trellis Instructions

These instructions are for AI assistants working in this project.

Use the `/trellis:start` command when starting a new session to:
- Initialize your developer identity
- Understand current project context
- Read relevant guidelines

Use `@/.trellis/` to learn:
- Development workflow (`workflow.md`)
- Project structure guidelines (`spec/`)
- Developer workspace (`workspace/`)

If you're using Codex, project-scoped helpers may also live in:
- `.agents/skills/` for reusable Trellis skills
- `.codex/agents/` for optional custom subagents

Keep this managed block so 'trellis update' can refresh the instructions.

<!-- TRELLIS:END -->

# Project Notes

## Project

SmartHelm is STM32F103C8T6 embedded firmware for a smart helmet. It uses STM32CubeMX, STM32 HAL, Keil MDK-ARM V5, and a bare-metal cooperative scheduler.

## Decisions

- Firmware is the current scope; cloud, web, and mini-program layers are not implemented here.
- `APP/` owns application modules; `Core/` is CubeMX-generated; `Drivers/` is vendor code.
- Main loop runs `scheduler_run()`; sensor work is periodic polling, not RTOS.
- Peripheral config source of truth is `helmet.ioc`; regenerate CubeMX code instead of hand-editing generated init code.
- APP hardware mappings belong at the top of the owning module `.c` with module-prefixed macros; headers and callers must not depend on CubeMX `main.h` pin-label macros.
- Cross-module control must use public module APIs, not direct GPIO writes or private state access.
- I2C1 is for MPU6050; I2C2 is for MAX30102. Both run Fast Mode 400 kHz.
- New modules must be registered in `APP/bsp_system.h`, `APP/scheduler.c`, `Core/Src/main.c`, and `MDK-ARM/helmet.uvprojx` when needed.
- Quality gate requires cppcheck clean, `APP/*.h` include guards, UTF-8 without BOM, and LF line endings.

## Key Paths

- `APP/`: application drivers, scheduler, shared firmware utilities.
- `Core/`: CubeMX output; user edits only inside `USER CODE BEGIN/END`.
- `Drivers/`: STM32 HAL/CMSIS; do not edit.
- `MDK-ARM/helmet.uvprojx`: Keil project; add new `.c` files here.
- `MDK-ARM/helmet/`: build output; do not treat as source.
- `.trellis/tasks/<task>/`: task state, PRD, and injected context.
- `.trellis/spec/`: durable implementation rules.
- `.trellis/workspace/`: session journals, not changelog.

## Common Confusions

- `APP/bsp_system.h` is the app include hub; `Core/Inc/*` is generated HAL-facing config.
- `CLAUDE.md` and `README.md` document current architecture; neither replaces source or `.ioc`.
- `helmet.ioc` controls peripherals; `Core/Src/i2c.c` only reflects generated output plus protected user blocks.
- `MDK-ARM/helmet.hex` is firmware output; `MDK-ARM/helmet.uvprojx` is project configuration.

## Long-Term Notes

- STM32F103C8T6 has 64 KB Flash and 20 KB SRAM; keep buffers static and small, avoid `malloc/free`.
- On Windows, verify Git/editor keeps UTF-8 no BOM and LF; `git diff` may warn about CRLF conversion.
- Do not use blocking logs or delays in interrupts or high-frequency tasks.
- Keep APP exported symbols module-prefixed and headers guarded as `MODULE_H`.
- Update `README.md` and `CLAUDE.md` when peripherals, modules, architecture, or workflow change.
- Follow Trellis workflow: read current task/specs before edits, and record sessions after human-tested commits.
