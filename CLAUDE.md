# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

SmartHelm — 开源智能头盔嵌入式固件项目，基于 STM32F103C8T6（Cortex-M3, 72MHz, 64KB Flash, 20KB SRAM）。使用 STM32CubeMX 生成外设初始化代码，Keil MDK-ARM V5 编译构建。

## 构建

- **IDE**: Keil MDK-ARM V5.32，工程文件位于 `MDK-ARM/helmet.uvprojx`
- **编译**: 在 Keil 中打开工程文件后 Build（F7）
- **烧录**: 使用 SWD 接口（PA13/PA14），通过 J-Link 或 ST-Link 下载
- **清理构建产物**: 运行 `Keilkill.bat`（删除 .o/.d/.axf/.map 等中间文件）
- **编译宏**: `USE_HAL_DRIVER`, `STM32F103xB`

## 架构

```
Core/           STM32CubeMX 自动生成代码（外设初始化、中断、时钟配置）
APP/            应用层代码（具体模块业务逻辑）
Drivers/        HAL 库 + CMSIS（不要手动修改）
MDK-ARM/        Keil 工程文件 + 启动文件
helmet.ioc      CubeMX 工程配置文件
```

### 执行流程

`main()` → HAL_Init → SystemClock_Config(HSE 8MHz + PLL ×9 = 72MHz) → 外设初始化(GPIO, DMA, USART1, ADC1, TIM1, I2C1, I2C2, USART2, TIM3) → ADC DMA 启动 → rgb_led_init（三色 LED 默认关闭） → pwm_motor_init（TB6612FNG 默认安全停止） → DHT11_Init → mpu6050_init（含 DMP 固件加载与自检） → max30102_init（Part ID 自检 + SpO2 模式配置） → st7735_init（PB0/PB1/PA7 软件 SPI，自检色块） → lcd_app_init（传感器数据显示页面） → asrpro_init（USART1 单字节中断接收） → m100pg_init（USART2 空闲 DMA 接收） → `scheduler_init()` → 主循环调用 `scheduler_run()`

M100PG 4G DTU 分为硬件链路和协议两层：`APP/m100pg.c` 负责 USART2 DMA、RingBuffer、上传调度和下发动作执行；`APP/m100pg_protocol.c` 负责 `UP,...\r\n` 上传帧格式化和 `LED_ON`、`LED_OFF`、`LED_WHITE`、`LED_RED`、`LED_GREEN` 完整命令解析。云端 LED 控制必须通过 `helmet_alarm_set_base_led()` 更新普通 LED 状态，不得直接写 PB12/PB13/PB14。上传 telemetry 包含 `mq2` 归一化趋势指数、`mq2_alarm`、MPU6050 的 `fall` / `collision` 报警状态；`APP/helmet_alarm.c` 以 20ms 周期读取报警状态，跌倒/碰撞红灯至少快闪 15s 且优先于 MQ2，MQ2 趋势异常黄灯至少快闪 5s，解除后恢复云端下发颜色。

MQ2 使用 ADC1 DMA 30 点均值和清洁空气 R0 校准计算 `Rs/R0`。`mq2` 遥测和 LCD 显示是归一化趋势指数，清洁空气约为 100；`mq2_get_ppm()` 仅是 LPG 曲线折算参考，不作为精确定量浓度。报警迟滞阈值集中在 `APP/mq2.c` 顶部：趋势指数 180 连续 3 次触发，130 连续 10 次恢复，并忽略前 5 次任务样本。

ASRPro 天问离线语音模块使用 USART1：ASR_TX 接 PA10，ASR_RX 接 PA9。默认固件把 USART1 作为纯语音串口，`APP/asrpro.c` 通过单字节中断接收并在调度器任务中解析 `led_on`、`led_off`、`motor_speed_0..3`。语音 LED 控制同样必须通过 `helmet_alarm_set_base_led()`，电机档位映射为 `{0, 33, 66, 100}`。`APP/asrpro.h` 中 `ASRPRO_ENABLE_USART1_DEBUG` 默认关闭，避免 `printf` 文本进入语音模块；临时串口一调试时可打开该宏或关闭 `ASRPRO_ENABLE_COMMAND_EXECUTION` 后重新编译。

