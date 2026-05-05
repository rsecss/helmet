# Journal - Maple (Part 1)

> AI development session journal
> Started: 2026-04-19

---

## Session 1: 项目初始化，搭建项目框架

**Date**: 2026-03-05
**Task**: 项目初始化，搭建项目框架
**Branch**: `main`

### Summary

搭建基于 STM32F103C8T6 的嵌入式工程骨架，包含 CubeMX 外设配置、Keil MDK-ARM 工程、串口调试和任务调度器基础设施，命名为 SmartHelm 并创建 GitHub 远程仓库。

### Main Changes

- 搭建基于 STM32F103C8T6 的 CubeMX + Keil MDK-ARM 工程骨架
- 实现基于 SysTick 的轮询协作任务调度器（`APP/scheduler.c`）
- 配置 USART1 串口调试（115200-8N1）及 `printf` 重定向（`Core/Src/usart.c` `fputc`）
- 创建项目文档：`README.md`、`CLAUDE.md`、`.gitignore`
- 项目更名为 SmartHelm，创建 GitHub 远程仓库

### Git Commits

| Hash | Message |
|------|---------|
| `86a9534` | feat: 初始化 SmartHelm 智能头盔项目框架 |

### Testing

- [OK] 串口回显正常（USART1 PA9/PA10 @ 115200）
- [OK] 调度器空任务列表不阻塞主循环

### Status

[OK] **Completed**

### Next Steps

- 陆续接入各类传感器模块

---

## Session 2: 新增 MQ2 烟雾传感器模块

**Date**: 2026-03-06
**Task**: 新增 MQ2 烟雾传感器模块
**Branch**: `main`

### Summary

集成 MQ2 模拟量烟雾传感器，使用 ADC1 + DMA 循环采样 30 点取均值，换算丙烷浓度（ppm），注册 100ms 周期任务。

### Main Changes

- 新增 `APP/mq2.c`，实现 ADC+DMA 方式采集 MQ2 模拟信号
- 配置 ADC1 通道 0（PA0），DMA 循环采样 30 个数据点取均值
- 基于 RS/R0 比值换算丙烷浓度（ppm）
- 注册 `mq2_task` 到调度器，100ms 执行周期
- 同时保留常规轮询读取方式 `mq2_task1` 作为备选

### Git Commits

| Hash | Message |
|------|---------|
| `c77edde` | feat(mq2): 新增 MQ2 烟雾传感器检测模块 |

### Testing

- [OK] 常温空气环境下 ADC 值稳定在基线附近
- [OK] 接触打火机气体时 ppm 读数明显上升

### Status

[OK] **Completed**

### Next Steps

- 继续接入其他传感器模块

---

## Session 3: 新增 DHT11 温湿度传感器模块

**Date**: 2026-03-06
**Task**: 新增 DHT11 温湿度传感器模块（含时钟修正与 TIM1 微秒延时）
**Branch**: `main`

### Summary

接入 DHT11 单总线温湿度传感器，同步修正系统时钟至 72MHz，引入 TIM1 实现微秒级延时函数。后续将 DHT11 位带操作重构为纯 HAL API。

### Main Changes

- 修正系统时钟配置：PLL 倍频从 ×2 改为 ×9，系统主频从 16MHz 提升至 72MHz（Flash 等待周期同步调整为 2）
- 新增 `APP/sys.h`，提供位带操作宏（`PAout`/`PAin` 等），实现类 51 单片机式 GPIO 位操作
- 新增 TIM1 外设（预分频 72-1，向上计数至 65535），用于实现微秒级延时函数 `Delay_us()`
- 新增 `APP/dht11.c` / `APP/dht11.h`，实现 DHT11 单总线时序通信（复位 → 响应检测 → 逐位读取 → 校验和验证）
- DHT11 数据引脚配置为 PA8（推挽输出，高速）
- 注册 `dht11_task` 到调度器，1000ms 执行周期，串口打印温湿度数据
- MQ2 任务暂时注释，仅保留 DHT11 任务用于调试
- `bsp_system.h` 新增 `tim.h`、`sys.h`、`dht11.h` 头文件引用
- 后续重构：DHT11 驱动改用纯 HAL API 替代位带操作，移除对 `sys.h` 的依赖

### Git Commits

