# Quality Guidelines

> Code quality standards for firmware development.

---

## Overview

感知层代码通过 GitHub Actions 的 **Quality Gate** 工作流强制执行质量标准（`.github/workflows/quality.yml`），触发路径为 `APP/**`、`README.md`、`CLAUDE.md`。

**必过门禁**：
1. cppcheck 静态分析（warning / performance / portability 零容忍）
2. 头文件守卫检查（`APP/*.h` 必须含 `#ifndef MODULE_H`）
3. 编码规范检查（UTF-8 无 BOM + LF 行尾）

**人工审查要点**：Conventional Commits 规范、模块职责单一性、CubeMX 代码边界保护。

当前文档覆盖范围：**感知层（APP/）代码质量**。`Core/`（CubeMX 生成）和 `Drivers/`（HAL 库）不受此门禁约束。

---

## Forbidden Patterns

### 1. 修改受保护目录

**禁止**：
- 手动修改 `Core/` 下的 CubeMX 生成代码（`gpio.c`, `usart.c` 等），除非在 `USER CODE BEGIN/END` 块内
- 修改 `Drivers/` 下的 HAL 库或 CMSIS 头文件

**原因**：CubeMX 重新生成会覆盖手动修改；HAL 库由 ST 官方维护，修改会阻塞未来升级。

### 2. 文件编码违规

- **禁止** UTF-8 BOM 开头（CI 会扫描 `\xef\xbb\xbf` 并报错）
- **禁止** CRLF 行尾（CI 会扫描 `\r\n` 并报错）
- **推荐** 在编辑器中强制 UTF-8 (no BOM) + LF

### 3. 中断处理违规

- **禁止** 在中断回调中调用 `printf` / `HAL_Delay` / 长时间轮询
- **禁止** 在中断中分配内存（`malloc`）
- **禁止** 在中断中持有 HAL 调用（可能与主循环的 HAL 调用冲突）

### 4. 全局状态滥用

- **禁止** 多个 `.c` 文件重复定义同名全局变量（链接错误）
- **禁止** 多任务并发写同一全局变量（即便当前是协作式调度，仍需保持单写者以便未来切 RTOS）
- **禁止** 使用 `malloc` / `free` 进行动态内存分配（Cortex-M3 上堆管理不可预测，项目所有缓冲区必须是静态/全局）

### 5. cppcheck 告警

CI 启用 `--enable=warning,performance,portability`，常见命中：

- 未初始化变量
- 内存越界访问（特别是 `memcpy` / 数组下标）
- 未使用的函数 / 变量（`static` 函数应在模块内引用）
- 可移植性问题（如假设 `int` 为 32 位 — 在 Cortex-M3 上成立，但代码应明确用 `uint32_t`）

### 6. 提交消息违规

- **禁止** 非 Conventional Commits 格式的提交消息
- **禁止** 英文提交消息（项目约定中文描述）
- **禁止** 提交 secrets、私钥、`.env` 类文件

---

## Required Patterns

### 1. 头文件守卫（CI 强制）

每个 `APP/*.h` 必须以文件名大写 + `_H` 作为守卫：

```c
#ifndef MPU6050_H
#define MPU6050_H

/* ... */

#endif /* MPU6050_H */
```

**C++ 兼容**（推荐）：

```c
#ifdef __cplusplus
extern "C" {
#endif

/* 声明 */

#ifdef __cplusplus
}
#endif
```

### 2. 应用层统一入口

所有应用模块头文件必须在 `APP/bsp_system.h` 中 `#include`。模块内部只需 `#include "bsp_system.h"` 即可获得所有依赖。

### 3. CubeMX 用户代码边界

在 `Core/` 下添加用户代码**必须**包裹在 CubeMX 标记内：

```c
/* USER CODE BEGIN 2 */
mpu6050_init();
scheduler_init();
/* USER CODE END 2 */
```

### 4. 函数注释（Doxygen 格式）

导出函数必须有中文 Doxygen 注释：

```c
/**
 * @brief       调度器运行函数
 * @param       无
 * @retval      无
 */
void scheduler_run(void) { ... }
```

