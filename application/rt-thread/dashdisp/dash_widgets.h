/**
 * @file dash_widgets.h
 * @brief 电摩仪表自定义仪表控件（Canvas 绘制）
 *
 * 提供罗盘、姿态球、倾角表、加速度条四种控件，
 * 每个控件是一个 LVGL 对象，内部用 Canvas + 定时重绘。
 */
#ifndef DASH_WIDGETS_H
#define DASH_WIDGETS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============ ① 磁方位罗盘 ============ */
/**
 * 创建罗盘控件（横向罗盘带，中心指向当前航向）
 * parent: 父对象
 * width/height: 控件尺寸
 */
lv_obj_t *dash_compass_create(lv_obj_t *parent, lv_coord_t width, lv_coord_t height);

/**
 * 设置罗盘当前航向角（度）
 */
void dash_compass_set_heading(lv_obj_t *obj, float heading_deg);

/* ============ ② 姿态指示 ============ */
lv_obj_t *dash_attitude_create(lv_obj_t *parent, lv_coord_t width, lv_coord_t height);

void dash_attitude_set(lv_obj_t *obj, float pitch_deg, float roll_deg);

/* ============ ③ 车身倾角 ============ */
lv_obj_t *dash_tilt_create(lv_obj_t *parent, lv_coord_t width, lv_coord_t height);
void dash_tilt_set(lv_obj_t *obj, float tilt_deg);

/* ============ ④ 加速度 ============ */
lv_obj_t *dash_accel_create(lv_obj_t *parent, lv_coord_t width, lv_coord_t height);
void dash_accel_set(lv_obj_t *obj, float gx, float gy, float gz);

#ifdef __cplusplus
}
#endif

#endif /* DASH_WIDGETS_H */