| Hash | Message |
|------|---------|
| `7ba2525` | feat(dht11): 新增 DHT11 温湿度传感器检测模块 |
| `c7825ba` | docs: 更新文档，补充 DHT11 模块与 TIM1 外设配置信息 |
| `c38070c` | refactor(dht11): 重构 DHT11 驱动，使用纯 HAL API 替代位带操作 |

### Testing

- [OK] 室温 25°C / 湿度 50% 环境读数与参考温湿度计一致（±1°C / ±3%RH）
- [OK] 串口输出每秒刷新温湿度值

### Status

[OK] **Completed**

### Next Steps

- 接入 MPU6050 姿态传感器

---

## Session 4: 新增 MPU6050 六轴姿态传感器模块

**Date**: 2026-03-06
**Task**: 新增 MPU6050 六轴姿态传感器模块（DMP 姿态解算）
**Branch**: `main`

### Summary

接入 MPU6050 通过 I2C1 通信，集成 InvenSense DMP 运动处理库实现 pitch/roll/yaw 姿态解算，注册 10ms 高频周期任务。

### Main Changes

- 在 CubeMX 中配置 I2C1 外设（PB6-SCL / PB7-SDA，Fast Mode 400kHz），启用 `HAL_I2C_MODULE`
- 新增 `APP/mpu6050.c` / `APP/mpu6050.h`，实现 MPU6050 初始化、I2C 读写、温度/陀螺仪/加速度计数据获取
- 集成 InvenSense DMP 运动处理库（`mpu6050_inv_mpu.*`、`mpu6050_inv_mpu_dmp_motion_driver.*`），实现 pitch/roll/yaw 姿态解算
- 注册 `mpu6050_task` 到调度器，10ms 执行周期
- `bsp_system.h` 新增 `i2c.h`、`mpu6050.h` 及 DMP 库头文件引用

### Git Commits

| Hash | Message |
|------|---------|
| `31c8468` | feat(mpu6050): 新增 MPU6050 六轴姿态传感器模块 |
| `19b17d0` | docs: 更新文档，补充 MPU6050 模块与 I2C1 外设信息 |

### Testing

- [OK] 静置时 pitch/roll/yaw 输出稳定在 0° 附近（±0.5°）
- [OK] 倾斜传感器时欧拉角输出符合物理方向

### Status

[OK] **Completed**

### Next Steps

- 精简 InvenSense 驱动，去除冗余代码

---

## Session 5: 重构 MPU6050 模块，精简 InvenSense 驱动

**Date**: 2026-03-06
**Task**: 重构 MPU6050 模块，功能不变前提下最大化精简代码
**Branch**: `main`

### Summary

保持 DMP 姿态解算、原始数据读取、跌倒检测功能完全不变，通过删除冗余 API、条件编译分支、未用宏定义，将模块从 8 文件精简到 6 文件，净删除约 1700 行代码（约 37%）。

### Main Changes

**应用层（`mpu6050.c` / `.h`）：**

- 删除冗余函数：`MPU_Init`（被 DMP 初始化覆盖）、`MPU_Write/Read_Byte`、`MPU_Get_Temperature`、`MPU_Write/Read_Len`
- 删除未使用的全局变量：`i`、`walk`、`steplength`、`Distance`、`svm_set`、`collision_flag`
- 删除未使用的功能函数：`dmp_getwalk`（步数检测）、`dmp_svm`（振动计算）
- 合并 `MPU_Init()` + `mpu_dmp_init()` 为单一入口 `mpu6050_init()`
- I2C 数据读取改用 `HAL_I2C_Mem_Read` 替代 `HAL_I2C_Master_Transmit/Receive`
- 寄存器宏从 67 个精简至 3 个（`MPU6050_ADDR`、`MPU6050_ACCEL_XOUTH_REG`、`MPU6050_GYRO_XOUTH_REG`）
- 从 `inv_mpu.c` 迁入自检与 DMP 初始化逻辑（`run_self_test`、`inv_row_2_scale` 等），标记为 `static`

**InvenSense 驱动层（`mpu6050_inv_mpu.c` / `.h`）：**

- 删除全部 `#ifdef AK89xx_SECONDARY`（磁力计）和 `#ifdef MPU6500` 条件编译代码
- 删除约 20 个未使用的公开 API（`mpu_reg_dump`、`mpu_lp_accel_mode`、`mpu_get_temperature` 等）
- 将 7 个仅内部调用的函数改为 `static`（`mpu_set_bypass`、`mpu_get_gyro_fsr` 等）
- `mpu_get_accel_fsr` 保持公开（DMP 驱动外部调用）

