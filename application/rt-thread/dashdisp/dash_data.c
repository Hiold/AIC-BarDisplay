/**
 * @file dash_data.c
 * @brief 电摩仪表数据结构与更新接口实现
 */
#include "dash_data.h"

dash_data_t g_dash;

/* 限制值到 [min, max] */
static float clampf(float v, float mn, float mx)
{
    if (v < mn) return mn;
    if (v > mx) return mx;
    return v;
}

/* 单轴滑动平均采样并入 */
static void accel_push(float *buf, uint8_t *idx, float val)
{
    buf[*idx] = val;
    *idx = (uint8_t)((*idx + 1) % DASH_SMOOTH_N);
}

/* 计算缓冲均值；未满时用已有值 */
static float accel_mean(const float *buf)
{
    float sum = 0.0f;
    for (int i = 0; i < DASH_SMOOTH_N; i++) sum += buf[i];
    return sum / DASH_SMOOTH_N;
}

void dash_data_init(void)
{
    g_dash.heading_deg = 0.0f;
    g_dash.pitch_deg   = 0.0f;
    g_dash.roll_deg    = 0.0f;
    g_dash.tilt_deg    = 0.0f;
    g_dash.accel_gx = g_dash.accel_gy = g_dash.accel_gz = 0.0f;
    g_dash.accel_smooth_idx = 0;
    for (int i = 0; i < DASH_SMOOTH_N; i++) {
        g_dash.accel_smooth_gx[i] = 0.0f;
        g_dash.accel_smooth_gy[i] = 0.0f;
        g_dash.accel_smooth_gz[i] = 0.0f;
    }
}

void dash_set_heading(float heading_deg)
{
    float h = heading_deg;
    while (h < 0.0f)   h += 360.0f;
    while (h >= 360.0f) h -= 360.0f;
    g_dash.heading_deg = h;
}

void dash_set_pitch(float pitch_deg)
{
    g_dash.pitch_deg = clampf(pitch_deg, -90.0f, 90.0f);
}

void dash_set_roll(float roll_deg)
{
    g_dash.roll_deg = clampf(roll_deg, -180.0f, 180.0f);
}

void dash_set_tilt(float tilt_deg)
{
    g_dash.tilt_deg = clampf(tilt_deg, -45.0f, 45.0f);
}

void dash_set_accel(float gx, float gy, float gz)
{
    g_dash.accel_gx = gx;
    g_dash.accel_gy = gy;
    g_dash.accel_gz = gz;

    accel_push(g_dash.accel_smooth_gx, &g_dash.accel_smooth_idx, gx);
    accel_push(g_dash.accel_smooth_gy, &g_dash.accel_smooth_idx, gy);
    accel_push(g_dash.accel_smooth_gz, &g_dash.accel_smooth_idx, gz);

    /* 立即更新均值到当前值 */
    g_dash.accel_gx = accel_mean(g_dash.accel_smooth_gx);
    g_dash.accel_gy = accel_mean(g_dash.accel_smooth_gy);
    g_dash.accel_gz = accel_mean(g_dash.accel_smooth_gz);
}