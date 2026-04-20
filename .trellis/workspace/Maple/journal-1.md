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