**DMP 驱动层（`mpu6050_inv_mpu_dmp_motion_driver.c` / `.h`）：**

- 删除 9 个未使用函数（计步器、回调注册、中断模式等）
- `dmp_enable_gyro_cal`、`dmp_enable_lp_quat`、`dmp_enable_6x_lp_quat` 改为 `static` 并添加前向声明

**文件清理：**

- 删除 `mpu6050_dmp_key.h`（437 个宏仅用 30 个）和 `mpu6050_dmp_map.h`（247 个宏仅用 4 个）
- 34 个实际使用的宏内联至 `mpu6050_inv_mpu_dmp_motion_driver.c` 顶部
- 从 Keil 工程文件（`.uvprojx` / `.uvoptx`）中移除已删文件引用
- `bsp_system.h` 移除对 `dmp_key` / `dmp_map` 头文件的 include

**结果：** 模块从 8 文件缩减至 6 文件，净删除约 1700 行代码（约 37%），消除 650 个未使用宏定义。

### Git Commits

| Hash | Message |
|------|---------|
| `cbea9df` | refactor(mpu6050): 重构 MPU6050 模块，精简 InvenSense 驱动 |
| `8dfc4ac` | docs: 更新文档，同步 MPU6050 模块重构变更 |

### Testing

- [OK] 精简前后 pitch/roll/yaw 输出数值完全一致
- [OK] DMP 自检、跌倒标志、向量模值（AVM/GVM）功能均正常
- [OK] Keil 工程编译通过，ROM/RAM 占用均下降

### Status

[OK] **Completed**

### Next Steps

- 补齐 CI 质量门禁，防止后续改动引入回归

---

## Session 6: 新增 CI/CD 质量门禁与自动发布

**Date**: 2026-03-06
**Task**: 新增 CI 工作流，覆盖质量门禁与自动发布
**Branch**: `main`

### Summary

建立 GitHub Actions 质量门禁（cppcheck + 头文件守卫 + 编码规范）和自动发布（git-cliff 生成 changelog + GitHub Release）两条工作流，并针对项目实际触发路径限制生效范围。

### Main Changes

- 新增 `.github/workflows/quality.yml`：push main/dev 和 PR main 触发质量门禁
  - cppcheck 静态分析扫描 `APP/`，覆盖 HAL / CMSIS include 路径（warning / performance / portability 级别）
  - 头文件守卫检查：验证 `APP/*.h` 包含 `#ifndef FILENAME_H`
  - 编码规范检查：检测 UTF-8 BOM 和 CRLF 行尾
- 新增 `.github/workflows/release.yml`：推送 `v*` 标签触发自动发布
  - 使用 git-cliff 基于 Conventional Commits 生成中文分组 + emoji 标题的 changelog
  - 通过 GitHub Release 发布，不附带固件产物
- 抑制 cppcheck 对 CMSIS 编译器检测的误报（`--suppress=preprocessorErrorDirective`）
- 限制质量门禁触发路径：仅在 `APP/**`、`README.md`、`CLAUDE.md` 变更时触发，避免文档/配置小改动都跑 CI
- 更新 `README.md` 添加 CI 徽章，更新 `CLAUDE.md` 补充 CI/CD 约定

### Git Commits

| Hash | Message |
|------|---------|
| `30f92ce` | ci: 新增质量门禁与自动发布工作流 |
| `95a9dca` | fix(ci): 抑制 cppcheck 对 CMSIS 编译器检测的误报 |
| `f82d129` | refactor(ci): 限制质量门禁仅在 APP 代码和项目文档变更时触发 |

### Testing

- [OK] 首次 push 触发质量门禁工作流通过
- [OK] 试推 `v0.1.0-rc` 标签，release 工作流生成 changelog 并创建 Release
- [OK] 修改非 APP 路径验证确实不触发质量门禁

### Status

[OK] **Completed**

### Next Steps

- 后续新增模块应沿用当前 CI 约束，保持编码规范与静态分析零告警


## Session 7: 完成 MAX30102 心率血氧模块

**Date**: 2026-04-28
**Task**: 完成 MAX30102 心率血氧模块
**Branch**: `dev`

### Summary

新增 MAX30102 驱动与调度集成，移植并修复心率/血氧解算稳定性问题，更新项目文档和质量规范；用户实机测试确认通过。

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `3b0f8f8` | (see git log) |
| `51a993f` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 8: 4G DTU 基础透传链路

