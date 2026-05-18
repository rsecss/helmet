# Changelog

本项目所有显著变更都会记录在这份文件中。

格式遵循 [Keep a Changelog 1.1.0](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [SemVer 2.0.0](https://semver.org/lang/zh-CN/)。

类别约定：`Added`（新功能）/ `Changed`（变更）/ `Fixed`（修复）/ `Removed`（移除）/ `Security`（安全）。

---

## [Unreleased]

### Added

- 新增 MQ2 归一化烟雾趋势异常状态，接入 LCD `ALM`、4G `mq2_alarm` 遥测和本地黄灯快闪报警仲裁。
- 新增 DHT11 高温风扇自动启动仲裁，30°C 自动至少 1 档运行、28°C 释放，并通过 4G telemetry 上报 `fan_auto` / `temp_limit` / `temp_recover`。

### Changed

- MQ2 采集改为基于清洁空气 R0 校准和 `Rs/R0` 的趋势指数，遥测 `mq2` 字段不再作为精确定量 ppm 表述。
- Web/语音 `motor_speed_0` 作为最高优先级关闭指令，高温自动打开风扇时也会立即停机；`motor_speed_1..3` 解除关闭覆盖并设置手动基础档位。

## [0.8.0] - 2026-05-05

### Added

- 新增 ASRPro 天问离线语音模块，USART1 单字节中断 + 行缓冲解析 `led_on` / `led_off` / `motor_speed_0..3` ([62a2c3b](https://github.com/rsecss/helmet/commit/62a2c3b))
- 新增 `helmet_alarm` 安全报警仲裁模块，MPU6050 跌倒 / 激烈碰撞确认后 RGB 红灯抢占快闪 ≥ 15 s，云端 / 语音 LED 颜色暂存待解除 ([1a17cad](https://github.com/rsecss/helmet/commit/1a17cad))
- 新增 ST7735 1.44" RGB TFT 模块（128×128，PB0/PA7/PB1 软件 SPI）+ `lcd_app` 6 行脏刷新 HUD 应用 ([e0e65b2](https://github.com/rsecss/helmet/commit/e0e65b2), [e7cef16](https://github.com/rsecss/helmet/commit/e7cef16))
- 新增 TB6612FNG A 通道 `pwm_motor` 电机驱动，TIM3_CH1 PWM 调速 / 转向 / 安全停止 ([3b935b3](https://github.com/rsecss/helmet/commit/3b935b3))
- 新增 CHANGELOG.md 与 Release PR 工作流，发版主体由人工提炼，cliff 自动生成附录

### Changed

- M100PG 4G 联调收尾，`m100pg.c` 拆出 `m100pg_bsp` 隔离硬件层；LED 下发收敛到 `helmet_alarm_set_base_led()`，禁止旁路写 PB12/PB13/PB14 ([88c7936](https://github.com/rsecss/helmet/commit/88c7936))
- `rgb_led` 模块由 on/off 扩展为颜色枚举接口，默认白色 ([d849f50](https://github.com/rsecss/helmet/commit/d849f50))
- `dht11` / `mq2` 暴露读数快照接口供 4G telemetry 与 HUD 复用 ([81948b3](https://github.com/rsecss/helmet/commit/81948b3), [d1f8e51](https://github.com/rsecss/helmet/commit/d1f8e51))
- `scheduler` 调整传感器任务节拍，避免 1 Hz 阻塞型采样推迟 10 ms 级 4G 下发处理

### Fixed

- 解决 DHT11 阻塞采样导致 4G 命令下发延迟的问题，恢复浏览器 → 设备命令实时性 ([2b2524c](https://github.com/rsecss/helmet/commit/2b2524c))

---

## [0.7.0] - 2026-04-29

### Added

- 实现 M100PG 4G 上云上传帧（每秒一帧 telemetry）与 LED 下发控制，浏览器可远程切换 RGB 颜色 ([#3](https://github.com/rsecss/helmet/pull/3))

---

## [0.6.0] - 2026-04-29

### Added

- 新增 MAX30102 心率血氧传感器模块，I2C2 FIFO + PBA 心跳算法 + AC/DC SpO2 估算 ([#2](https://github.com/rsecss/helmet/pull/2))

### Documentation

- 沉淀 MAX30102 调试方法论与提交规范到 `.trellis/spec/`

---

## [0.5.2] - 2026-04-20

### Added

- 引入 Trellis 多 Agent 任务管理工作流（`.trellis/` 目录、spec / tasks / journal 三层）
- 强化 CI 发版流程：质量门禁 / 自动 Release / git-cliff changelog 生成 ([#1](https://github.com/rsecss/helmet/pull/1))

---

## [0.5.1] - 2026-03-06

### Changed

- 重构 MPU6050 模块，精简 InvenSense MotionDriver 驱动，仅保留项目使用的 DMP 子集 ([cbea9df](https://github.com/rsecss/helmet/commit/cbea9df))

---

## [0.5.0] - 2026-03-06

### Added

- 新增 MPU6050 六轴姿态传感器模块（DMP 固件加载 + 自检 + 欧拉角输出），I2C1 Fast Mode 400 kHz ([31c8468](https://github.com/rsecss/helmet/commit/31c8468))

---

## [0.4.0] - 2026-03-06

### Added

- 新增 GitHub Actions 质量门禁工作流（cppcheck 静态分析、头文件守卫、UTF-8 + LF 编码检查）
- 新增自动发布工作流：推送 `v*` 标签触发，git-cliff 基于 Conventional Commits 生成 changelog 并创建 GitHub Release ([30f92ce](https://github.com/rsecss/helmet/commit/30f92ce))

### Fixed

- 抑制 cppcheck 对 CMSIS 编译器检测的预处理误报 ([95a9dca](https://github.com/rsecss/helmet/commit/95a9dca))

### Changed

- 限制质量门禁仅在 `APP/` 代码与项目文档变更时触发，减少无效 CI 构建 ([f82d129](https://github.com/rsecss/helmet/commit/f82d129))

---

## [0.3.1] - 2026-03-06

### Changed

- 重构 DHT11 驱动，使用纯 HAL API 替代位带操作，提升可移植性 ([c38070c](https://github.com/rsecss/helmet/commit/c38070c))
- 简化 README 标题排版 ([d010c43](https://github.com/rsecss/helmet/commit/d010c43))

---

## [0.3.0] - 2026-03-06

### Added

- 新增 DHT11 温湿度传感器模块，单总线时序 + TIM1 微秒级延时 ([7ba2525](https://github.com/rsecss/helmet/commit/7ba2525))

---

## [0.2.0] - 2026-03-06

### Added

- 新增 MQ2 烟雾传感器模块，ADC1 + DMA 循环采样 ([c77edde](https://github.com/rsecss/helmet/commit/c77edde))

---

## [0.1.0] - 2026-03-05

### Added

- 初始化 SmartHelm 智能头盔项目框架：STM32F103C8T6 基础工程、CubeMX 时钟与外设配置、协作调度器骨架 ([86a9534](https://github.com/rsecss/helmet/commit/86a9534))

[Unreleased]: https://github.com/rsecss/helmet/compare/v0.8.0...HEAD
[0.8.0]: https://github.com/rsecss/helmet/compare/v0.7.0...v0.8.0
[0.7.0]: https://github.com/rsecss/helmet/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/rsecss/helmet/compare/v0.5.2...v0.6.0
[0.5.2]: https://github.com/rsecss/helmet/compare/v0.5.1...v0.5.2
[0.5.1]: https://github.com/rsecss/helmet/compare/v0.5.0...v0.5.1
[0.5.0]: https://github.com/rsecss/helmet/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/rsecss/helmet/compare/v0.3.1...v0.4.0
[0.3.1]: https://github.com/rsecss/helmet/compare/v0.3.0...v0.3.1
[0.3.0]: https://github.com/rsecss/helmet/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/rsecss/helmet/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/rsecss/helmet/releases/tag/v0.1.0
