#ifndef LCD_APP_H
#define LCD_APP_H

#include "bsp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

void lcd_app_init(void);         // 初始化 LCD 业务显示页面
void lcd_app_task(void);         // 200ms 刷新传感器数据显示

#ifdef __cplusplus
}
#endif

#endif /* LCD_APP_H */
