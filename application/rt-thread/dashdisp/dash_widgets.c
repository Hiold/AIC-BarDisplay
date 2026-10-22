/**
 * @file dash_widgets.c
 * @brief 电摩仪表自定义仪表控件实现（LVGL 9.x Canvas 绘制）
 *
 * 从 LVGL 8.3 移植到 ArtInChip 定制 LVGL 9.1：
 *  - v8 的 lv_canvas_draw_line/arc/rect/polygon/text 在 v9 被移除，
 *    改为 lv_canvas_init_layer + 通用 lv_draw_* 绘制到 layer，再
 *    lv_canvas_finish_layer 提交。
 *  - buffer 用 lv_draw_buf_create(ARGB8888) 分配（带对齐）。
 *
 * 每个控件 = 一个 lv_obj（内含 canvas）+ 自持 layer。
 */
#include "dash_widgets.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DASH_COLOR_BG        lv_color_hex(0x0A0F14)
#define DASH_COLOR_PANEL     lv_color_hex(0x101A22)
#define DASH_COLOR_PRIMARY   lv_color_hex(0x33E7C4)   /* 青绿 */
#define DASH_COLOR_SECONDARY lv_color_hex(0x3AA5FF)   /* 蓝 */
#define DASH_COLOR_WARN      lv_color_hex(0xFF9F33)   /* 橙 */
#define DASH_COLOR_DANGER    lv_color_hex(0xFF4D4D)   /* 红 */
#define DASH_COLOR_TEXT      lv_color_hex(0xEAF6F2)
#define DASH_COLOR_TEXT_DIM  lv_color_hex(0x7A8B96)
#define DASH_COLOR_TICK      lv_color_hex(0x2A3A46)
#define DASH_COLOR_SKY       lv_color_hex(0x1E3A57)
#define DASH_COLOR_GROUND    lv_color_hex(0x3A2F22)

#define DASH_PI  3.14159265358979f

/* ================= 通用 Canvas 工具 ================= */

typedef struct {
    lv_obj_t      *canvas;
    lv_draw_buf_t *draw_buf;
    lv_layer_t     layer;
    lv_coord_t     w;
    lv_coord_t     h;
} dash_canvas_t;

static void dash_canvas_alloc(dash_canvas_t *cv, lv_obj_t *parent,
                              lv_coord_t w, lv_coord_t h)
{
    cv->w = w;
    cv->h = h;
    cv->draw_buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    cv->canvas = lv_canvas_create(parent);
    lv_obj_set_size(cv->canvas, w, h);
    lv_canvas_set_draw_buf(cv->canvas, cv->draw_buf);
}

/* 开始一帧：清底 + 初始化绘制 layer */
static void dash_canvas_begin(dash_canvas_t *cv, lv_color_t bg, lv_opa_t bg_opa)
{
    lv_canvas_fill_bg(cv->canvas, bg, bg_opa);
    lv_canvas_init_layer(cv->canvas, &cv->layer);
}

/* 结束一帧：提交绘制并请求刷新 */
static void dash_canvas_end(dash_canvas_t *cv)
{
    lv_canvas_finish_layer(cv->canvas, &cv->layer);
    lv_obj_invalidate(cv->canvas);
}

/* 从中心按 LVGL 角（0=3点，逆时针增大，90=12点）+半径得到屏幕点 */
static void lvgl_angle_to_screen(float cx, float cy, float r, uint16_t ang,
                                 float *out_x, float *out_y)
{
    float rad = (float)ang * DASH_PI / 180.0f;
    *out_x = cx + r * cosf(rad);
    *out_y = cy - r * sinf(rad);
}

static void dash_draw_line(dash_canvas_t *cv, float x0, float y0, float x1, float y1,
                           lv_coord_t width, lv_color_t color, lv_opa_t opa)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.p1.x = x0;
    dsc.p1.y = y0;
    dsc.p2.x = x1;
    dsc.p2.y = y1;
    dsc.width = width;
    dsc.color = color;
    dsc.opa = opa;
    dsc.round_start = 1;
    dsc.round_end = 1;
    lv_draw_line(&cv->layer, &dsc);
}

