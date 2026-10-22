/**
 * @file dash_data.h
 * @brief 电摩仪表数据接口
 *
 * 用户 MCU / 传感器模块只需调用本文件提供的 set 接口刷新数据，
 * LVGL 定时器会自动把它们渲染到屏幕。
 */
#ifndef DASH_DATA_H
#define DASH_DATA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 三轴缓冲点数（用于平滑） */
#define DASH_SMOOTH_N   8

/* 仪表可选显示单元 */
typedef enum {
    DASH_UNIT_DEG = 0,   /* 角度 ° */
    DASH_UNIT_G          /* 加速度 G */
} dash_unit_t;

/**
 * @brief 电摩仪表数据集合
 */
typedef struct {
    /* 磁方位 0-359.9°，顺时针，北=0 */
    float    heading_deg;
    /* 俯仰角 -90~+90°，抬头为正 */
    float    pitch_deg;
    /* 横滚角 -180~+180°，右倾为正 */
    float    roll_deg;
    /* 车身倾角 -45~+45°，左倾为负、右倾为正 */
    float    tilt_deg;
    /* 三轴加速度，单位 G（重力加速度） */
    float    accel_gx;
    float    accel_gy;
    float    accel_gz;
    /* 三轴加速度的一次滑动平均缓冲 */
    float    accel_smooth_gx[DASH_SMOOTH_N];
    float    accel_smooth_gy[DASH_SMOOTH_N];
    float    accel_smooth_gz[DASH_SMOOTH_N];
    uint8_t  accel_smooth_idx;
} dash_data_t;

/** 全局数据实例（由 dash_hmi 初始化） */
extern dash_data_t g_dash;

/** 初始化数据（清零 + 单位） */
void dash_data_init(void);

/* ---- 数据更新接口：供外部 MCU 调用 ---- */

/** 设置磁方位角 */
void dash_set_heading(float heading_deg);
/** 设置俯仰角 */
void dash_set_pitch(float pitch_deg);
/** 设置横滚角 */
void dash_set_roll(float roll_deg);
/** 设置车身倾角 */
void dash_set_tilt(float tilt_deg);
/** 设置三轴加速度 (G) */
void dash_set_accel(float gx, float gy, float gz);

#ifdef __cplusplus
}
#endif

#endif /* DASH_DATA_H */