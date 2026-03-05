#include "scheduler.h"

uint8_t task_number = 0;        // 全局变量，用于存储用户任务数量

typedef struct {
    void (*task_func)(void);    // 任务函数
    uint32_t period_ms;         // 任务执行周期（ms）
    uint32_t last_run_time;     // 上次执行时间（ms）
}task_t;


/* 静态调度器任务列表，每个任务包括任务函数、执行周期（ms）、上次执行时间（ms） */
static task_t scheduler_task[] = {
    {mq2_task, 100, 0},         // MQ2 传感器任务，每 100 ms 执行一次
    {dht11_task, 1000, 0},      // DHT11 传感器任务，每 1000 ms 执行一次
};

/**
 * @brief       调度器初始化函数
 * 
 * @param       无
 * @retval      无
 */
void scheduler_init()
{
    task_number = sizeof(scheduler_task) / sizeof(task_t);
}

/**
 * @brief       调度器运行函数
 * @param       无
 * @retval      无
 */
void scheduler_run()
{
    for (uint8_t i = 0; i < task_number; i++)
    {
        uint32_t now_time = HAL_GetTick();

        if (now_time >= scheduler_task[i].last_run_time + scheduler_task[i].period_ms)
        {
            scheduler_task[i].last_run_time = now_time;     // 更新任务的上次运行时间为当前时间
            scheduler_task[i].task_func();                  // 执行任务函数
        }
    }
}
