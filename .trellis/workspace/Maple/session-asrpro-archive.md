### Summary

ASRPro 离线语音模块接入 STM32 USART1：硬件实机已完成 `led_on` / `led_off` / `motor_speed_0..3` 命令验证，PRD 全部 12 项 AC 勾选完毕，task.json 状态推进至 done/phase 6 并归档到 `archive/2026-05/`。

### Main Changes

| Item | Details |
|------|---------|
| 实现入口 | 新增 `APP/asrpro.c` / `APP/asrpro.h`，128B RX 环形缓冲 + 48B 行缓冲，单字节 IT 收 + 调度器侧解析 |
| 命令分发 | LED 走 `helmet_alarm_set_base_led()` 保留报警优先级；电机走 `pwm_motor_set_speed()`，档位映射 `{0, 33, 66, 100}` |
| 解析策略 | `memcmp` 严格等长匹配（小写命令 + 数字尾），裁剪首尾 ASCII 空白与 CRLF；超长行进入 `overflow` 截断丢弃直至下一 `\n` |
| 中断侧 | `HAL_UART_RxCpltCallback` 仅 push 字节并 `HAL_UART_Receive_IT` 重启接收；`HAL_UART_ErrorCallback` 在 USART1 出错时恢复接收，USART2 路径未动 |
| 调试开关 | `ASRPRO_ENABLE_COMMAND_EXECUTION`（默认 1，关 0 用于临时 USART1 调试）+ `ASRPRO_ENABLE_USART1_DEBUG`（默认 0，控制 `printf` / `m100pg` 转发）|
| Cube 集成 | `Core/Src/usart.c` 在 USER 块启用 USART1 IRQ；`stm32f1xx_it.c` 增加 `USART1_IRQHandler`；`Core/Src/main.c` 在用户块调用 `asrpro_init()` 并按宏切换 boot 日志 |
| 工程注册 | `APP/bsp_system.h` include、`APP/scheduler.c` 加 `asrpro_task` (10ms)、`MDK-ARM/helmet.uvprojx` 加源文件 |
| 文档 | `README.md`、`CLAUDE.md` 同步外设表 / 模块表 / 执行流程；PRD 勾 12/12 AC 并写 Manual Verification |
| 归档 | task.json 推进 `planning → done`、`current_phase 0 → 6`、`commit=62a2c3b`、`completedAt=2026-05-05`，由 `task.py archive` 自动迁移到 `archive/2026-05/` |

### Testing

- [OK] 用户在 Keil 完成 Build 并烧录目标板，实机验证 `led_on` / `led_off` / `motor_speed_0..3` 语音命令驱动 RGB LED 与 PWM 电机正常
- [OK] 用户确认 USART2 4G 上传与下行 LED/motor 命令在 ASRPro 接入后行为不变
- [OK] 报警仲裁仍生效：跌倒/碰撞下语音 `led_off` 不抑制红灯指示
- [SKIP] 本机无 cppcheck，未做静态分析；按代码层 `memcmp` 严格匹配 + 调度器侧解析达成

### Status

[OK] **Completed**

### Next Steps

- None - task archived
