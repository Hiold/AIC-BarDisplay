/**
 * @file dash_hmi.h
 * @brief 电摩仪表主界面组装与数据源
 */
#ifndef DASH_HMI_H
#define DASH_HMI_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 屏幕尺寸 */
#define DASH_SCREEN_W   320
#define DASH_SCREEN_H   1280

/**
 * @brief 创建电摩仪表主界面（挂到 scr 上）
 * @param scr 目标 screen 对象
 */
void dash_hmi_create(lv_obj_t *scr);

/**
 * @brief 启动演示数据源（正弦模拟数据）。
 *        真实移植时可改为从 IMU/磁力计回调调用 dash_data_*.h 中的 set 接口。
 */
void dash_hmi_start_demo(void);

#ifdef __cplusplus
}
#endif

#endif /* DASH_HMI_H */