**Date**: 2026-04-29
**Task**: 4G DTU 基础透传链路
**Branch**: `dev`

### Summary

完成 M100PG 4G DTU 基础通信链路：接入 USART2 115200-8N1、RX DMA 空闲中断、ring buffer 缓存、USART1 调试转发和 m100pg_send_bytes 底层发送接口；同步 README/CLAUDE、helmet.ioc 与 Keil 工程配置。后续继续做正式上传封包和云端下发解析。

### Main Changes

(Add details)

### Git Commits

| Hash | Message |
|------|---------|
| `774c2dd` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 9: 新增三色 LED 模块

**Date**: 2026-04-29
**Task**: 新增三色 LED 模块
**Branch**: `dev`

### Summary

(Add summary)

### Main Changes

| 项目 | 内容 |
|------|------|
| 三色 LED 模块 | 新增 `APP/rgb_led.c/.h`，封装共阴 RGB LED 的颜色、开关、状态读取和 1000ms 白色闪烁测试任务。 |
| 硬件映射 | PB12=R、PB13=G、PB14=B，公共端接地，共阴极，高电平点亮，默认低电平熄灭。 |
| 调度与初始化 | 在 `APP/bsp_system.h` 引入模块，在 `APP/scheduler.c` 注册 `rgb_led_task()`，在 `Core/Src/main.c` 调用 `rgb_led_init()`。 |
| CubeMX/Keil | `helmet.ioc` 增加 PB12/PB13/PB14 GPIO Output；`MDK-ARM/helmet.uvprojx` 纳入 RGB LED 源文件。 |
| 规则沉淀 | 更新 `AGENTS.md`、`CLAUDE.md`、`.trellis/spec/backend/*`，明确模块硬件映射集中在模块 `.c` 顶部，调用方不依赖 CubeMX `main.h` 标签宏。 |
| 4G 前置关系 | 更新 4G 任务 PRD，记录 4G Phase 2 前先完成三色 LED 模块，后续 `m100pg` 只调用 LED 公开接口。 |

**提交**：
- `4b4fe05 feat(rgb-led): 新增三色 LED 模块`
- `a6bb8ef chore(task): 归档 04-29-tri-color-led`

**验证**：
- 已通过 `git diff --check`。
- 已通过 `APP/*.h` 头文件守卫检查。
- 已通过 UTF-8 无 BOM + LF 行尾扫描。
- 已确认无 `led_r_Pin` / `led_g_Pin` / `led_b_Pin` 等 CubeMX LED 标签宏残留。
- 用户已完成实机测试，功能正常：上电默认灭，1 秒白色亮/灭循环。
- `cppcheck` 未运行：本机未安装 `cppcheck`。
- Keil 命令行编译未运行：本机未找到 `UV4/UV4.exe`；用户已完成实际硬件测试。

**任务状态**：
- `04-29-tri-color-led` 已归档到 `.trellis/tasks/archive/2026-04/04-29-tri-color-led`。
- 4G Phase 2 后续可基于 `rgb_led_set_enabled()` / `rgb_led_get_enabled()` 接入云端下发控制和上传状态字段。

**未提交文件**：
- `MDK-ARM/helmet.uvoptx`
- `MDK-ARM/helmet/helmet.hex`

这两个为 Keil 用户配置/构建产物，已刻意留在工作区未提交。


### Git Commits

| Hash | Message |
|------|---------|
| `4b4fe05` | (see git log) |
| `a6bb8ef` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 10: PWM 电机驱动模块（TB6612FNG A 通道）

**Date**: 2026-05-04
**Task**: PWM 电机驱动模块（TB6612FNG A 通道）
**Branch**: `feature/pwm-motor`

### Summary

(Add summary)

### Main Changes

| Item | Description |
|------|-------------|
| 新模块 | `APP/pwm_motor.c/h` 封装 TB6612FNG A 通道驱动 |
| 公开接口 | `pwm_motor_init/stop/set_speed/set_direction/set_signed_speed` |
| 时基 | TIM3_CH1 部分重映射 → PB4，Period=3599，Prescaler=0 |
| 控制脚 | PA12=AIN1，PA11=AIN2，PB15=STBY |
| 安全策略 | 默认进入安全停止；方向切换先归零再换向再恢复速度；速度百分比超 100 自动夹断 |
| 启动顺序 | `MX_TIM3_Init` → `pwm_motor_init()`（main.c 在 `MX_USART2_UART_Init` 之后插入） |