### 任务调度器（`APP/scheduler.c`）

轮询式协作调度器，基于 `HAL_GetTick()` 实现毫秒级定时。添加新任务：在 `scheduler_task[]` 数组中追加 `{函数指针, 周期ms, 0}` 条目。

ST7735 显示屏模块使用 GPIO 软件 SPI：`PB0=SCL`、`PA7=SDA`、`PB1=DC`，`CS/RES/BLK` 本轮保持悬空且固件不控制。`APP/st7735.c` 只保留低耦合绘图接口；`APP/lcd_app.c` 以 200ms 周期读取传感器缓存并脏刷新 6 行 ASCII 文本，避免全屏高频刷新阻塞协作调度。

### 头文件依赖

`bsp_system.h` 是应用层的统一头文件入口，汇聚 HAL 头文件和所有 APP 模块头文件。新增应用模块时在此文件中 `#include`。

## 外设配置

| 外设    | 引脚       | 配置                      |
|---------|-----------|--------------------------|
| USART1  | PA9/PA10  | ASRPro 离线语音模块，115200-8N1，默认禁用 printf 输出 |
| ADC1    | PA0       | MQ2 烟雾传感器，DMA 循环采样 |
| TIM1    | 内部时钟   | 微秒级延时，预分频 72-1（1MHz 计数频率） |
| TIM3_CH1 | PB4      | TB6612FNG A 通道 PWMA，小风扇 PWM 调速，部分重映射 |
| GPIO    | PA8       | DHT11 数据线，推挽输出高速模式 |
| GPIO    | PA11/PA12/PB15 | TB6612FNG AIN2/AIN1/STBY |
| GPIO    | PB12/PB13/PB14 | 共阴三色 LED，低电平默认熄灭 |
| I2C1    | PB6/PB7   | MPU6050 六轴传感器，Fast Mode 400kHz |
| I2C2    | PB10/PB11 | MAX30102 心率血氧传感器，Fast Mode 400kHz |
| USART2  | PA2/PA3   | M100PG 4G DTU 透传通信，115200-8N1，RX 使用 DMA 空闲中断 |
| SWD     | PA13/PA14 | 调试接口                  |
| HSE     | PD0/PD1   | 外部 8MHz 晶振            |

## 代码风格

- 命名：函数和变量使用 `snake_case`，宏和常量使用 `UPPER_SNAKE_CASE`，类型定义以 `_t` 结尾（如 `task_t`）
- 模块前缀：BSP 层函数以 `bsp_` 开头，调度器相关以 `scheduler_` 开头，新模块以模块名为前缀
- 缩进：4 空格，大括号换行风格（函数体 `{` 另起一行，`if/for` 的 `{` 同行）
- 注释风格：
  - `.c` 函数实现处使用 Doxygen 格式（`@brief` / `@param` / `@retval`），简洁说明函数用法
  - `.h` 函数声明末尾只写简短 `//` 注释，避免重复维护大段说明
  - 函数内部只注释关键意图、硬件约束或非显而易见的分支
  - 结构体成员注释用 `//` 右侧对齐
