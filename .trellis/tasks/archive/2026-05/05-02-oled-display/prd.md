# ST7735 TFT display module

## Goal

Add a 1.44-inch ST7735 SPI TFT color display module for SmartHelm firmware so the device can show compact runtime status such as sensor values, network state, and warnings without blocking existing scheduler tasks.

Note: the task name still uses `oled-display` for continuity, but the target hardware is a color ST7735 TFT LCD, not an I2C monochrome OLED.

## Requirements

- Add an APP-level ST7735 display module with `st7735_` module-prefixed public APIs.
- Target hardware: 1.44-inch ST7735 SPI TFT color display.
- Resolution: `128x128`.
- Default display background is black.
- Target wiring to be configured later in CubeMX or APP GPIO mapping:
  - `DC` -> `PB1`
  - `OLED_SCL` -> `PB0`
  - `OLED_SDA` -> `PA7`
- Accepted PCB constraint for this implementation round: `BLK`, `CS`, and `RES` remain floating; later hardware debug may wire them if the panel cannot be brought up reliably.
- Firmware must not configure or depend on `BLK`, `CS`, or `RES`.
- Because `PB0` is not a hardware SPI SCK pin on STM32F103, default implementation plan is GPIO software SPI for the specified wiring.
- Hardware SPI is only an alternative if wiring changes to a valid SPI SCK/MOSI pair.
- Use static buffers only; no `malloc` / `free`.
- Do not allocate a full-screen RGB framebuffer; use direct/windowed writes or small line/page buffers.
- Keep display update work in scheduler context, not interrupts.
- SPI/GPIO transfer loops must be bounded and must not run from interrupts.
- Public header must not expose CubeMX pin macros or raw GPIO details.
- Register new files in `APP/bsp_system.h`, `Core/Src/main.c`, `APP/scheduler.c` if periodic refresh is needed, and `MDK-ARM/helmet.uvprojx`.
- Register `st7735_task()` at a `200ms` scheduler period, but only redraw the two DHT11 text lines when values or validity change.
- During this hardware-debug MVP, if `st7735_init()` returns failure, boot may stop in the existing error loop to make display bring-up faults visible. Because this write-only bus has no MISO/readback and no controllable reset pin, the driver cannot reliably detect an absent or failed panel.
- Keep UI ownership inside the display module; other modules should pass data/state through public APIs rather than drawing raw pixels by default.
- Startup must run a visible self-test: black clear, red/green/blue/white color blocks, and `ST7735 OK`.
- Runtime MVP displays real DHT11 data using compact ASCII `asc2_1206` text after hardware testing showed `asc2_1608` was too large for the 128x128 panel:
  - `Temp:25C`
  - `Humi:50%`
  - invalid data displays `Temp:--C` and `Humi:--%`.

## Acceptance Criteria

- [ ] `st7735_init()` configures software SPI GPIO, sends the ST7735 init sequence, clears black, and draws color self-test blocks plus `ST7735 OK`.
- [ ] `st7735_task()` can be called by the cooperative scheduler every `200ms`.
- [ ] Display init sends ST7735 initialization sequence and clears the screen to black.
- [ ] Display clear, text draw with `asc2_1206`, color fill, and DHT11 status screen APIs exist.
- [ ] Drawing APIs clamp or reject coordinates outside `128x128`.
- [ ] Runtime display refresh does not spam high-frequency logs and does not redraw unchanged DHT11 values.
- [ ] Keil project includes all new display source files.
- [ ] README/CLAUDE mention ST7735 GPIO software SPI mapping and the current `CS/RES/BLK` floating constraint.

## Definition of Done

- Keil build expected clean.
- `cppcheck` expected clean for APP changes.
- APP header guard and UTF-8 no BOM + LF checks pass.
- Manual test notes include display init, black clear, text/color render, missing-display behavior, and scheduler coexistence.

## Out of Scope

- No graphics UI framework.
- No LVGL or heavy UI framework in MVP.
- No full-screen RGB565 framebuffer because STM32F103C8T6 SRAM is too small.
- No Chinese labels in MVP; use ASCII text first because the provided Chinese font table is not confirmed cleanly encoded.
- No animation or high-rate full-screen refresh in MVP.
- No changes to 4G cloud UART behavior.

## Technical Approach

- Use ST7735 command/data model with a private low-level write-command/write-data boundary.
- Use the user-provided target pins as software SPI GPIO mapping by default:
  - `ST7735_DC_GPIO_PORT = GPIOB`, `ST7735_DC_PIN = GPIO_PIN_1`
  - `ST7735_SCL_GPIO_PORT = GPIOB`, `ST7735_SCL_PIN = GPIO_PIN_0`
  - `ST7735_SDA_GPIO_PORT = GPIOA`, `ST7735_SDA_PIN = GPIO_PIN_7`
- Do not implement `CS`, `RES/RST`, or `BLK` control because the PCB leaves them floating.
- Init sequence must rely on software reset commands and normal power-on state; no hardware reset pulse is available.
- Since no CS control exists, the display is treated as the only device on this software SPI line.
- Keep all GPIO mappings private at the top of `APP/st7735.c`.
- Use direct drawing primitives:
  - set address window
  - fill rectangle/screen
  - draw character/string
  - status screen helper
