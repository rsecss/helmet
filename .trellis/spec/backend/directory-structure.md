# Directory Structure

> How firmware code is organized in this project.

---

## Overview

SmartHelm 采用三层架构：**感知层（STM32 固件）** → **传输层（UART + 4G DTU）** → **应用层（云端 + Web/小程序）**。

**当前文档覆盖范围：感知层（STM32F103C8T6 嵌入式固件）**。传输层、应用层代码尚未开始，在相应代码落地时再扩展本文档。

感知层代码分为三类，严格分离：

| 目录 | 归属 | 修改规则 |
|------|------|----------|
| `Core/` | CubeMX 自动生成 | **不要手动修改**；改动必须通过 `helmet.ioc` 在 CubeMX 中重新生成 |
| `APP/` | 应用层业务代码 | 所有新增模块放这里 |
| `Drivers/` | HAL 库 + CMSIS | **禁止手动修改**，由 ST 官方维护 |
| `MDK-ARM/` | Keil 工程文件 + 启动文件 | 新增源文件需在此登记（工程配置） |

---

## Directory Layout

```
helmet/
├── APP/                              # 应用层业务代码（新模块放这里）
│   ├── bsp_system.h                  # 应用层统一头文件入口（聚合所有模块头）
│   ├── scheduler.c / .h              # 协作式任务调度器
│   ├── ringbuffer.c / .h             # 通用环形缓冲区
│   ├── mq2.c / .h                    # MQ2 烟雾传感器（ADC+DMA）
│   ├── dht11.c / .h                  # DHT11 温湿度传感器（单总线）
│   ├── mpu6050.c / .h                # MPU6050 六轴姿态传感器（I2C+DMP）
│   ├── mpu6050_inv_mpu*.c / .h       # InvenSense 官方驱动（精简版）
│   ├── m100pg.c / .h                 # M100PG 4G+GPS 模块（UART2 DMA）
│   └── gps.c / .h                    # NMEA 协议 GPS 解析
├── Core/                             # CubeMX 生成（外设初始化、中断、时钟）
│   ├── Inc/                          # *.h：main.h / gpio.h / usart.h / i2c.h / ...
│   └── Src/                          # *.c：main.c / gpio.c / usart.c / ...
├── Drivers/
│   ├── STM32F1xx_HAL_Driver/         # STM32 HAL 库
│   └── CMSIS/                        # ARM CMSIS 头文件
├── MDK-ARM/
│   ├── helmet.uvprojx                # Keil 工程文件
│   ├── startup_stm32f103xb.s         # 启动文件
│   └── helmet/                       # 编译产物（.hex, .axf, .map）
├── helmet.ioc                        # STM32CubeMX 工程配置
├── Keilkill.bat                      # 清理中间文件脚本
└── .github/workflows/                # CI 质量门禁 + 自动发布
```

---

## Module Organization

**新增应用模块的标准步骤：**

1. 在 `APP/` 下创建 `module_name.c` 和 `module_name.h`
2. 头文件模板：
   ```c
   #ifndef MODULE_NAME_H
   #define MODULE_NAME_H

   #include "bsp_system.h"

   #ifdef __cplusplus
   extern "C" {
   #endif

   void module_name_init(void);   // 初始化函数
   void module_name_task(void);   // 调度器周期任务（可选）

   #ifdef __cplusplus
   }
   #endif

   #endif /* MODULE_NAME_H */
   ```
3. 在 `APP/bsp_system.h` 的"应用层头文件"区块 `#include "module_name.h"`
4. 在 Keil 工程（`helmet.uvprojx`）中添加源文件 + 头文件路径
5. 在 `main()` 的 `USER CODE BEGIN 2` 内调用 `module_name_init()`
6. 如需周期执行，在 `APP/scheduler.c` 的 `scheduler_task[]` 数组追加 `{module_name_task, 周期ms, 0}`

**单一职责：** 每个模块对应一个硬件设备或一个功能（如 `mpu6050` 只负责姿态解算，`gps` 只负责 NMEA 解析）。