- 函数顺序：模块 `*_task()` 放在 `.c` 文件底部，`*_init()` 放在 `*_task()` 上方
- 硬件映射：GPIO 端口、引脚、有效电平、默认状态等板级差异集中放在模块 `.c` 顶部，并使用模块前缀；`.h` 和调用方不暴露具体 GPIO 或 CubeMX 生成的 `main.h` 标签名
- 头文件保护：使用 `#ifndef / #define / #endif` 守卫，命名 `MODULE_H`
- 提交信息：遵循 [Conventional Commits](https://www.conventionalcommits.org/)，使用中文描述，如 `feat: 新增电池电量检测模块`
  - 可用前缀：`feat:` / `fix:` / `docs:` / `refactor:` / `test:` / `chore:` / `ci:`
  - 涉及具体模块时添加 scope，如 `feat(scheduler): 支持动态添加任务`

## 开发文档

- 开发日志使用 Trellis 的会话日志机制：`.trellis/workspace/<developer>/journal-*.md`（纳入版本控制，由 Trellis 管理）
  - 通过 `.trellis/scripts/add_session.py` 追加 session，索引 `.trellis/workspace/<developer>/index.md` 自动同步
  - 每个 session 关联 commit hash 便于追溯
- **每次提交前必须同步更新**：
  - `README.md`：涉及外设、功能模块、项目结构变动时更新对应章节
  - `CLAUDE.md`：涉及架构、外设配置、执行流程、约定变更时更新对应章节
- 提交后使用 `/trellis:record-session` 或 `add_session.py` 将本次 session 追加到 journal

## CI/CD

### 质量门禁（`.github/workflows/quality.yml`）

`push main` 与 `PR main` 触发，三类检查：

- **静态检查**：cppcheck（warning/performance/portability）、APP 头文件守卫、UTF-8 无 BOM、LF 行尾
- **Conventional Commits**：PR 标题强制 `<type>(<scope>): <desc>`，可选类型 `feat / fix / docs / style / refactor / perf / test / chore / ci / build / revert / release`
- **CHANGELOG 同步**：`feat / fix / perf / refactor` 类 PR 或改动 `APP/*.{c,h}` 的 PR 必须更新 `CHANGELOG.md` 的 `[Unreleased]` 段；release PR 走反向校验

APP 目录的源文件必须通过质量门禁：无 cppcheck 警告、头文件含 `#ifndef` 守卫、UTF-8 无 BOM、LF 行尾。

### 自动发版（`.github/workflows/release.yml`）

推送 `v*` 标签触发，三段式：

1. **validate**：tag 必须已合入 main（防止从非 main 分支误打 tag）+ `CHANGELOG.md` 含 `## [X.Y.Z] - YYYY-MM-DD` 段
2. **quality**：复跑静态检查
3. **release**：从 `CHANGELOG.md` 抽取该版本段作为 Release Note 主体 + `git-cliff` 生成完整提交记录附录（折叠展示），调 GitHub API 发 Release

### 发版流程（单 main 主干 + tag 发版）

```text
日常开发（main 分支）
  ├─ 小改动：直接 commit 到 main
  ├─ 较大功能：可选开短命 feat/xxx 分支 → PR → squash 合 main → 删分支
  └─ feat/fix/perf/refactor 提交同步在 CHANGELOG.md [Unreleased] 段加一行

发版（main 分支本地操作）
  ├─ CHANGELOG.md：[Unreleased] 重命名为 [X.Y.Z] - YYYY-MM-DD，
  │   顶部新建空 [Unreleased]，底部 compare 链接区追加 [X.Y.Z] 与更新 [Unreleased]
  ├─ commit: chore(release): vX.Y.Z
  ├─ git tag vX.Y.Z
  └─ git push origin main --tags

CI 自动 release.yml → 校验 tag/CHANGELOG → 质量门禁 → 抽取 CHANGELOG → 创建 GitHub Release
```

**版本号（SemVer）**：MAJOR 不兼容 / MINOR 新增功能 / PATCH 仅修复。

**分支模型**：`main` 为唯一长期分支，允许直接提交。功能开发可用短命 `feat/xxx` 分支，做完即合即删，不保留长期集成分支。

## 关键约定

- **CubeMX 生成的文件**：所有用户代码必须写在 `USER CODE BEGIN/END` 块内，否则重新生成时会被覆盖
- **新增应用模块**：源文件放 `APP/`，头文件 include 到 `APP/bsp_system.h`，并在 Keil 工程中添加源文件和头文件路径
- **模块移植边界**：调用方只能使用模块公开接口，不能直接操作其他模块的 GPIO 或私有状态；更换引脚、共阴/共阳、默认电平时只改模块顶部映射和 `.ioc`
- **修改外设配置**：通过 `helmet.ioc` 在 CubeMX 中修改后重新生成，不要直接改 `Core/` 下的初始化代码
- **HAL 库**：`Drivers/` 目录下的文件不应手动修改
