#ifndef M100PG_BSP_H
#define M100PG_BSP_H

/*
 * 协议库 ↔ 板级硬件桥接层
 *
 * 职责：
 *   - 持有 m100pg_proto_t 全局实例（g_m100pg_proto）
 *   - 在 m100pg_bsp_init() 注册回调，把协议库的语义事件
 *     (led_set / motor_set_speed / collect_sample / send_frame)
 *     绑到本工程的具体驱动上
 *
 * 这是唯一同时 #include 协议库与各传感器/执行器驱动头的文件，
 * 让 m100pg_protocol.{c,h} 保持纯粹（可在 PC 上 gcc 编译跑测试）。
 */

#include "m100pg_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 全局协议库实例。m100pg.c 的 task 通过它驱动 feed/publish。 */
extern m100pg_proto_t g_m100pg_proto;

/* 注册回调并初始化 g_m100pg_proto。在 m100pg_init 内部被调用。 */
void m100pg_bsp_init(void);

/* 调度器周期任务：拉一帧 telemetry 并通过 USART2 上传。
 * 在 scheduler.c 的任务表里以固定周期注册（建议 1000 ms）。 */
void m100pg_bsp_publish_task(void);

#ifdef __cplusplus
}
#endif

#endif /* M100PG_BSP_H */