- Use RGB565 color constants, with black as the default background.
- Define width/height and init offsets as module-private macros, with default width `128` and height `128`.
- Use bounded transfer loops; if hardware SPI is later selected, use finite HAL timeouts and batch transfers.
- Do not change CubeMX configuration in this round; `APP/st7735.c` configures its GPIO outputs locally.

## Planned Public Contract

```c
uint8_t st7735_init(void);
void st7735_task(void);
uint8_t st7735_clear(void);
uint8_t st7735_fill_screen(uint16_t color);
uint8_t st7735_fill_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint16_t color);
uint8_t st7735_draw_string(uint8_t x, uint8_t y, const char *text, uint16_t color, uint16_t bg_color);
uint8_t st7735_show_dht11_status(void);
uint8_t st7735_is_ready(void);
```

Contract notes:

- `st7735_init()` returns `0` on success and nonzero on internal argument/state failure; physical panel presence cannot be verified without readback.
- `st7735_task()` must return quickly when display is not ready.
- Text APIs must validate coordinates and reject null pointers.
- Color is RGB565.
- Public APIs use the true device prefix `st7735_`; the task name remains `oled-display` only for continuity.

## Decision Tree

- Controller: confirmed ST7735.
- Resolution: confirmed `128x128`.
- Init offsets/tab variant: keep configurable at module top because ST7735 128x128 modules can still differ by panel offset.
- Bus: specified pins imply software SPI; hardware SPI requires different SCK wiring.
- Control pins: `BLK`, `CS`, and `RES` are floating by fixed PCB design; firmware will skip these controls and verify real hardware behavior.
- Refresh: `200ms` dirty refresh for DHT11 text, not continuous full-screen redraw.
- Content MVP: ASCII DHT11 temperature/humidity page first; sensor dashboard and warnings later if needed.

## Manual Verification Plan

- Power on with ST7735 connected: init succeeds, clears screen black, and displays a known boot/status string.
- Power on with ST7735 disconnected: firmware cannot reliably detect absence on the write-only bus; use the visible self-test and serial `[ST7735] init=0` log as the bring-up gate.
- On the fixed PCB with `BLK/CS/RES` floating, display must reliably show visible black clear and text after at least 5 cold boots.
- Draw tests cover `(0,0)`, `(127,0)`, `(0,127)`, and `(127,127)` boundaries without wrapping into invalid memory windows.
- Display update task runs for at least two full refresh cycles without blocking sensor tasks.
- Search confirms no `HAL_MAX_DELAY`, unbounded wait, or interrupt-side display transfer in display path.

## Research Notes

- ST7735/ST7735S supports serial interfaces and uses a command/data distinction; in 4-wire serial mode the D/CX signal selects command versus parameter/RAM data. Datasheet sources document CS active-low behavior and reset as active-low. This project accepts the existing PCB constraint that `BLK`, `CS`, and `RES` are floating, so the risk is handled by explicit hardware verification rather than firmware control. Sources: [DisplayFuture ST7735 Datasheet, 2010, Module I/O Pins](https://www.displayfuture.com/Display/datasheet/controller/ST7735.pdf), [Phoenix Display ST7735S Datasheet, 2011, Module I/O Pins / Serial Interface](https://phoenixdisplay.com/wp-content/uploads/2018/05/ST7735S_V1.1_20111121.pdf).
- Common ST7735 breakout guidance treats `CS`, `DC`, and `RST` as explicit control pins when available, and uses black as a normal background color in display examples. Source: [XOD, ST7735 display guide](https://xod.io/docs/guide/st7735-display/).
- Practical STM32 examples commonly initialize GPIO/SPI, initialize ST7735, clear/fill the screen black, then draw strings with foreground/background colors. Source: [ControllersTech, STM32 ST7735 TFT Display Interface](https://controllerstech.com/st7735-1-8-tft-display-with-stm32/).
- ST community discussion on SPI DMA for ST7735 highlights that queued DMA transfers must wait for completion or be driven by completion callbacks; for this MVP, direct bounded writes or carefully serialized transfers are safer than uncontrolled DMA queuing. Source: [STMicroelectronics Community, 2024, SPI with DMA and TFT Display ST7735](https://community.st.com/t5/stm32-mcus-embedded-software/spi-with-dma-and-tft-display-st7735/td-p/674378).

## Relevant Context

- `.trellis/spec/backend/directory-structure.md`: new module registration.
- `.trellis/spec/backend/quality-guidelines.md`: APP module style and CubeMX boundaries.
- `.trellis/spec/backend/database-guidelines.md`: static data and scheduler nonblocking rules.
- Existing I2C modules: `APP/mpu6050.c`, `APP/max30102.c`.
- Existing scheduler integration: `APP/scheduler.c`.

## Open Questions

- None for OLED/ST7735 planning; remaining panel offset/tab variant can be handled by implementation-time macros and hardware verification.
