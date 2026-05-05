# SmartHelm

> 基于 STM32F103 的开源智能头盔嵌入式固件 — 环境感知、生命体征、姿态报警、4G 上云、离线语音控制与本地彩屏 HUD。

![Quick Start](https://img.shields.io/badge/QUICK_START-5_MIN-2196F3?style=for-the-badge&labelColor=37474F)
![Quality Gate](https://img.shields.io/github/actions/workflow/status/rsecss/helmet/quality.yml?branch=main&label=QUALITY%20GATE&style=for-the-badge&labelColor=37474F&color=8BC34A)
![Release](https://img.shields.io/github/v/release/rsecss/helmet?include_prereleases&sort=semver&label=RELEASE&style=for-the-badge&labelColor=37474F&color=E91E63)
![License](https://img.shields.io/github/license/rsecss/helmet?label=LICENSE&style=for-the-badge&labelColor=37474F&color=F0A30A)
![Conventional Commits](https://img.shields.io/badge/COMMITS-CONVENTIONAL-1E88E5?style=for-the-badge&labelColor=37474F)

---

## 目录

- [架构概览](#架构概览)
- [硬件与引脚](#硬件与引脚)
- [快速开始](#快速开始)
- [应用模块](#应用模块)
- [通信协议](#通信协议)
- [离线语音控制](#离线语音控制)
- [代码组织](#代码组织)
- [开发约定](#开发约定)
- [CI / Release](#ci--release)
- [License](#license)

---

## 架构概览

单 MCU 协作调度的嵌入式固件，三层职责：

1. **感知层** — 4 类传感器（IMU / 心率血氧 / 温湿度 / 烟雾）各自一个 `APP/` 模块，周期任务采样并写入模块全局缓存。
2. **决策层** — 协作调度器 `APP/scheduler.c` 按毫秒节拍执行任务表；本地报警仲裁 `APP/helmet_alarm.c` 是 RGB LED 的唯一直写者，跌倒 / 碰撞期间红灯抢占普通灯效。
3. **通信层** — USART2 DMA + RingBuffer 接 4G DTU 上云，USART1 单字节 IT 接 ASRPro 离线语音，ST7735 软件 SPI 驱本地 HUD。

```
                    +---------------------------+
                    |        ST7735 HUD         |
                    |  (PB0 SCL / PA7 SDA / PB1)|
                    +-------------▲-------------+
                                  │ lcd_app_task 200ms
+----------+  I2C1  +----------+  │
|  MPU6050 |◀──────▶|          │  │ helmet_alarm_task 20ms
|   DMP    |        │          │  ▼ (RGB 仲裁优先级)
+----------+        │          │ +-----------+        +-----------+
+----------+  I2C2  │          │ │  RGB LED  │◀──────▶│ helmet_   │
| MAX30102 |◀──────▶│  STM32   │ │ PB12-14   │        │  alarm    │
+----------+        │ F103C8T6 │ +-----------+        +-----------+
+----------+ 1-Wire │   72MHz  │ +-----------+
|  DHT11   |◀──────▶│   裸机   │ │ TB6612FNG │  TIM3_CH1 PWM
+----------+        │  调度器  │ │ + 风扇    │◀────────────┐
+----------+   ADC  │          │ +-----------+             │
|   MQ2    |◀──────▶│          │                           │
+----------+   DMA  │          │              motor_speed_0..3
                    │          │ USART1   +----------------▼----+
                    │          │◀────────▶│  ASRPro 离线语音    │
                    │          │  IT      │  led_on/off/motor   │
                    │          │ USART2   +---------------------+
                    │          │◀────────▶+---------------------+
                    +----------+ DMA-IDLE │ M100PG 4G DTU       │
                                          │ → WebSocket / 浏览器│
                                          +---------------------+
```

执行流程：`HAL_Init` → 时钟 72 MHz → CubeMX 外设初始化 → 各模块 `*_init()` → `scheduler_run()` 主循环。任务节拍：

| Task | 周期 | 作用 |
|---|---|---|
| `m100pg_task` | 10 ms | 4G RX 解析、转发、上传节流 |
| `asrpro_task` | 10 ms | 语音命令消费 |
| `mpu6050_task` | 10 ms | DMP 采样 + 跌倒 / 碰撞检测 |
| `helmet_alarm_task` | 20 ms | RGB 输出仲裁 |
| `max30102_task` | 50 ms | 心率 / SpO2 |
| `mq2_task` | 100 ms | 烟雾浓度 |
| `lcd_app_task` | 200 ms | HUD 脏刷新 |
| `dht11_task` | 1000 ms | 温湿度 |

---

## 硬件与引脚

| 类别 | 型号 | 接口 | 引脚 |
|---|---|---|---|
| MCU | STM32F103C8T6 | LQFP48 | Cortex-M3 @ 72 MHz, 64 KB Flash / 20 KB SRAM |
| IMU | MPU6050 | I2C1 400 kHz | PB6 (SCL) / PB7 (SDA) |
| 心率血氧 | MAX30102 | I2C2 400 kHz | PB10 (SCL) / PB11 (SDA) |
| 温湿度 | DHT11 | 单总线 | PA8 |
| 烟雾 | MQ2 | ADC1 + DMA | PA0 |
| 显示 | ST7735 1.44" RGB TFT (128×128) | 软件 SPI | PB0 (SCL) / PA7 (SDA) / PB1 (DC) |
| 电机 | TB6612FNG + 5V 风扇 | TIM3_CH1 PWM | PB4 + PA11 / PA12 / PB15 |
| RGB LED | 共阴三色 | GPIO | PB12 (R) / PB13 (G) / PB14 (B) |
| 4G DTU | M100PG | USART2 DMA | PA2 (TX) / PA3 (RX), 115200-8N1 |
| 离线语音 | ASRPro 天问 | USART1 IT | PA9 (TX) / PA10 (RX), 115200-8N1 |
| 调试 | SWD | PA13 / PA14 | J-Link / ST-Link |
| 时钟 | HSE 8 MHz | PD0 / PD1 | × PLL 9 = 72 MHz |

---

## 快速开始

| 工具 | 版本 |
|---|---|
| Keil MDK-ARM | V5.32+ |
| STM32CubeMX | 6.17.0 |
| STM32Cube FW_F1 | V1.8.7 |
| 烧录器 | J-Link / ST-Link |

```bash
git clone https://github.com/rsecss/helmet.git
cd helmet
```

1. Keil 打开 `MDK-ARM/helmet.uvprojx`
2. 按 **F7** 编译
3. 通过 SWD 下载到 STM32F103C8T6
4. 上电后 RGB LED 默认关闭，HUD 自检后进入传感器数据页

清理构建产物：`./Keilkill.bat`。修改外设配置：通过 `helmet.ioc` 在 CubeMX 重新生成，**手写代码必须落在 `USER CODE BEGIN/END` 块内**。

---

## 应用模块

| 模块 | 源文件 | 说明 |
|---|---|---|
| 任务调度器 | `APP/scheduler.c` | SysTick 毫秒级协作调度，静态任务表 |
| BSP 头汇聚 | `APP/bsp_system.h` | HAL + APP 头文件统一入口 |
| MQ2 烟雾 | `APP/mq2.c` | ADC + DMA 循环采样，输出 ppm |
| DHT11 温湿度 | `APP/dht11.c` | 单总线时序，1 Hz |
| MPU6050 IMU | `APP/mpu6050.c` + `mpu6050_inv_mpu*.c` | InvenSense DMP 固件 + 跌倒 / 碰撞算法 |
| MAX30102 生命体征 | `APP/max30102.c` | I2C2 FIFO + PBA 心跳 + AC/DC SpO2 |
| RGB LED | `APP/rgb_led.c` | 共阴三色 LED，仅暴露 `rgb_led_color_t` 接口 |
| 安全报警仲裁 | `APP/helmet_alarm.c` | 唯一直写 RGB 的模块；报警态强制红色快闪 ≥ 15 s |
| PWM 电机 | `APP/pwm_motor.c` | TB6612FNG A 通道，调速 / 转向 / 停止 |
| 4G 链路 | `APP/m100pg.c` + `APP/m100pg_bsp.c` | USART2 DMA + RingBuffer + 心跳 + 上传调度 |
| 4G 协议 | `APP/m100pg_protocol.c` | 上行 telemetry 格式化 + 下行命令字典 |
| 离线语音 | `APP/asrpro.c` | USART1 单字节 IT + 调度器侧解析 |
| ST7735 驱动 | `APP/st7735.c` | 软件 SPI + 低耦合绘图原语 |
| LCD 应用 | `APP/lcd_app.c` | 6 行脏刷新 HUD |

---

## 通信协议

### 上行（设备 → 浏览器）

每秒一帧，单行逗号分隔 `key=value`，`\n` 终止：

```
temp=23,hum=60,mq2=120,pitch=1.2,roll=-0.5,yaw=180.0,fall=0,collision=0,hr=72,spo2=98,led=white,motor=2
```

| 字段 | 类型 | 含义 |
|---|---|---|
| `temp` / `hum` | uint8 | 温度 (°C) / 相对湿度 (%) |
| `mq2` | uint32 | MQ2 ppm 估算 |
| `pitch` / `roll` / `yaw` | float | DMP 姿态欧拉角 |
| `fall` / `collision` | uint8 | 0/1，跌倒确认 / 激烈碰撞 |
| `hr` / `spo2` | int32 | 心率 (bpm) / 血氧 (%)，0 表示无效 |
| `led` | enum | `off` / `white` / `red` / `green`（意图镜像） |
| `motor` | uint8 | 0..3 档位（意图镜像） |

### 下行（浏览器 → 设备）

| 命令 | 动作 |
|---|---|
| `led_on` / `led_off` | `led_set(WHITE / OFF)` |
| `led_color_white` / `led_color_red` / `led_color_green` | `led_set(...)` |
| `motor_speed_0..3` | `motor_set_speed(0..3)` → `{0, 33, 66, 100}` % |
| `ping` / `pong` | 心跳，静默忽略 |
| 其他 | 投递到 `on_unknown(line, len)` |

LED 命令必须经 `helmet_alarm_set_base_led()`，**不允许直接写 PB12/PB13/PB14**。报警活动期间下发的颜色仅暂存，待报警自动解除后恢复。协议库说明详见 `APP/m100pg_protocol.h` 头注释。

---

## 离线语音控制

| 项目 | 取值 |
|---|---|
| 串口 | USART1 @ 115200-8N1 |
| 接线 | ASR_TX → MCU PA10 (RX) ; ASR_RX → MCU PA9 (TX) |
| 接收方式 | 单字节中断 + 128 B 环形缓冲 + 48 B 行缓冲 |
| 命令格式 | 小写英文 + `\n`，容忍 `\r` 与首尾 ASCII 空白 |
| 支持命令 | `led_on` / `led_off` / `motor_speed_0..3` |
| 严格性 | 严格 `memcmp` 等长匹配，**不做模糊 / 中文 / 大小写转换** |

调试旁路（编译时切换）：

```c
// APP/asrpro.h
#define ASRPRO_ENABLE_COMMAND_EXECUTION  1U  // 改为 0 临时关闭语音执行
#define ASRPRO_ENABLE_USART1_DEBUG       0U  // 改为 1 重新打开 printf 与 4G 转发
```

---

## 代码组织

```
helmet/
├── APP/                  应用层（业务逻辑、传感器驱动、调度器）
├── Core/                 STM32CubeMX 生成（外设初始化、中断、时钟）
├── Drivers/              STM32 HAL 库 + CMSIS（不要手改）
├── MDK-ARM/              Keil 工程文件 + 启动文件
├── .github/workflows/    CI（quality.yml + release.yml）
├── .trellis/             Trellis 多 Agent 任务管理（spec / tasks / journal）
├── helmet.ioc            STM32CubeMX 工程配置
├── Keilkill.bat          构建产物清理脚本
└── README.md
```

---

## 开发约定

- **命名**：函数 / 变量 `snake_case`；宏 / 常量 `UPPER_SNAKE_CASE`；类型以 `_t` 结尾。
- **模块前缀**：所有导出符号以模块名开头（`mpu6050_*`、`scheduler_*` …）。
- **缩进**：4 空格；函数体 `{` 另起一行，控制流 `{` 同行。
- **注释**：`.c` 实现处 Doxygen `@brief / @param / @retval`；`.h` 行尾短注释；函数内部只标注非显而易见的意图与硬件约束。
- **硬件映射**：GPIO 端口 / 引脚 / 有效电平 / 默认状态集中在模块 `.c` 顶部，模块前缀；`.h` 与调用方不暴露 `main.h` 的 CubeMX 标签。
- **头文件守卫**：`#ifndef MODULE_H / #define MODULE_H / #endif`。
- **CubeMX 边界**：手写代码必须落在 `USER CODE BEGIN/END` 内。
- **新增模块**：源文件放 `APP/`，头文件 `#include` 到 `APP/bsp_system.h`，并在 Keil 工程添加源文件与头文件路径。
- **跨模块边界**：调用方仅使用模块公开接口；更换引脚 / 共阴共阳 / 默认电平时只改模块顶部映射 + `.ioc`。
- **HAL 库**：`Drivers/` 不可手改。
- **提交信息**：[Conventional Commits](https://www.conventionalcommits.org/) 中文描述，前缀 `feat:` / `fix:` / `docs:` / `refactor:` / `test:` / `chore:` / `ci:`。
- **每次提交前同步**：涉及外设 / 模块 / 项目结构变动同步 `README.md`；架构 / 外设 / 执行流程变更同步 `CLAUDE.md`。
- **Trellis 工作流**：开发日志走 `.trellis/workspace/<developer>/journal-*.md`，由 `add_session.py` 追加，commit hash 关联。

详细 spec 见 `.trellis/spec/backend/`（错误处理、质量标准、嵌入式数据策略、日志约定 + 5 个跨模块 Scenario 合同）。

---

## CI / Release

### Quality Gate (`.github/workflows/quality.yml`)

`push main/dev` 与 `PR main` 触发，三类检查：

- **静态检查**：cppcheck（warning/performance/portability）+ 头文件守卫 + UTF-8 无 BOM + LF 行尾
- **Conventional Commits 校验**：PR 标题必须形如 `<type>(<scope>): <description>`
- **CHANGELOG 同步**：`feat / fix / perf / refactor` 类 PR 必须更新 `CHANGELOG.md` 的 `[Unreleased]` 段；release PR 反向校验版本段已写入

### Auto Release (`.github/workflows/release.yml`)

推送 `v*` 标签触发，三段式：

1. **validate**：tag 必须已合入 main + CHANGELOG.md 含对应版本段
2. **quality**：复跑静态检查
3. **release**：抽取 CHANGELOG.md 中该版本段为 release note 主体；git-cliff 生成完整提交记录作为可折叠附录；调 GitHub API 创建 Release

### 发版工作流（5 步）

```text
开发期
  └─ 每个 feat/fix PR → 顺手在 CHANGELOG.md 的 [Unreleased] 段加一行

准备发版（在 dev 分支）
  ├─ 把 [Unreleased] 重命名为 [X.Y.Z] - YYYY-MM-DD
  ├─ 顶部新建空 [Unreleased]
  ├─ 底部 compare 链接区追加 [X.Y.Z] 与更新 [Unreleased]
  └─ commit: chore(release): vX.Y.Z

提 Release PR (dev → main)
  ├─ PR URL 加 ?template=release.md 选用发版模板
  ├─ 标题: release: vX.Y.Z
  └─ 评审通过后 squash & merge

打 tag（在 main HEAD）
  ├─ git checkout main && git pull
  ├─ git tag vX.Y.Z
  └─ git push origin vX.Y.Z

CI 自动发版
  └─ release.yml 校验 → 质量门禁 → 抽取 CHANGELOG → 拼附录 → 发 Release
```

**版本号约定（SemVer）**：MAJOR 不兼容变更 / MINOR 新增功能 / PATCH 仅修复。

**分支保护**：main 只接 PR 合并，禁止直接 push；建议在 GitHub Settings → Branches 启用 "Require pull request before merging" + "Require status checks to pass"。

**本地预览**：`git cliff --unreleased --strip header --config cliff.toml` 可预览将自动生成的提交附录。

---

## License

发布于本仓库 LICENSE 条款；项目使用的第三方组件保留各自许可（STM32 HAL / CMSIS — Apache 2.0；InvenSense MotionDriver — InvenSense 授权）。