static void dash_draw_arc(dash_canvas_t *cv, float cx, float cy, lv_coord_t radius,
                          uint16_t start_ang, uint16_t end_ang,
                          lv_coord_t width, lv_color_t color, lv_opa_t opa, bool rounded)
{
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center.x = (lv_coord_t)roundf(cx);
    dsc.center.y = (lv_coord_t)roundf(cy);
    dsc.radius = radius;
    dsc.start_angle = start_ang;
    dsc.end_angle = end_ang;
    dsc.width = width;
    dsc.color = color;
    dsc.opa = opa;
    dsc.rounded = rounded ? 1 : 0;
    lv_draw_arc(&cv->layer, &dsc);
}

static void dash_draw_disc(dash_canvas_t *cv, float cx, float cy, lv_coord_t r,
                           lv_color_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = r;
    lv_area_t area;
    area.x1 = (lv_coord_t)roundf(cx - r);
    area.y1 = (lv_coord_t)roundf(cy - r);
    area.x2 = (lv_coord_t)roundf(cx + r);
    area.y2 = (lv_coord_t)roundf(cy + r);
    lv_draw_rect(&cv->layer, &dsc, &area);
}

static void dash_draw_text(dash_canvas_t *cv, lv_coord_t x, lv_coord_t y,
                           const lv_font_t *font, const char *txt,
                           lv_color_t color, lv_opa_t opa)
{
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.color = color;
    dsc.font  = font;
    dsc.opa   = opa;
    /* text 多来自栈上临时 buffer，必须拷贝到内部 */
    dsc.text = txt;
    dsc.text_local = 1;
    lv_area_t coords;
    coords.x1 = x;
    coords.y1 = y;
    coords.x2 = x + 400;
    coords.y2 = y + 60;
    lv_draw_label(&cv->layer, &dsc, &coords);
}

/* 把三角形画到 layer（v9 lv_draw_triangle，替代 v8 的多边形绘制） */
static void dash_draw_triangle(dash_canvas_t *cv, const lv_point_t *p0,
                               const lv_point_t *p1, const lv_point_t *p2,
                               lv_color_t color, lv_opa_t opa)
{
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.p[0].x = p0->x;
    dsc.p[0].y = p0->y;
    dsc.p[1].x = p1->x;
    dsc.p[1].y = p1->y;
    dsc.p[2].x = p2->x;
    dsc.p[2].y = p2->y;
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    lv_draw_triangle(&cv->layer, &dsc);
}

static void dash_draw_polygon(dash_canvas_t *cv, const lv_point_t *pts,
                              uint32_t cnt, lv_color_t color, lv_opa_t opa)
{
    /* 原 v8 用法中 cnt==3（指北三角/飞机符号）；用三角扇覆盖多边形的其余点。 */
    if (cnt < 3) return;
    for (uint32_t i = 1; i + 1 < cnt; i++) {
        dash_draw_triangle(cv, &pts[0], &pts[i], &pts[i + 1], color, opa);
    }
}

static inline bool is_cardinal(const char *s)
{
    return (s[0] == 'N' || s[0] == 'E' || s[0] == 'S' || s[0] == 'W');
}

/* ================= ① 磁方位罗盘 ================= */

typedef struct {
    dash_canvas_t cv;
    float heading_deg;
} dash_compass_t;

