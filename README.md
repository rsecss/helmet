# SmartHelm — 智能头盔固件

基于 STM32F103C8T6 的开源智能头盔嵌入式项目。

## 硬件平台

| 参数 | 规格 |
|------|------|
| MCU | STM32F103C8T6 (Cortex-M3) |
| 主频 | 72 MHz (HSE 8MHz + PLL ×9) |
| Flash / SRAM | 64 KB / 20 KB |
| 封装 | LQFP48 |

## 项目结构

```
Core/           CubeMX 生成的外设初始化、中断、时钟配置
APP/            应用层业务逻辑（调度器、BSP 等）
Drivers/        STM32 HAL 库 + CMSIS
MDK-ARM/        Keil 工程文件 + 启动文件
helmet.ioc      STM32CubeMX 工程配置
```

## 开发环境

- **IDE**: Keil MDK-ARM V5.32
- **代码生成**: STM32CubeMX 6.17.0
- **固件包**: STM32Cube FW_F1 V1.8.7
- **烧录工具**: J-Link / ST-Link（SWD 接口）

## 快速开始

1. 使用 Keil 打开 `MDK-ARM/helmet.uvprojx`
2. Build（F7）编译工程
3. 通过 SWD 下载至目标板

## 外设使用

| 外设 | 引脚 | 用途 |
|------|------|------|
| USART1 | PA9 (TX) / PA10 (RX) | 串口调试，115200-8N1 |
| SWD | PA13 / PA14 | 程序下载与调试 |

## 软件架构

采用裸机轮询调度模型，`APP/scheduler.c` 实现基于 SysTick 的毫秒级协作调度器，主循环中周期性执行各任务函数。