**移植边界：** 模块硬件映射必须集中在本模块 `.c` 顶部，包括 GPIO 端口、引脚、有效电平、默认状态等板级差异；`.h` 只暴露业务接口和状态读取接口，不暴露 `GPIOx`、`GPIO_PIN_x` 或 CubeMX 生成的 `*_Pin` / `*_GPIO_Port` 标签名。调用方只能通过公开接口访问模块能力，不能直接操作其他模块的 GPIO 或私有状态。

**CubeMX 用户代码边界：** 所有写在 `Core/` 下的用户代码**必须**位于 `USER CODE BEGIN/END` 成对标记之间，否则 CubeMX 重新生成时会被覆盖。

---

## Naming Conventions

| 类别 | 规则 | 示例 |
|------|------|------|
| 文件名 | 小写 + 下划线，`.c` / `.h` 成对 | `mpu6050.c`, `m100pg.h` |
| 函数名 | `模块名_动词_对象` snake_case | `mpu6050_dmp_get_data`, `ringbuffer_write` |
| 变量名 | snake_case | `last_run_time`, `fall_flag` |
| 全局变量 | 简短但有区分性，.h 用 `extern` 声明、.c 定义 | `pitch`, `aacx`, `usart_rx_dma_buffer` |
| 宏 / 常量 | UPPER_SNAKE_CASE | `RINGBUFFER_SIZE`, `MPU6050_ADDR`, `DEFAULT_MPU_HZ` |
| 类型定义 | snake_case + `_t` 后缀 | `task_t`, `gps_data_t`, `ringbuffer_t` |
| 头文件守卫 | 文件名大写 + `_H` | `MPU6050_H`, `SCHEDULER_H` |

**模块前缀强制：** 所有导出函数/全局变量必须以模块名前缀开头（`mpu6050_*`, `scheduler_*`, `ringbuffer_*`），避免命名冲突。

---

## Examples

**调度器任务模板** — `APP/scheduler.c`：
```c
static task_t scheduler_task[] = {
    {mpu6050_task, 10, 0},     // 10ms：姿态解算（实时性最高）
    {mq2_task,     100, 0},    // 100ms：烟雾浓度
    {m100pg_task,  20, 0},     // 20ms：GPS NMEA 解析
    {dht11_task,   1000, 0},   // 1000ms：温湿度（采样周期较长）
};
```

**典型模块代码布局** — 参考 `APP/mpu6050.c`：
```c
/* 1. include 本模块头文件 + 依赖 */
#include "mpu6050.h"
#include "mpu6050_inv_mpu.h"

/* 2. 外部 HAL 句柄 */
extern I2C_HandleTypeDef hi2c1;

/* 3. 模块全局状态（与 .h 的 extern 声明对应） */
float pitch, roll, yaw;

/* 4. 私有常量与静态辅助函数（static） */
#define Q30 1073741824.0f
static unsigned short inv_row_2_scale(const signed char *row) { ... }

/* 5. 导出函数：init / task / 数据获取 */
void mpu6050_init(void) { ... }
void mpu6050_task(void) { ... }
```

**推荐参考模块：**
- `APP/scheduler.c` — 简洁的调度器，入门阅读
- `APP/mpu6050.c` — 带 DMP 姿态解算、原始数据读取、任务函数的完整模块
- `APP/ringbuffer.c` — 独立无依赖的通用数据结构

---

## Scenario: ST7735 GPIO Software SPI Display Module

### 1. Scope / Trigger

Use this contract when adding or modifying a small ST7735/ST7735S RGB TFT display on the STM32F103 application layer, especially when the PCB uses GPIO bit-banged SPI instead of a CubeMX hardware SPI peripheral.

### 2. Signatures

`APP/st7735.h` public API:

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

Current board mapping in `APP/st7735.c`:

```c
#define ST7735_DC_GPIO_PORT         GPIOB
#define ST7735_DC_PIN               GPIO_PIN_1
#define ST7735_SCL_GPIO_PORT        GPIOB
#define ST7735_SCL_PIN              GPIO_PIN_0
#define ST7735_SDA_GPIO_PORT        GPIOA
#define ST7735_SDA_PIN              GPIO_PIN_7
#define ST7735_X_OFFSET             2U
#define ST7735_Y_OFFSET             3U
#define ST7735_MADCTL_VALUE         0xC0U
#define ST7735_FONT_WIDTH           6U
#define ST7735_FONT_HEIGHT          12U
```