static void compass_redraw(lv_obj_t *obj)
{
    dash_compass_t *c = (dash_compass_t *)obj->user_data;
    if (!c) return;
    const lv_coord_t W = c->cv.w;
    const lv_coord_t H = c->cv.h;
    float h = c->heading_deg;

    dash_canvas_begin(&c->cv, DASH_COLOR_BG, LV_OPA_COVER);

    const float window_deg = 120.0f;              /* 可见窗口 ±60° */
    const float px_per_deg = (float)W / window_deg;
    const lv_coord_t band_y = 92;                 /* 刻度带中心线 */

    /* 画刻度 */
    for (float d = h - 60.0f; d <= h + 60.0f; d += 5.0f) {
        float diff = d - h;
        float sx = (float)W / 2.0f + diff * px_per_deg;
        if (sx < -6 || sx > W + 6) continue;
        int d_int = ((int)floorf(d)) % 360;
        if (d_int < 0) d_int += 360;
        int mod30 = d_int % 30;
        bool major = (mod30 == 0);
        lv_coord_t tick_len = major ? 16 : 8;
        lv_color_t col = major ? DASH_COLOR_PRIMARY : DASH_COLOR_TICK;
        lv_coord_t tw = major ? 2 : 1;
        dash_draw_line(&c->cv, sx, band_y - 30, sx, band_y - 30 + tick_len,
                       tw, col, major ? 220 : 140);

        if (major) {
            char label[8];
            const char *txt;
            switch (d_int) {
                case 0:   txt = "N"; break;
                case 90:  txt = "E"; break;
                case 180: txt = "S"; break;
                case 270: txt = "W"; break;
                default:
                    snprintf(label, sizeof(label), "%d", d_int);
                    txt = label;
                    break;
            }
            dash_draw_text(&c->cv, (lv_coord_t)sx - 14, band_y - 56, &lv_font_montserrat_12,
                           txt, is_cardinal(txt) ? DASH_COLOR_SECONDARY : DASH_COLOR_TEXT_DIM, 210);
        }
    }

    /* 罗盘带上下沿 */
    dash_draw_line(&c->cv, 8, band_y - 36, W - 8, band_y - 36, 1, DASH_COLOR_TICK, 100);

    /* 顶部航向数字 */
    char head[16];
    snprintf(head, sizeof(head), "%03d°", (int)roundf(h));
    dash_draw_text(&c->cv, W / 2 - 30, 10, &lv_font_montserrat_16, head, DASH_COLOR_TEXT, 255);

    /* 中央指北三角（当前方位） */
    {
        lv_point_t tri[3];
        tri[0].x = W / 2;
        tri[0].y = band_y - 40;
        tri[1].x = W / 2 - 7;
        tri[1].y = band_y - 28;
        tri[2].x = W / 2 + 7;
        tri[2].y = band_y - 28;
        dash_draw_polygon(&c->cv, tri, 3, DASH_COLOR_PRIMARY, LV_OPA_COVER);
        /* 在带中央画细指针线 */
        dash_draw_line(&c->cv, W / 2, band_y - 26, W / 2, band_y + 2, 2, DASH_COLOR_PRIMARY, 200);
    }

    (void)H;
    dash_canvas_end(&c->cv);
}

lv_obj_t *dash_compass_create(lv_obj_t *parent, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, width, height);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, DASH_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);

    dash_compass_t *c = (dash_compass_t *)calloc(1, sizeof(dash_compass_t));
    dash_canvas_alloc(&c->cv, obj, width, height);
    lv_obj_set_pos(c->cv.canvas, 0, 0);
    obj->user_data = c;

    compass_redraw(obj);
    return obj;
}

void dash_compass_set_heading(lv_obj_t *obj, float heading_deg)
{
    dash_compass_t *c = (dash_compass_t *)obj->user_data;
    if (!c) return;
    c->heading_deg = heading_deg;
    compass_redraw(obj);
}

/* ================= ② 姿态指示 ================= */

typedef struct {
    dash_canvas_t cv;
    float pitch_deg;
    float roll_deg;
} dash_attitude_t;

