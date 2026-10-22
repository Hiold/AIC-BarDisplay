/**
 * @file dash_hmi.c
 * @brief 电摩仪表主界面：把四个仪表控件按 320×1280 组装并驱动数据。
 */
#include "dash_hmi.h"
#include "dash_data.h"
#include "dash_widgets.h"
#include <math.h>

typedef struct {
    lv_obj_t *compass;
    lv_obj_t *attitude;
    lv_obj_t *tilt;
    lv_obj_t *accel;
} dash_hmi_t;

static dash_hmi_t s_hmi;
static lv_timer_t *s_demo_timer = NULL;
static float s_t = 0.0f;

void dash_hmi_create(lv_obj_t *scr)
{
    dash_data_init();

    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0F14), 0);

    /* 分区高度（320 宽） */
    lv_coord_t h_compass = 300;
    lv_coord_t h_att     = 300;
    lv_coord_t h_tilt    = 350;
    lv_coord_t h_accel   = DASH_SCREEN_H - (h_compass + h_att + h_tilt);

    /* ① 磁方位罗盘 */
    s_hmi.compass = dash_compass_create(scr, DASH_SCREEN_W, h_compass);
    lv_obj_set_pos(s_hmi.compass, 0, 0);

    /* ② 姿态 */
    s_hmi.attitude = dash_attitude_create(scr, DASH_SCREEN_W, h_att);
    lv_obj_set_pos(s_hmi.attitude, 0, h_compass);

    /* ③ 倾角 */
    s_hmi.tilt = dash_tilt_create(scr, DASH_SCREEN_W, h_tilt);
    lv_obj_set_pos(s_hmi.tilt, 0, h_compass + h_att);

    /* ④ 加速度 */
    s_hmi.accel = dash_accel_create(scr, DASH_SCREEN_W, h_accel);
    lv_obj_set_pos(s_hmi.accel, 0, h_compass + h_att + h_tilt);

    /* 用真实数据单次绘制（此时为初始 0 值） */
    dash_compass_set_heading(s_hmi.compass, g_dash.heading_deg);
    dash_attitude_set(s_hmi.attitude, g_dash.pitch_deg, g_dash.roll_deg);
    dash_tilt_set(s_hmi.tilt, g_dash.tilt_deg);
    dash_accel_set(s_hmi.accel, g_dash.accel_gx, g_dash.accel_gy, g_dash.accel_gz);
}

static void demo_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_t += 0.05f;

    /* 航向：缓慢变化 */
    float heading = fmodf(s_t * 12.0f, 360.0f);

    /* 俯仰/横滚 */
    float pitch = 15.0f * sinf(s_t * 0.6f);
    float roll  = 25.0f * sinf(s_t * 0.4f + 1.0f);

    /* 倾角 */
    float tilt  = 30.0f * sinf(s_t * 0.5f);

    /* 加速度 */
    float gx = 0.8f  * sinf(s_t * 0.9f);
    float gy = 0.5f  * sinf(s_t * 0.7f + 2.0f);
    float gz = 1.0f  + 0.3f * sinf(s_t * 2.0f);

    dash_set_heading(heading);
    dash_set_pitch(pitch);
    dash_set_roll(roll);
    dash_set_tilt(tilt);
    dash_set_accel(gx, gy, gz);

    dash_compass_set_heading(s_hmi.compass, g_dash.heading_deg);
    dash_attitude_set(s_hmi.attitude, g_dash.pitch_deg, g_dash.roll_deg);
    dash_tilt_set(s_hmi.tilt, g_dash.tilt_deg);
    dash_accel_set(s_hmi.accel, g_dash.accel_gx, g_dash.accel_gy, g_dash.accel_gz);
}

void dash_hmi_start_demo(void)
{
    if (s_demo_timer) lv_timer_del(s_demo_timer);
    s_demo_timer = lv_timer_create(demo_timer_cb, 50, NULL);
}