### 3. Contracts

- `CS`, `RES/RST`, and `BLK` are not firmware-controlled in the current wiring. If the panel stays white or does not receive commands, first wire `CS` to `GND` and `RES/RST` to `3.3V`; if the backlight is dark, wire `BLK` to `3.3V`.
- The public header must not expose GPIO ports, pins, ST7735 command bytes, MADCTL values, offsets, or font table internals.
- `APP/st7735.c` owns all display drawing and hardware bit-banging. Other modules call public `st7735_*` APIs and must not write the display GPIO directly.
- `st7735_init()` may run a visible hardware bring-up self-test, but `st7735_task()` must return quickly and only redraw dirty text regions.
- The display path must not allocate a full-screen RGB565 framebuffer. Use direct/windowed writes such as `st7735_set_address_window()` plus bounded pixel loops.
- The current status page uses ASCII `asc2_1206`. This font table's pixel bits are read low-bit first with `(0x01U << col)`, not high-bit first, otherwise characters display mirrored.
- This write-only bus cannot verify real panel presence. Treat `[ST7735] init=0` plus visible color blocks/text as the practical bring-up gate.

### 4. Validation & Error Matrix

| Symptom | First check | Required validation |
|---------|-------------|---------------------|
| Serial shows `[ST7735] init=0`, screen remains white | `CS` / `RES` wiring | Connect `CS=GND`, `RES/RST=3.3V`, power-cycle, confirm color blocks appear |
| Backlight is dark | `BLK` wiring | Connect `BLK=3.3V` and verify the panel is lit |
| Side or bottom has colored noise | `ST7735_X_OFFSET`, `ST7735_Y_OFFSET` | Tune offsets at the module top and verify black clear reaches visible edges |
| Whole screen direction is upside down | `ST7735_MADCTL_VALUE` | Try only MADCTL values before changing font drawing or coordinates |
| Individual characters are mirrored | font bit order | For `asc2_1206`, read bits as `(0x01U << col)` and verify `Temp` / `Humi` render normally |
| DHT11 init fails | DHT11 hardware path | Display should show `Temp:--C` / `Humi:--%`; this is not an ST7735 failure |
| 4G downlink becomes unresponsive | display task too heavy | Ensure `st7735_task()` dirty-checks values and does not full-screen redraw every 200ms |

### 5. Good/Base/Bad Cases

**Good**: `st7735_task()` compares cached DHT11 value/valid state, redraws only changed `Temp` / `Humi` lines, and exits immediately when unchanged.

**Base**: `st7735_init()` sends the ST7735 command sequence, clears black, shows red/green/blue/white blocks, displays `ST7735 OK`, then clears before showing the runtime page.

**Bad**: A scheduler task clears and redraws the whole 128x128 screen every 200ms, or a caller outside `APP/st7735.c` toggles `PB0` / `PA7` / `PB1` directly.

### 6. Tests Required

- Build with Keil and confirm no C compile errors after adding `APP/st7735.c` to `MDK-ARM/helmet.uvprojx`.
- Serial boot log must include `[ST7735] init=0` and `[BOOT] scheduler ready`.
- Hardware must show the boot color self-test and then a readable ASCII page.
- With DHT11 invalid, display must show `Temp:--C` and `Humi:--%`.
- With DHT11 valid, display must update to numeric values such as `Temp:25C` and `Humi:50%`.
- Search the display module for forbidden runtime patterns: `HAL_MAX_DELAY`, `0xFFFFFF`, `malloc`, `free`, and `printf`.
- Run `git diff --check` and APP header guard / UTF-8 no BOM / LF checks before commit.

### 7. Wrong vs Correct

#### Wrong

```c
/* High-bit-first rendering mirrors asc2_1206 characters on the tested panel/font table. */
if ((glyph_row & (uint8_t)(0x20U >> col)) != 0U) {
    st7735_write_color(color);
}
```

#### Correct

```c
/* asc2_1206 rows are consumed low-bit first for this display path. */
if ((glyph_row & (uint8_t)(0x01U << col)) != 0U) {
    st7735_write_color(color);
}
```
