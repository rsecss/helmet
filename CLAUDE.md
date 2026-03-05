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

`main()` → HAL_Init → SystemClock_Config(HSE+PLL) → 外设初始化(GPIO, DMA, USART1, ADC1) → ADC DMA 启动 → `scheduler_init()` → 主循环调用 `scheduler_run()`

### 任务调度器（`APP/scheduler.c`）

轮询式协作调度器，基于 `HAL_GetTick()` 实现毫秒级定时。添加新任务：在 `scheduler_task[]` 数组中追加 `{函数指针, 周期ms, 0}` 条目。

### 头文件依赖

`bsp_system.h` 是应用层的统一头文件入口，汇聚 HAL 头文件和所有 APP 模块头文件。新增应用模块时在此文件中 `#include`。

## 外设配置

| 外设    | 引脚       | 配置                      |
|---------|-----------|--------------------------|
| USART1  | PA9/PA10  | 115200-8N1, printf 重定向 |
| ADC1    | PA0       | MQ2 烟雾传感器，DMA 循环采样 |
| SWD     | PA13/PA14 | 调试接口                  |
| HSE     | PD0/PD1   | 外部 8MHz 晶振            |

## 代码风格

- 命名：函数和变量使用 `snake_case`，宏和常量使用 `UPPER_SNAKE_CASE`，类型定义以 `_t` 结尾（如 `task_t`）
- 模块前缀：BSP 层函数以 `bsp_` 开头，调度器相关以 `scheduler_` 开头，新模块以模块名为前缀
- 缩进：4 空格，大括号换行风格（函数体 `{` 另起一行，`if/for` 的 `{` 同行）
- 注释风格：
  - 函数注释使用 Doxygen 格式（`@brief` / `@param` / `@retval`），中文描述
  - 行内注释用 `//`，写在代码右侧或上方，说明意图而非复述代码
  - 结构体成员注释用 `//` 右侧对齐
- 头文件保护：使用 `#ifndef / #define / #endif` 守卫，命名 `MODULE_H`
- 提交信息：遵循 [Conventional Commits](https://www.conventionalcommits.org/)，使用中文描述，如 `feat: 新增电池电量检测模块`
  - 可用前缀：`feat:` / `fix:` / `docs:` / `refactor:` / `test:` / `chore:` / `ci:`
  - 涉及具体模块时添加 scope，如 `feat(scheduler): 支持动态添加任务`

## 开发文档

- `dev_log.md`（开发日志）记录每次功能变更、问题排查、设计决策，不纳入版本控制（已在 `.gitignore` 中排除），仅本地维护
- **每次提交前必须同步更新**：
  - `dev_log.md`：记录本次变更内容、日期、设计决策
  - `README.md`：涉及外设、功能模块、项目结构变动时更新对应章节
  - `CLAUDE.md`：涉及架构、外设配置、执行流程、约定变更时更新对应章节

## 关键约定

- **CubeMX 生成的文件**：所有用户代码必须写在 `USER CODE BEGIN/END` 块内，否则重新生成时会被覆盖
- **新增应用模块**：源文件放 `APP/`，头文件 include 到 `APP/bsp_system.h`，并在 Keil 工程中添加源文件和头文件路径
- **修改外设配置**：通过 `helmet.ioc` 在 CubeMX 中修改后重新生成，不要直接改 `Core/` 下的初始化代码
- **HAL 库**：`Drivers/` 目录下的文件不应手动修改
