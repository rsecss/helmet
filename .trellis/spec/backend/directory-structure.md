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
