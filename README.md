<div align="center">

# SmartHelmet

**开源智能头盔固件 — 跌倒/碰撞检测 · 心率血氧 · 烟雾报警 · 4G 上云 · 离线语音 · 彩屏 HUD**

基于 STM32F103C8T6 · HAL 库 · STM32CubeMX + Keil MDK

![Release](https://img.shields.io/github/v/release/rsecss/helmet?include_prereleases&sort=semver&label=RELEASE&style=for-the-badge&labelColor=37474F&color=E91E63)
![Quality Gate](https://img.shields.io/github/actions/workflow/status/rsecss/helmet/quality.yml?branch=main&label=QUALITY%20GATE&style=for-the-badge&labelColor=37474F&color=8BC34A)
![Stars](https://img.shields.io/github/stars/rsecss/helmet?label=STARS&style=for-the-badge&labelColor=37474F&color=FBC02D)
![Last Commit](https://img.shields.io/github/last-commit/rsecss/helmet?label=LAST%20COMMIT&style=for-the-badge&labelColor=37474F&color=FF7043)
![Platform](https://img.shields.io/badge/PLATFORM-STM32F103C8T6-E53935?style=for-the-badge&labelColor=37474F)
![Lib](https://img.shields.io/badge/LIB-HAL-7E57C2?style=for-the-badge&labelColor=37474F)
![License](https://img.shields.io/github/license/rsecss/helmet?label=LICENSE&style=for-the-badge&labelColor=37474F&color=03A9F4)

发现 Bug 或有改进建议，欢迎提交 [Issue](https://github.com/rsecss/helmet/issues) 或 [PR](https://github.com/rsecss/helmet/pulls)。觉得有帮助的话，请点个 Star ⭐ 支持一下

</div>

## 功能

- **跌倒 / 碰撞检测** — MPU6050 Mahony 姿态解算，触发后红灯快闪至少 15 s，优先级最高
- **烟雾监测** — MQ2 归一化趋势指数（清洁空气 ≈ 100），迟滞报警，黄灯快闪至少 5 s
- **生命体征** — MAX30102 心率 / 血氧
- **环境监测** — DHT11 温湿度
- **4G 上云** — M100PG DTU 每秒上传一帧遥测，云端可下发 LED / 风扇命令
- **离线语音** — ASRPro 语音控制 LED 与风扇档位，无需联网
- **本地 HUD** — ST7735 128×128 彩屏实时显示传感器数据
- **风扇调速** — TB6612FNG PWM 驱动，0–3 四档

> 配套上位机 [helmet-console](https://github.com/rsecss/helmet-console) — 浏览器端 WebSocket 控制台，集成实时终端、设备面板与 AI 助手。

## 目录结构

```
helmet/
├── APP/          应用层：传感器驱动、报警仲裁、通信、HUD、调度器
├── Core/         STM32CubeMX 生成的初始化与中断代码
├── Drivers/      STM32 HAL + CMSIS（勿手改）
├── MDK-ARM/      Keil 工程与启动文件
├── helmet.ioc    CubeMX 工程配置
└── Keilkill.bat  构建产物清理脚本
```

## 系统架构

裸机 + 轮询式协作调度器：`APP/scheduler.c` 基于 `HAL_GetTick()` 毫秒节拍执行静态任务表。
RGB LED 由 `APP/helmet_alarm.c` 统一仲裁，优先级：跌倒/碰撞红灯 > MQ2 黄灯 > 云端/语音设定的常态颜色。

```
                    +---------------------------+
                    |        ST7735 HUD         |
                    |  (PB0 SCL / PA7 SDA / PB1)|
                    +-------------▲-------------+
                                  │ lcd_app_task 200ms
+----------+  I2C1  +----------+  │
|  MPU6050 |◀──────▶|          │  │ helmet_alarm_task 20ms
|  Mahony  |        │          │  ▼ (RGB 仲裁优先级)
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

| 模块 | 周期 | 职责 |
|---|---|---|
| `m100pg` + `m100pg_protocol` | 10 ms | 4G 收发、遥测上传、下行命令解析 |
| `asrpro` | 10 ms | 离线语音命令解析 |
| `mpu6050` | 10 ms | Mahony 姿态解算、跌倒/碰撞检测 |
| `helmet_alarm` | 20 ms | RGB 报警仲裁（唯一直写 LED 的模块） |
| `max30102` | 50 ms | 心率 / 血氧 |
| `mq2` | 100 ms | 烟雾趋势指数与报警 |
| `lcd_app` + `st7735` | 200 ms | HUD 脏刷新 |
| `dht11` | 1000 ms | 温湿度 |
| `rgb_led` / `pwm_motor` | — | LED / 风扇底层驱动 |

启动流程：`HAL_Init` → 72 MHz 时钟 → CubeMX 外设初始化 → 各模块 `*_init()` → `scheduler_run()` 主循环。

## 硬件与引脚

主控 STM32F103C8T6（Cortex-M3 @ 72 MHz，64 KB Flash / 20 KB SRAM），HSE 8 MHz（PD0/PD1）× PLL9。

| 器件 | 用途 | 接口 | 引脚 |
|---|---|---|---|
| MPU6050 | 姿态解算、跌倒/碰撞检测 | I2C1 400 kHz | PB6 (SCL) / PB7 (SDA) |
| MAX30102 | 心率 / 血氧 | I2C2 400 kHz | PB10 (SCL) / PB11 (SDA) |
| DHT11 | 温湿度 | 单总线 | PA8 |
| MQ2 | 烟雾 | ADC1 + DMA | PA0 |
| ST7735 1.44" 128×128 | HUD 显示 | 软件 SPI | PB0 (SCL) / PA7 (SDA) / PB1 (DC) |
| TB6612FNG | 风扇驱动 | TIM3_CH1 PWM | PB4 (PWMA)，PA11 / PA12 / PB15 (AIN2 / AIN1 / STBY) |
| RGB LED（共阴） | 状态 / 报警指示 | GPIO | PB12 / PB13 / PB14 (R / G / B) |
| M100PG | 4G 上云 | USART2 115200-8N1，RX DMA | PA2 (TX) / PA3 (RX) |
| ASRPro | 离线语音 | USART1 115200-8N1，RX 中断 | PA9 (TX) / PA10 (RX) |
| SWD | 调试烧录 | J-Link / ST-Link | PA13 / PA14 |

## 快速开始

### 环境准备

| 工具 | 版本 | 用途 |
|---|---|---|
| Keil MDK-ARM | ≥ V5.32 | 编译、调试、烧录 |
| J-Link / ST-Link | — | SWD 烧录器 |
| STM32CubeMX + FW_F1 | 6.17 / V1.8.7 | 仅修改外设配置时需要 |

### 编译与烧录

```bash
git clone https://github.com/rsecss/helmet.git
```

1. Keil 打开 `MDK-ARM/helmet.uvprojx`
2. **F7** 编译
3. 连接 SWD（PA13 / PA14），**F8** 下载

### 上电验证

- HUD 显示自检色块，随后进入传感器数据页
- RGB LED 默认熄灭
- 4G 入网后每秒上传一帧遥测（见[通信与控制](#通信与控制)）

> 清理构建产物运行 `Keilkill.bat`。外设配置只通过 `helmet.ioc` 在 CubeMX 中修改后重新生成，手写代码放在 `USER CODE BEGIN/END` 块内。

## 通信与控制

两条控制通道共用一套命令字典：云端经 M100PG 4G 下发（浏览器端控制台由 [helmet-console](https://github.com/rsecss/helmet-console) 提供），本地经 ASRPro 离线语音识别。报警期间下发的 LED 颜色仅暂存，报警解除后自动生效。

### 遥测上行（4G，每秒一帧）

UTF-8 文本行，`\n` 结尾；WebSocket 封包由 4G 模块负责，MCU 只透传文本。协议细节见 `APP/m100pg_protocol.h`。

```
temp=23,hum=60,mq2=120,mq2_alarm=0,pitch=1.2,roll=-0.5,yaw=180.0,fall=0,collision=0,hr=72,spo2=98,led=white,motor=2
```

| 字段 | 含义 |
|---|---|
| `temp` / `hum` | 温度 °C / 湿度 % |
| `mq2` / `mq2_alarm` | 烟雾趋势指数 / 报警 0-1 |
| `pitch` / `roll` / `yaw` | 姿态欧拉角 |
| `fall` / `collision` | 跌倒 / 碰撞 0-1 |
| `hr` / `spo2` | 心率 bpm / 血氧 %（0 = 无效） |
| `led` / `motor` | LED 颜色与风扇档位镜像 |

### 控制命令（4G 下行 / 离线语音）

| 命令 | 动作 | 4G | 语音 |
|---|---|:-:|:-:|
| `led_on` / `led_off` | LED 白光开 / 关 | ✔ | ✔ |
| `led_color_white` / `led_color_red` / `led_color_green` | 设定 LED 颜色 | ✔ | — |
| `motor_speed_0` .. `motor_speed_3` | 风扇档位 {0, 33, 66, 100}% | ✔ | ✔ |
| `ping` / `pong` | 心跳，静默忽略 | ✔ | — |

语音接线 ASR_TX → PA10、ASR_RX → PA9（115200-8N1）；命令为小写英文 + `\n`，严格等长匹配（不做模糊 / 大小写转换）。USART1 为纯语音串口，printf 调试输出默认关闭，需要时打开 `APP/asrpro.h` 中的 `ASRPRO_ENABLE_USART1_DEBUG` 重新编译。

## 许可证

[Apache License 2.0](LICENSE)

## 免责声明

- 本项目内容**按原样提供（AS-IS）**，不提供任何明示或暗示的保证
- 本项目仅供学习交流使用，未经任何安全或医疗认证，不得用于安全防护或医疗用途
- 作者不对代码的正确性、完整性、适用性或可靠性做任何承诺
- 使用者需自行评估内容适用性并承担全部使用风险
- 在任何情况下，作者均不对因使用或无法使用本项目内容而导致的任何直接、间接、偶然、特殊、惩罚性或后果性损害承担责任