**新增/更新文件**:
- `APP/pwm_motor.c`、`APP/pwm_motor.h`（模块实现与公开接口）
- `APP/bsp_system.h`（统一引入）
- `Core/Src/main.c`（外设初始化后调用 `pwm_motor_init`）
- `Core/Src/tim.c`、`Core/Inc/tim.h`（CubeMX 生成 TIM3_CH1 PWM）
- `Core/Src/gpio.c`（PA11/PA12/PB15 输出配置）
- `helmet.ioc`（启用 TIM3、PB4 重映射、新增方向/STBY 引脚）
- `MDK-ARM/helmet.uvprojx`、`helmet.uvoptx`、`helmet.hex`（Keil 工程纳入新源文件并重新编译）
- `README.md`、`CLAUDE.md`（外设映射表 + 执行流程同步）

**任务归档**:
- `.trellis/tasks/05-02-pwm-motor` → `archive/2026-05/`

**验证**:
- 板上实测：duty 0/50/100 风扇响应正常，启动后保持安全停止
- Keil 构建生成 `helmet.hex` 已纳入提交


### Git Commits

| Hash | Message |
|------|---------|
| `3b935b3` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 11: ST7735 彩色显示屏模块

**Date**: 2026-05-05
**Task**: ST7735 彩色显示屏模块
**Branch**: `dev`

### Summary

(Add summary)

### Main Changes

| Item | Details |
|------|---------|
| Feature | Added APP-level ST7735 color TFT display module for 1.44-inch 128x128 panel using GPIO software SPI. |
| Wiring | PB0=SCL, PA7=SDA, PB1=DC; CS/RES/BLK kept outside firmware control for this round. |
| Display | Boot self-test shows color blocks and ST7735 OK, then clears and displays DHT11 Temp/Humi ASCII page. |
| Debug Results | Hardware testing fixed white screen/control-pin assumptions, panel offset noise, orientation, font size, and mirrored character bit order. |
| Docs | Updated README, CLAUDE, OLED PRD, and backend directory-structure spec with executable ST7735 contracts and validation matrix. |
| Verification | Keil build/download and human hardware test passed; task context validation, git diff --check, header guard, encoding, and forbidden-pattern checks passed. cppcheck was not run locally because it is not installed. |

**Updated Files**:
- `APP/st7735.c`
- `APP/st7735.h`
- `APP/lcd_font_lib.h`
- `APP/bsp_system.h`
- `APP/scheduler.c`
- `Core/Src/main.c`
- `MDK-ARM/helmet.uvprojx`
- `MDK-ARM/helmet/helmet.hex`
- `README.md`
- `CLAUDE.md`
- `.trellis/spec/backend/directory-structure.md`
- `.trellis/tasks/archive/2026-05/05-02-oled-display/`


### Git Commits

| Hash | Message |
|------|---------|
| `e0e65b2` | (see git log) |
| `85a9fd5` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 12: LCD 传感器状态页

**Date**: 2026-05-05
**Task**: LCD 传感器状态页
**Branch**: `dev`

### Summary

Completed the ST7735 LCD sensor status page and documented the LCD Chinese font/Keil encoding lessons learned.

### Main Changes

- Added `APP/lcd_app.c` / `APP/lcd_app.h` as the hardware-specific display application layer for six sensor lines: temperature, humidity, smoke concentration, heart rate, SpO2, and attitude angles.
- Kept `APP/st7735.c` as a reusable low-coupling display driver and moved sensor-specific page logic out of the ST7735 module.
- Added project-specific 12x12 Chinese glyph support with UTF-8 byte keys and UTF-8 hex escaped LCD labels to avoid Keil source charset failures.
- Registered the new LCD app module in `APP/bsp_system.h`, `APP/scheduler.c`, `Core/Src/main.c`, and `MDK-ARM/helmet.uvprojx`.
- Updated README, CLAUDE, and backend specs for the new LCD module boundary and Chinese font validation rules.

### Testing