static void attitude_redraw(lv_obj_t *obj)
{
    dash_attitude_t *a = (dash_attitude_t *)obj->user_data;
    if (!a) return;
    const lv_coord_t W = a->cv.w;
    const lv_coord_t H = a->cv.h;
    float pitch = a->pitch_deg;
    float roll  = a->roll_deg;

    dash_canvas_begin(&a->cv, DASH_COLOR_BG, LV_OPA_COVER);

    const float cx = W / 2.0f;
    const float cy = H / 2.0f + 6;
    const lv_coord_t R = (lv_coord_t)roundf((W / 2.0f) * 0.92f);
    float rad = roll * DASH_PI / 180.0f;
    float ca = cosf(rad), sa = sinf(rad);
    float pitch_px = pitch * 2.2f;        /* 俯仰对应像素 */
    float hl0 = cy + pitch_px;            /* 地平线屏坐标 */

    /* 1) 姿态球外框：以天空色填充圆盘 */
    dash_draw_disc(&a->cv, cx, cy, R, DASH_COLOR_SKY, LV_OPA_COVER);

    /* 2) 地面：地平线斜线 + 其下方逐行近似带 */
    {
        float len = R * 1.3f;
        float dx = sinf(rad);
        float dy = cosf(rad);
        float hx1 = cx - dx * len;
        float hy1 = hl0 - dy * len;
        float hx2 = cx + dx * len;
        float hy2 = hl0 + dy * len;
        if (hy1 > cy + R) hy1 = cy + R;
        if (hy2 > cy + R) hy2 = cy + R;
        if (hy1 < cy - R) hy1 = cy - R;
        if (hy2 < cy - R) hy2 = cy - R;
        dash_draw_line(&a->cv, hx1, hy1, hx2, hy2, 3, DASH_COLOR_TEXT, 255);
        dash_draw_line(&a->cv, hx1, hy1, hx2, hy2, 1, DASH_COLOR_WARN, 120);

        float ystep = 5.0f;
        for (float yy = hl0; yy <= cy + R; yy += ystep) {
            float dyv = yy - cy;
            float halfw = (fabsf(dyv) <= R) ? sqrtf((float)(R * R) - dyv * dyv) : 0.0f;
            if (halfw > 0) {
                float alpha = 60.0f + (yy - hl0) * 0.6f;
                if (alpha > 200.0f) alpha = 200.0f;
                dash_draw_line(&a->cv, cx - halfw, yy, cx + halfw, yy, ystep,
                               DASH_COLOR_GROUND, (lv_opa_t)alpha);
            }
        }
    }

    /* 3) pitch 刻度线（旋转 roll） */
    for (int deg = -30; deg <= 30; deg += 10) {
        if (deg == 0) continue;
        float yy = cy + (float)deg * 2.2f + pitch_px;
        float dyv = yy - cy;
        if (fabsf(dyv) > R) continue;
        float halfw = (deg % 20 == 0) ? 24 : 13;
        float p0x = -halfw, p0y =  yy - cy;
        float p1x =  halfw, p1y =  yy - cy;
        float rx0 = cx + p0x * ca - p0y * sa;
        float ry0 = cy + p0x * sa + p0y * ca;
        float rx1 = cx + p1x * ca - p1y * sa;
        float ry1 = cy + p1x * sa + p1y * ca;
        dash_draw_line(&a->cv, rx0, ry0, rx1, ry1, 2, DASH_COLOR_TEXT_DIM, 210);
        char lbl[8];
        snprintf(lbl, sizeof(lbl), "%d", abs(deg));
        dash_draw_text(&a->cv, (lv_coord_t)rx1 + 4, (lv_coord_t)ry1 - 8,
                       &lv_font_montserrat_12, lbl, DASH_COLOR_TEXT_DIM, 210);
    }

    /* 4) 球体边框 */
    dash_draw_arc(&a->cv, cx, cy, R, 0, 360, 3, DASH_COLOR_SECONDARY, 210, true);

    /* 5) 中央固定飞机符号（不随姿态转） */
    {
        lv_point_t tri[3];
        tri[0].x = (lv_coord_t)cx;
        tri[0].y = (lv_coord_t)(cy - 20);
        tri[1].x = (lv_coord_t)(cx - 10);
        tri[1].y = (lv_coord_t)cy;
        tri[2].x = (lv_coord_t)(cx + 10);
        tri[2].y = (lv_coord_t)cy;
        dash_draw_polygon(&a->cv, tri, 3, DASH_COLOR_PRIMARY, LV_OPA_COVER);
        dash_draw_line(&a->cv, cx - 10, cy, cx - 22, cy + 6, 3, DASH_COLOR_PRIMARY, 220);
        dash_draw_line(&a->cv, cx + 10, cy, cx + 22, cy + 6, 3, DASH_COLOR_PRIMARY, 220);
    }

    /* 6) 数值读出（底部） */
    char txt[24];
    snprintf(txt, sizeof(txt), "P %+.0f  R %+.0f", pitch, roll);
    dash_draw_text(&a->cv, 8, H - 20, &lv_font_montserrat_12, txt, DASH_COLOR_TEXT, 230);

    dash_canvas_end(&a->cv);
}