### 5. 提交消息（Conventional Commits）

```
<type>[optional scope]: <中文描述>

[可选正文]
```

**允许的 type**：`feat` / `fix` / `docs` / `refactor` / `test` / `chore` / `ci`

**示例**：
- `feat(mpu6050): 新增跌倒检测逻辑`
- `fix(ringbuffer): 修复满时写入不返回错误的问题`
- `refactor(scheduler): 支持动态添加任务`
- `docs: 同步 README 外设配置说明`

### 6. 代码风格

- **缩进**：4 空格，禁用 Tab
- **大括号**：函数体 `{` 另起一行；`if/for/while` 的 `{` 同行
- **命名**：函数/变量 `snake_case`，宏/常量 `UPPER_SNAKE_CASE`，类型 `_t` 后缀
- **模块前缀**：所有导出符号以模块名前缀开头（详见 `directory-structure.md`）

---

## Testing Requirements

**当前项目无自动化测试框架**——嵌入式硬件依赖使单元测试成本高。验证策略：

### 1. 手动测试（必须）
- Keil Debug 模式下通过 SWD 断点验证关键路径
- 串口（USART1 115200）观察 `printf` 输出验证传感器值
- 真机测试跨越至少一个完整调度周期（最长任务周期 ×2）

### 2. 静态分析（CI 强制）
- cppcheck 零告警是合并的必要条件
- 新增模块需在 PR 描述中说明：是否修改 `.ioc`、是否新增外设依赖

### 3. 回归风险点（Review 时重点确认）
- 修改调度器 → 所有任务周期是否仍满足实时性
- 修改 HAL 调用 → 是否影响中断优先级
- 修改全局变量 → 消费者模块是否同步更新

### 4. 未来扩展（不强制）
- 可考虑 Ceedling / Unity 做纯算法逻辑（NMEA 解析、环形缓冲区）的 PC 侧单元测试
- 仅限与硬件解耦的纯 C 函数

---

## Code Review Checklist

PR 作者与审阅者按以下清单逐项确认：

### 编译与 CI
- [ ] Keil 编译零警告
- [ ] CI Quality Gate 通过（cppcheck + 头文件守卫 + 编码规范）
- [ ] 未提交 `.o/.d/.axf/.map` 等中间文件（必要时运行 `Keilkill.bat`）

### 文件组织
- [ ] 新模块在 `APP/` 目录下，`.c`/`.h` 成对
- [ ] 新头文件已 include 到 `APP/bsp_system.h`
- [ ] Keil 工程已添加新源文件与头文件路径
- [ ] 头文件含正确守卫与 `extern "C"` 块

### 代码规范
- [ ] 函数/变量命名遵循 `snake_case` + 模块前缀
- [ ] 宏/常量 `UPPER_SNAKE_CASE`
- [ ] 导出函数含 Doxygen `@brief`/`@param`/`@retval` 中文注释
- [ ] 4 空格缩进、大括号风格符合约定

### 外设与中断
- [ ] CubeMX 相关修改通过 `.ioc` 完成，未手动修改 `Core/` 下非 `USER CODE` 块代码
- [ ] 中断回调无阻塞操作（无 `printf` / `HAL_Delay` / 长轮询）
- [ ] 新增 HAL 调用均检查返回值

### 错误处理
- [ ] 初始化失败不阻塞主流程（可降级）
- [ ] 未调用 `Error_Handler()` 于应用层
- [ ] 全局变量单一写者

### 日志与调试
- [ ] 高频任务（≤10ms）中无 `printf`（或已加级别控制）
- [ ] 无遗留的调试 `printf("test...")`
- [ ] 日志无敏感信息（凭据、精确坐标等）

### 提交与文档
- [ ] 提交消息遵循 Conventional Commits 中文格式
- [ ] `README.md` 在外设/模块变动时同步更新
- [ ] `CLAUDE.md` 在架构/约定变动时同步更新
- [ ] `dev_log.md` 本地记录变更（不入库）