- [OK] User confirmed Keil build succeeded.
- [OK] User confirmed hardware LCD display test succeeded.
- [OK] `git diff --check HEAD~2..HEAD` passed.
- [OK] ST7735 driver no longer contains direct DHT11/MQ2/MAX30102/MPU6050 sensor coupling.
- [OK] LCD text/font files contain no known mojibake markers checked during review.
- [OK] APP header guards passed for touched headers.
- [WARN] Local `cppcheck` command is not installed, so full static analysis was not run locally.

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 13: 归档 4G 云端联调任务

**Date**: 2026-05-05
**Task**: 归档 4G 云端联调任务
**Branch**: `dev`

### Summary

(Add summary)

### Main Changes

### Summary

确认 4G 云端联调任务实际功能已全部完成（含 Web 端实机联调与上电默认 LED 状态修复），代码复核 PRD 剩余 2 个 AC 已在代码层实现防御逻辑，更新 PRD 与 `task.json` 后归档任务到 `archive/2026-05/`。

### Main Changes

| Item | Details |
|------|---------|
| Code Review | 复核 `APP/m100pg.c` 与 `APP/m100pg_protocol.c`，确认非法/不完整命令拒绝（`memcmp` 严格等长匹配 + 未知命令落 `on_unknown` 不触发控制）和 RingBuffer 中断侧溢出截断（满则置 `rx_overflow` 并 `break`，不阻塞中断；`m100pg_proto_feed` 溢出后跳至下一 `\n` 丢弃半截数据）已实现 |
| PRD | 勾选剩余 2 个 AC（非法/不完整数据不误触发、RingBuffer 溢出不阻塞中断），并在 Manual Verification 追加 2026-05-05 代码复核条目，注明未做专项注入/压测，按代码层防御达成 |
| Task State | `task.json` 状态从 `planning → done`，`current_phase: 0 → 6`，`completedAt: 2026-05-05` |
| Archive | 任务从 `tasks/04-28-04-28-4g-cloud-uart` 移动到 `tasks/archive/2026-05/04-28-04-28-4g-cloud-uart`，由 `task.py archive` 自动提交 |

**Updated Files**:
- `.trellis/tasks/archive/2026-05/04-28-04-28-4g-cloud-uart/prd.md`
- `.trellis/tasks/archive/2026-05/04-28-04-28-4g-cloud-uart/task.json`

### Testing

- [OK] PRD 全部 12 个 AC 已勾选
- [OK] `git status` 工作目录 clean
- [OK] 归档自动提交成功（`7f4a67a chore(task): 归档 04-28-04-28-4g-cloud-uart`）
- [SKIP] 未做非法命令注入和强制 RingBuffer 溢出专项压测（按代码层防御达成；后续若扩展命令字典或接入语音模块需补回归测试）

### Status

[OK] **Completed**

### Next Steps

- None - task archived


### Git Commits

| Hash | Message |
|------|---------|
| `7f4a67a` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete


## Session 14: MPU6050倒地与碰撞报警

**Date**: 2026-05-05
**Task**: MPU6050倒地与碰撞报警
**Branch**: `dev`

### Summary

(Add summary)

### Main Changes

实现 MPU6050 事故级倒地和激烈碰撞报警：倒地采用低重力/冲击事件后稳定倾斜姿态确认，碰撞采用独立尖峰检测并短保持；两类报警可同时存在。新增本地 helmet_alarm 模块，报警后 RGB 红灯快闪并至少展示 15s，云端 LED 基础颜色通过 helmet_alarm_set_base_led() 仲裁恢复。

同步 LCD 第 6 行报警状态显示、M100PG telemetry 的 fall/collision 字段、Keil 工程文件、README/CLAUDE，以及 backend 规格中的 MPU6050 Safety Alarm Fanout 合同。rgb_led_color_t 保持由 rgb_led.h 定义，rgb_led.h 自包含，helmet_alarm.h 通过 bsp_system.h 聚合头使用该类型。

验证：git diff --check 通过；本轮文本文件 UTF-8 无 BOM + LF 检查通过；APP 头文件守卫通过；新增高频路径无 printf/HAL_MAX_DELAY/0xFFFFFF；RGB 直接写入路径仅保留 rgb_led.c 与 helmet_alarm.c。cppcheck 本机不可用，Keil Build 由用户本地验证。提交后仅剩 MDK-ARM/helmet/helmet.hex 构建产物未提交。


### Git Commits

| Hash | Message |
|------|---------|
| `1a17cad` | (see git log) |

### Testing

- [OK] (Add test results)

### Status

[OK] **Completed**

### Next Steps

- None - task complete