lv_obj_t *dash_attitude_create(lv_obj_t *parent, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, width, height);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, DASH_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    dash_attitude_t *a = (dash_attitude_t *)calloc(1, sizeof(dash_attitude_t));
    dash_canvas_alloc(&a->cv, obj, width, height);
    lv_obj_set_pos(a->cv.canvas, 0, 0);
    obj->user_data = a;
    attitude_redraw(obj);
    return obj;
}

void dash_attitude_set(lv_obj_t *obj, float pitch_deg, float roll_deg)
{
    dash_attitude_t *a = (dash_attitude_t *)obj->user_data;
    if (!a) return;
    a->pitch_deg = pitch_deg;
    a->roll_deg  = roll_deg;
    attitude_redraw(obj);
}

/* ================= ③ 车身倾角 ================= */

typedef struct {
    dash_canvas_t cv;
    float tilt_deg;
} dash_tilt_t;

static void tilt_redraw(lv_obj_t *obj)
{
    dash_tilt_t *t = (dash_tilt_t *)obj->user_data;
    if (!t) return;
    const lv_coord_t W = t->cv.w;
    const lv_coord_t H = t->cv.h;
    float tilt = t->tilt_deg;

    dash_canvas_begin(&t->cv, DASH_COLOR_BG, LV_OPA_COVER);

    const float cx = W / 2.0f;
    const float cyp = H - 34.0f;            /* 表针枢轴 */
    const lv_coord_t R = (lv_coord_t)(H * 0.60f);

    uint16_t a_left  = 90 + 45;              /* 左端 */
    uint16_t a_right = 90 - 45;              /* 右端 */

    /* 轨道弧 */
    dash_draw_arc(&t->cv, cx, cyp, R - 24, a_left, a_right, 8, DASH_COLOR_PANEL, 220, true);

    /* 分度线 */
    for (int deg = -40; deg <= 40; deg += 10) {
        uint16_t ang = (uint16_t)(90 - deg);   /* 右倾(正) → 角<90 */
        float x0, y0, x1, y1;
        lvgl_angle_to_screen(cx, cyp, R - 36, ang, &x0, &y0);
        lvgl_angle_to_screen(cx, cyp, R - 46, ang, &x1, &y1);
        bool major = (deg % 20 == 0);
        dash_draw_line(&t->cv, x0, y0, x1, y1, major ? 3 : 1,
                       (deg == 0) ? DASH_COLOR_TEXT_DIM : DASH_COLOR_TICK, 200);
        if (major) {
            float tx, ty;
            lvgl_angle_to_screen(cx, cyp, R - 62, ang, &tx, &ty);
            char lbl[8];
            snprintf(lbl, sizeof(lbl), "%+d", deg);
            dash_draw_text(&t->cv, (lv_coord_t)tx - 12, (lv_coord_t)ty - 7,
                           &lv_font_montserrat_12, lbl, DASH_COLOR_TEXT_DIM, 200);
        }
    }

    /* 指针 */
    uint16_t needle_a = (uint16_t)(90.0f - tilt);
    lv_coord_t needle_len = R - 26;
    float nx, ny;
    lvgl_angle_to_screen(cx, cyp, needle_len, needle_a, &nx, &ny);
    lv_color_t needle_col = (fabsf(tilt) > 30.0f) ? DASH_COLOR_WARN : DASH_COLOR_PRIMARY;
    dash_draw_line(&t->cv, cx, cyp, nx, ny, 4, needle_col, 255);
    dash_draw_disc(&t->cv, cx, cyp, 8, needle_col, 255);

    /* 读数 */
    char val[16];
    snprintf(val, sizeof(val), "%+.1f°", tilt);
    dash_draw_text(&t->cv, 10, 8, &lv_font_montserrat_16, val,
                   fabsf(tilt) > 30.0f ? DASH_COLOR_WARN : DASH_COLOR_TEXT, 255);
    dash_draw_text(&t->cv, 10, 32, &lv_font_montserrat_12, "LEAN ANGLE",
                   DASH_COLOR_TEXT_DIM, 180);

    dash_canvas_end(&t->cv);
}

lv_obj_t *dash_tilt_create(lv_obj_t *parent, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, width, height);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, DASH_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    dash_tilt_t *t = (dash_tilt_t *)calloc(1, sizeof(dash_tilt_t));
    dash_canvas_alloc(&t->cv, obj, width, height);
    lv_obj_set_pos(t->cv.canvas, 0, 0);
    obj->user_data = t;
    tilt_redraw(obj);
    return obj;
}

void dash_tilt_set(lv_obj_t *obj, float tilt_deg)
{
    dash_tilt_t *t = (dash_tilt_t *)obj->user_data;
    if (!t) return;
    t->tilt_deg = tilt_deg;
    tilt_redraw(obj);
}

/* ================= ④ 加速度 ================= */

typedef struct {
    dash_canvas_t cv;
    float gx, gy, gz;
} dash_accel_t;

#define ACCEL_MAX  2.0f

static void accel_draw_axis(dash_canvas_t *cv, lv_coord_t y, const char *name,
                            float val, lv_color_t color)
{
    const lv_coord_t bar_x = 54;
    const lv_coord_t bar_w = 168;
    const lv_coord_t bar_h = 14;
    const lv_coord_t xc = bar_x + bar_w / 2;

    /* 轨道 */
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = DASH_COLOR_PANEL;
    rd.bg_opa = 255;
    rd.radius = bar_h / 2;
    lv_area_t track;
    track.x1 = bar_x; track.y1 = y; track.x2 = bar_x + bar_w; track.y2 = y + bar_h;
    lv_draw_rect(&cv->layer, &rd, &track);

    /* 中心 0 刻度 */
    dash_draw_line(cv, xc, y - 3, xc, y + bar_h + 3, 2, DASH_COLOR_TEXT_DIM, 190);

    /* 填充 */
    float ratio = val / ACCEL_MAX;
    if (ratio >  1.0f) ratio =  1.0f;
    if (ratio < -1.0f) ratio = -1.0f;
    lv_coord_t fill_w = (lv_coord_t)((bar_w / 2) * fabsf(ratio));
    if (fill_w > 0) {
        lv_draw_rect_dsc_t fd;
        lv_draw_rect_dsc_init(&fd);
        fd.bg_color = color;
        fd.bg_opa = 230;
        fd.radius = bar_h / 2;
        lv_area_t f;
        if (ratio >= 0) { f.x1 = xc; f.x2 = xc + fill_w; }
        else            { f.x1 = xc - fill_w; f.x2 = xc; }
        f.y1 = y; f.y2 = y + bar_h;
        lv_draw_rect(&cv->layer, &fd, &f);
    }

    /* 轴名（左端） */
    dash_draw_text(cv, 8, y, &lv_font_montserrat_12, name, DASH_COLOR_TEXT_DIM, 220);

    /* 刻度 ±1 */
    dash_draw_line(cv, bar_x + bar_w / 4, y + bar_h / 2, bar_x + bar_w / 4, y + bar_h, 1, DASH_COLOR_TICK, 150);
    dash_draw_line(cv, bar_x + bar_w * 3 / 4, y + bar_h / 2, bar_x + bar_w * 3 / 4, y + bar_h, 1, DASH_COLOR_TICK, 150);

    /* 数值（右端） */
    char valtxt[12];
    snprintf(valtxt, sizeof(valtxt), "%+.2f", val);
    dash_draw_text(cv, 228, y - 1, &lv_font_montserrat_12, valtxt,
                   fabsf(val) > 1.2f ? DASH_COLOR_WARN : DASH_COLOR_TEXT, 240);
}

static void accel_redraw(lv_obj_t *obj)
{
    dash_accel_t *a = (dash_accel_t *)obj->user_data;
    if (!a) return;
    const lv_coord_t H = a->cv.h;

    dash_canvas_begin(&a->cv, DASH_COLOR_BG, LV_OPA_COVER);

    dash_draw_text(&a->cv, 8, 6, &lv_font_montserrat_12, "ACCELERATION (G)",
                   DASH_COLOR_TEXT_DIM, 200);

    lv_coord_t y1 = 44;
    lv_coord_t y2 = y1 + 58;
    lv_coord_t y3 = y2 + 58;

    dash_draw_text(&a->cv, 8, y1 - 20, &lv_font_montserrat_12, "LONGITUDINAL 前/后", DASH_COLOR_TEXT_DIM, 150);
    accel_draw_axis(&a->cv, y1, "Gx", a->gx, DASH_COLOR_PRIMARY);

    dash_draw_text(&a->cv, 8, y2 - 20, &lv_font_montserrat_12, "LATERAL 左/右", DASH_COLOR_TEXT_DIM, 150);
    accel_draw_axis(&a->cv, y2, "Gy", a->gy, DASH_COLOR_SECONDARY);

    dash_draw_text(&a->cv, 8, y3 - 20, &lv_font_montserrat_12, "VERTICAL 上/下", DASH_COLOR_TEXT_DIM, 150);
    accel_draw_axis(&a->cv, y3, "Gz", a->gz, DASH_COLOR_WARN);

    /* 合成加速度 */
    float gmag = sqrtf(a->gx * a->gx + a->gy * a->gy + a->gz * a->gz);
    char mag[28];
    snprintf(mag, sizeof(mag), "|a| = %.2f G", gmag);
    dash_draw_text(&a->cv, 8, H - 34, &lv_font_montserrat_12, mag,
                   gmag > 1.2f ? DASH_COLOR_WARN : DASH_COLOR_TEXT, 230);

    dash_canvas_end(&a->cv);
}

lv_obj_t *dash_accel_create(lv_obj_t *parent, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, width, height);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, DASH_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    dash_accel_t *a = (dash_accel_t *)calloc(1, sizeof(dash_accel_t));
    dash_canvas_alloc(&a->cv, obj, width, height);
    lv_obj_set_pos(a->cv.canvas, 0, 0);
    obj->user_data = a;
    accel_redraw(obj);
    return obj;
}

void dash_accel_set(lv_obj_t *obj, float gx, float gy, float gz)
{
    dash_accel_t *a = (dash_accel_t *)obj->user_data;
    if (!a) return;
    a->gx = gx;
    a->gy = gy;
    a->gz = gz;
    accel_redraw(obj);
}