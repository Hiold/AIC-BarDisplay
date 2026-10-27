/* SPDX-License-Identifier: Apache-2.0 */
/* Instrument-cluster demo screen (LVGL, UI layer of the USB bar display).
 *
 * The old orientation / boundary test pattern is gone. This module renders
 * three animated instruments and adapts to the logical canvas orientation:
 *
 *   landscape (width >= height, e.g. rotated 320x1280 panel):
 *       left    - attitude indicator (AI): pitch ladder + sliding roll bubble
 *       middle  - rear obstacle-avoidance radar: range rings, sweep, blips
 *       right    - acceleration trajectory: rolling 2-channel G trace
 *   portrait (native long-strip, no rotation):
 *       bottom   - attitude indicator
 *       middle   - rear obstacle radar
 *       top-right- acceleration trajectory
 *
 * The panel has no IMU / radar / accelerometer, so all three instruments are
 * driven by an internally simulated attitude / obstacle / acceleration model
 * stepped by an LVGL timer. Only widgets present in the trimmed per-app LVGL
 * build are used: lv_obj (plain rectangles/circles), lv_label and lv_arc.
 * There is no canvas / line / chart / image in this build.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "rtthread.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lvdisp.h"

/* lv_port_indev.c exposes no public header; declare its entry directly. */
extern void lv_port_indev_init(void);

#define DBG_TAG "udisp-lvgl"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RAD(d) ((float)(d) * (float)M_PI / 180.0f)

/* ---------------- Palette (dark instrument-cluster theme) ---------------- */
#define C_BG          lv_color_hex(0x0B0E14)   /* outer background            */
#define C_PANEL       lv_color_hex(0x12161F)   /* widget fill                 */
#define C_SKY         lv_color_hex(0x1F3A5C)   /* attitude sky                */
#define C_GROUND      lv_color_hex(0x394A2C)   /* attitude ground             */
#define C_HORIZON     lv_color_hex(0xE8EDF5)   /* horizon bar / bright text   */
#define C_STEEL       lv_color_hex(0x8FA3BC)   /* dim ticks / secondary text  */
#define C_STEEL_FAINT lv_color_hex(0x3A465A)   /* very dim grid / ring        */
#define C_ACCENT      lv_color_hex(0x35E0C1)   /* primary cyan accent         */
#define C_RADAR       lv_color_hex(0x22E07A)   /* radar sweep / far blip      */
#define C_RADAR_DIM   lv_color_hex(0x173226)   /* idle ring                   */
#define C_BLIP_MID    lv_color_hex(0xFFC24D)   /* medium-range contact        */
#define C_BLIP_NEAR   lv_color_hex(0xFF5050)   /* near contact                */
#define C_TRACE_A     lv_color_hex(0x4DA6FF)   /* acceleration X trace        */
#define C_TRACE_G     lv_color_hex(0xFF5C8A)   /* acceleration Z trace        */

/* ---------------- Plain object helpers ------------------------------------ */

/* A solid rectangle with all default widget chrome stripped. */
static lv_obj_t *make_rect(lv_obj_t *parent, int x, int y, int w, int h,
                           lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

/* A ring (annulus) drawn as a circle outline via border on a transparent fill.
 * radius is measured to the ring centre line; thick is the stroke width. */
static lv_obj_t *make_ring(lv_obj_t *parent, int cx, int cy, int radius,
                           int thick, lv_color_t color, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    int size = radius * 2;
    lv_obj_set_pos(o, cx - radius, cy - radius);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(o, color, 0);
    lv_obj_set_style_border_opa(o, opa, 0);
    lv_obj_set_style_border_width(o, thick, 0);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

/* A text label, left-anchored at (x, y). */
static lv_obj_t *make_label(lv_obj_t *parent, int x, int y, const char *text,
                            const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

/* ---------------- Simulated model ----------------------------------------- */

#define TRACE_N  96                 /* acceleration history points            */
#define RING_N   3                  /* radar range rings                      */
#define BLIP_N   4                  /* obstacle contacts                      */

struct sim_blip {
    float theta;                    /* bearing, radians, 0 = up (rear)        */
    float range;                    /* normalised radius 0..1                 */
    float phase;                    /* blink phase                             */
    int   gold;                     /* 1 = medium range colour                */
};

struct sim_state {
    float t;                        /* sim clock, seconds                      */
    float pitch;                    /* degrees, + = nose up                    */
    float roll;                     /* degrees, + = bank right                 */
    float sweep;                    /* radar sweep angle, degrees (0=up)       */
    struct sim_blip blips[BLIP_N];
    float ax, az;                   /* simulated horizontal / vertical G      */

    int   trace_x[TRACE_N];         /* last accel sample column screen x      */
    int   trace_ay[TRACE_N];        /* sampled X accel pixel y                 */
    int   trace_az[TRACE_N];        /* sampled Z accel pixel y                 */
    int   trace_head;               /* index of newest sample                  */
    int   trace_count;              /* populated samples                       */
};

static struct sim_state sim;

/* ---------------- Widget object handles ----------------------------------- */

struct att_widget {
    int   x, y, w, h;
    lv_obj_t *sky, *ground, *horizon;
    lv_obj_t *tick[2][3];           /* +pitch / -pitch ladder ticks           */
    lv_obj_t *bubble;               /* roll bubble                             */
    lv_obj_t *lbl_pitch, *lbl_roll;
    char  pitch_txt[24], roll_txt[24];
};

struct radar_widget {
    int   cx, cy, r;
    lv_obj_t *ring[3];
    lv_obj_t *spoke_h, *spoke_v;
    lv_obj_t *sweep;
    lv_obj_t *blip[BLIP_N];
    lv_obj_t *lbl_range, *lbl_count;
    char  range_txt[24], count_txt[24];
};

struct accel_widget {
    int   x, y, w, h;
    int   pad, center_y, scale;
    lv_obj_t *dot_a[TRACE_N];
    lv_obj_t *dot_g[TRACE_N];
    lv_obj_t *zero_h, *zero_v;
    lv_obj_t *lbl_read, *lbl_ax, *lbl_gz;
    char  read_txt[24];
};

static struct {
    struct att_widget   att;
    struct radar_widget radar;
    struct accel_widget accel;
} ui;

/* True when the logical canvas is wide (rotation enabled). Drives whether the
 * instruments are arranged horizontally (left/middle/right) or vertically. */
static int g_landscape;

/* ---------------- Construction -------------------------------------------- */

static void build_attitude(int x, int y, int w, int h)
{
    struct att_widget *a = &ui.att;
    a->x = x; a->y = y; a->w = w; a->h = h;

    lv_obj_t *scr = lv_screen_active();

    /* sky / ground split by pitch: nose-up pushes the boundary down. */
    a->sky    = make_rect(scr, x, y, w, h, C_SKY);
    a->ground = make_rect(scr, x, y, w, h, C_GROUND);
    a->horizon = make_rect(scr, x, y, w, 4, C_HORIZON);

    /* pitch ladder ticks, two rows above and two below the horizon. */
    int ty = y + h / 2 - 60;
    for (int side = 0; side < 2; side++) {      /* 0 = climb(+), 1 = dive(-)  */
        int base = side ? ty + 40 : ty;
        for (int i = 0; i < 3; i++) {
            int lw = side ? 34 : 44;            /* wider on the climb side    */
            a->tick[side][i] = make_rect(scr, x + w / 2 - lw / 2,
                                         base + i * 20, lw, 3, C_STEEL);
        }
    }

    /* roll bank: a short horizontal slot with a sliding bubble. */
    int slot_w = w / 2;
    int slot_y = y + 22;
    make_rect(scr, x + w / 2 - slot_w / 2, slot_y + 14, slot_w, 1, C_STEEL_FAINT);
    make_rect(scr, x + w / 2 - 1, slot_y + 9, 2, 11, C_HORIZON);   /* center  */
    a->bubble = make_rect(scr, x + w / 2 - 3, slot_y, 6, 6, C_ACCENT);

    strcpy(a->pitch_txt, "PITCH +0.0 deg");
    strcpy(a->roll_txt, "ROLL +0.0 deg");
    a->lbl_pitch = make_label(scr, x + 10, y + h - 20, a->pitch_txt,
                              &lv_font_montserrat_12, C_STEEL);
    a->lbl_roll = make_label(scr, x + w - 10 - 110, y + h - 20, a->roll_txt,
                             &lv_font_montserrat_12, C_STEEL);
    lv_obj_set_width(a->lbl_roll, 110);
    lv_obj_set_style_text_align(a->lbl_roll, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(a->lbl_roll, C_ACCENT, 0);
    make_label(scr, x + w / 2 - 30, y + 2, "ATTITUDE", &lv_font_montserrat_12,
               C_STEEL);
}

static void update_attitude(void)
{
    struct att_widget *a = &ui.att;
    float rad = DEG2RAD(sim.roll);
    /* pitch amount in pixels for a +/-60 deg full travel across the height.  */
    float yoff = (float)a->h * sim.pitch / 120.0f;
    if (yoff > (float)a->h * 0.45f) yoff = (float)a->h * 0.45f;
    if (yoff < -(float)a->h * 0.45f) yoff = -(float)a->h * 0.45f;
    int hy = a->y + a->h / 2 + (int)(yoff + 0.5f);

    lv_obj_set_pos(a->horizon, a->x, hy);
    int ground_top = hy + 2;
    lv_obj_set_pos(a->ground, a->x, ground_top);
    lv_obj_set_size(a->ground, a->w, a->y + a->h - ground_top);

    /* pitch ladder ticks ride with the horizon: 0 = below (climb), above. */
    for (int side = 0; side < 2; side++) {
        int dir = side ? -1 : 1;                /* below / above the horizon  */
        int lw  = side ? 34 : 44;
        for (int i = 0; i < 3; i++)
            lv_obj_set_pos(a->tick[side][i], a->x + a->w / 2 - lw / 2,
                           hy + dir * (10 + i * 20));
    }

    /* slide the roll bubble sideways by sin(roll). */
    int bx = a->x + a->w / 2 + (int)(sinf(rad) * (float)a->w / 4.0f);
    bx -= 3;
    if (bx < a->x) bx = a->x;
    if (bx > a->x + a->w - 6) bx = a->x + a->w - 6;
    lv_obj_set_x(a->bubble, bx);

    snprintf(a->pitch_txt, sizeof(a->pitch_txt), "PITCH %+.0f deg", sim.pitch);
    snprintf(a->roll_txt, sizeof(a->roll_txt), "ROLL %+.0f deg", sim.roll);
    lv_label_set_text(a->lbl_pitch, a->pitch_txt);
    lv_label_set_text(a->lbl_roll, a->roll_txt);
}

static void build_radar(int cx, int cy, int r)
{
    struct radar_widget *rd = &ui.radar;
    rd->cx = cx; rd->cy = cy; rd->r = r;
    lv_obj_t *scr = lv_screen_active();

    /* concentric safe-distance rings: outer dim, inner accents. */
    rd->ring[0] = make_ring(scr, cx, cy, r,     2, C_STEEL_FAINT, LV_OPA_COVER);
    rd->ring[1] = make_ring(scr, cx, cy, r * 2 / 3, 1, C_STEEL_FAINT, LV_OPA_70);
    rd->ring[2] = make_ring(scr, cx, cy, r / 3,    1, C_STEEL_FAINT, LV_OPA_70);

    /* cross-hair spokes read as a planar radar grid. */
    rd->spoke_h = make_rect(scr, cx - r, cy - 1, r * 2, 1, C_STEEL_FAINT);
    rd->spoke_v = make_rect(scr, cx - 1, cy - r, 1, r * 2, C_STEEL_FAINT);

    /* rotating sweep wedge. */
    rd->sweep = lv_arc_create(scr);
    lv_obj_remove_style_all(rd->sweep);
    lv_obj_set_pos(rd->sweep, cx - r, cy - r);
    lv_obj_set_size(rd->sweep, r * 2, r * 2);
    lv_obj_set_style_bg_opa(rd->sweep, LV_OPA_TRANSP, 0);
    lv_obj_set_style_arc_color(rd->sweep, C_RADAR, 0);
    lv_obj_set_style_arc_opa(rd->sweep, LV_OPA_60, 0);
    lv_obj_set_style_arc_width(rd->sweep, 3, 0);
    lv_arc_set_bg_angles(rd->sweep, 0, 0);              /* no static guide ring */

    for (int i = 0; i < BLIP_N; i++)
        rd->blip[i] = make_rect(scr, cx - 3, cy - 3, 6, 6, C_RADAR);

    strcpy(rd->range_txt, "REAR  --.- m");
    strcpy(rd->count_txt, "CONTACTS 0");
    rd->lbl_range = make_label(scr, 0, 0, rd->range_txt,
                               &lv_font_montserrat_16, C_RADAR);
    rd->lbl_count = make_label(scr, 0, 0, rd->count_txt,
                               &lv_font_montserrat_12, C_STEEL);
    if (g_landscape) {
        /* to the right of the ring, stacked top to bottom, centred on ring y. */
        lv_obj_set_style_text_align(rd->lbl_range, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_align(rd->lbl_count, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(rd->lbl_range, cx + r + 10, cy - 26);
        lv_obj_set_pos(rd->lbl_count, cx + r + 10, cy - 2);
    } else {
        /* centred under the ring; the radar is centred on the screen. */
        lv_obj_align_to(rd->lbl_range, scr, LV_ALIGN_CENTER, 0, cy + r + 4);
        lv_obj_align_to(rd->lbl_count, scr, LV_ALIGN_CENTER, 0, cy + r + 26);
    }
}

static void update_radar(void)
{
    struct radar_widget *rd = &ui.radar;

    /* sweep: 0 deg = up (rear). Step clockwise through a full circle. */
    int ang = ((int)sim.sweep % 360 + 360) % 360;
    lv_arc_set_angles(rd->sweep, ang, ang + 2);

    /* reposition blips; blink by pulsing opacity. */
    int contacts = 0;
    float nearest = 999.0f;
    for (int i = 0; i < BLIP_N; i++) {
        struct sim_blip *b = &sim.blips[i];
        float rpix = (float)(rd->r - 6) * b->range;
        int  bx = rd->cx + (int)(rpix * sinf(b->theta) + 0.5f);
        int  by = rd->cy - (int)(rpix * cosf(b->theta) + 0.5f);
        lv_obj_set_pos(rd->blip[i], bx - 3, by - 3);
        lv_obj_set_style_bg_color(rd->blip[i],
                                  b->gold ? C_BLIP_MID : C_BLIP_NEAR, 0);
        float blink = 0.5f + 0.5f * sinf(b->phase);
        lv_obj_set_style_bg_opa(rd->blip[i],
                                (lv_opa_t)(60 + (lv_opa_t)(40 * blink)), 0);

        if (b->range < 0.95f) {         /* inside the outermost ring          */
            contacts++;
            if (b->range < nearest) nearest = b->range;
        }
    }
    if (contacts == 0) {
        strcpy(rd->range_txt, "REAR  CLEAR");
    } else {
        float metres = 1.0f / (nearest < 0.05f ? 0.05f : nearest);
        snprintf(rd->range_txt, sizeof(rd->range_txt), "REAR  %5.1f m", metres);
    }
    snprintf(rd->count_txt, sizeof(rd->count_txt), "CONTACTS %d", contacts);
    lv_label_set_text(rd->lbl_range, rd->range_txt);
    lv_label_set_text(rd->lbl_count, rd->count_txt);
}

static void build_accel(void)
{
    struct accel_widget *ac = &ui.accel;
    lv_obj_t *scr = lv_screen_active();

    ac->pad = 4;
    ac->center_y = ac->y + ac->h / 2;
    ac->scale = ac->h / 2 - ac->pad - 4;             /* +/-1 g at the edge     */

    /* plot frame + faint central axes. */
    lv_obj_t *frame = lv_obj_create(scr);
    lv_obj_remove_style_all(frame);
    lv_obj_set_pos(frame, ac->x, ac->y);
    lv_obj_set_size(frame, ac->w, ac->h);
    lv_obj_set_style_bg_color(frame, C_PANEL, 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(frame, C_STEEL_FAINT, 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_radius(frame, 0, 0);
    lv_obj_set_style_pad_all(frame, 0, 0);

    ac->zero_h = make_rect(scr, ac->x + ac->pad, ac->center_y,
                           ac->w - 2 * ac->pad, 1, C_STEEL_FAINT);
    ac->zero_v = make_rect(scr, ac->x + ac->w / 2, ac->y + ac->pad,
                           1, ac->h - 2 * ac->pad, C_STEEL_FAINT);

    /* one dot per history sample per channel; repositioned each tick. */
    for (int i = 0; i < TRACE_N; i++) {
        ac->dot_a[i] = make_rect(scr, ac->x, ac->center_y, 3, 3, C_TRACE_A);
        ac->dot_g[i] = make_rect(scr, ac->x, ac->center_y, 3, 3, C_TRACE_G);
        lv_obj_set_style_bg_opa(ac->dot_a[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(ac->dot_g[i], LV_OPA_TRANSP, 0);
    }

    strcpy(ac->read_txt, "AX 0.00  AZ 0.00");
    ac->lbl_read = make_label(scr, ac->x + 6, ac->y + 4, ac->read_txt,
                              &lv_font_montserrat_12, C_HORIZON);
    ac->lbl_ax = make_label(scr, ac->x + 6, ac->y + ac->h - 18, "AX",
                            &lv_font_montserrat_12, C_TRACE_A);
    ac->lbl_gz = make_label(scr, ac->x + ac->w - 26, ac->y + ac->h - 18, "AZ",
                            &lv_font_montserrat_12, C_TRACE_G);
}

/* Re-render the whole trace from the ring buffer. */
static void redraw_accel(void)
{
    struct accel_widget *ac = &ui.accel;
    int n = sim.trace_count;
    for (int k = 0; k < TRACE_N; k++) {
        int idx = (sim.trace_head - k + TRACE_N) % TRACE_N;
        int visible = k < n;
        if (!visible) {
            lv_obj_set_style_bg_opa(ac->dot_a[idx], LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_opa(ac->dot_g[idx], LV_OPA_TRANSP, 0);
            continue;
        }
        int sx = sim.trace_x[idx];
        lv_obj_set_pos(ac->dot_a[idx], sx, sim.trace_ay[idx] - 1);
        lv_obj_set_pos(ac->dot_g[idx], sx, sim.trace_az[idx] - 1);
        lv_obj_set_style_bg_opa(ac->dot_a[idx], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(ac->dot_g[idx], LV_OPA_COVER, 0);
    }
}

static void update_accel(void)
{
    struct accel_widget *ac = &ui.accel;
    /* newest sample goes to the right edge; older ones step one column left. */
    for (int k = 0; k < sim.trace_count; k++)
        sim.trace_x[(sim.trace_head - k + TRACE_N) % TRACE_N] -= 3;

    struct sim_state *s = &sim;
    int idx = s->trace_head;
    s->trace_x[idx] = ac->x + ac->w - ac->pad - 2;
    s->trace_ay[idx] = ac->center_y - (int)(s->ax * (float)ac->scale);
    s->trace_az[idx] = ac->center_y - (int)(s->az * (float)ac->scale);
    if (s->trace_count < TRACE_N) s->trace_count++;
    s->trace_head = (s->trace_head + 1) % TRACE_N;

    snprintf(ac->read_txt, sizeof(ac->read_txt), "AX %+.2f  AZ %+.2f",
             s->ax, s->az);
    lv_label_set_text(ac->lbl_read, ac->read_txt);
    redraw_accel();
}

/* ---------------- Simulation step (called by the LVGL timer) -------------- */

static void sim_step(void)
{
    /* attitude: gentle pendulum wander. */
    sim.t += 0.04f;
    sim.pitch = 20.0f * sinf(sim.t * 0.55f);
    sim.roll  = 26.0f * sinf(sim.t * 0.37f + 1.0f);

    /* radar: sweep a full circle about every 1.8 s; blips drift on range. */
    sim.sweep += 6.0f;
    for (int i = 0; i < BLIP_N; i++) {
        struct sim_blip *b = &sim.blips[i];
        b->phase += 0.25f;
        b->range += 0.004f * sinf(sim.t * 0.4f + i);
        if (b->range > 1.0f) b->range = 0.6f;
        if (b->range < 0.30f) b->range = 0.32f;
    }

    /* acceleration: correlated low-frequency wobble (+ steady -1 g in Z). */
    sim.ax = 0.6f * sinf(sim.t * 1.1f) + 0.25f * sinf(sim.t * 3.3f);
    sim.az = -1.0f + 0.3f * sinf(sim.t * 1.7f + 0.6f);
}

static void anim_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    sim_step();
    update_attitude();
    update_radar();
    update_accel();
}

static void build_ui(int w, int h)
{
    /* Reserve the attitude / radar labels room by reducing the usable band. */
    int label_row = 56;
    int m = w / 60 + 4;                         /* outer margin                */

    g_landscape = w >= h;

    if (g_landscape) {
        /* Wide canvas: left = attitude, middle = radar, right = accel. */
        int gap = w / 80 + 4;
        if (gap < 8) gap = 8;
        m = h / 24 + 4;
        if (m < 6) m = 6;

        /* right: acceleration trajectory, a square filling the height. */
        int s = h - 2 * m;
        ui.accel.x = w - m - s; ui.accel.y = m;
        ui.accel.w = s; ui.accel.h = s;
        build_accel();

        /* left: attitude indicator block. */
        int aw = w * 30 / 100;
        if (aw < 220) aw = 220;
        if (aw > w - s - 2 * gap) aw = w - s - 2 * gap;
        build_attitude(m, m, aw, h - 2 * m);

        /* middle: radar, centred in the free horizontal band. */
        int x_lo = m + aw + gap;
        int x_hi = ui.accel.x - gap;
        int cx = (x_lo + x_hi) / 2;
        int r = (h - 2 * m) / 2;
        if (r > (x_hi - x_lo) / 2) r = (x_hi - x_lo) / 2;
        if (r < 30) r = 30;
        build_radar(cx, h / 2, r);
        return;
    }

    /* Portrait: bottom = attitude, middle = radar, top-right = accel. */
    int att_h = h * 30 / 100;
    if (att_h < 220) att_h = 220;
    build_attitude(m, h - att_h, w - 2 * m, att_h - m);

    /* top-right: acceleration trajectory, a compact square. */
    int ap = w * 55 / 100;
    if (ap > 340) ap = 340;
    ui.accel.x = w - m - ap; ui.accel.y = m;
    ui.accel.w = ap; ui.accel.h = ap;
    build_accel();

    /* middle: rear obstacle radar, centred in the band left over; reserve a
     * few rows below the ring for the range / contact readouts. */
    int band_top = m + ap + 6;
    int band_bot = h - att_h - m;
    int band_h = band_bot - band_top;
    if (band_h < label_row + 30) band_h = label_row + 30;
    int r = (w - 2 * m) / 2;
    if (r > (band_h - label_row) / 2) r = (band_h - label_row) / 2;
    if (r > w * 42 / 100) r = w * 42 / 100;
    if (r < 30) r = 30;
    int cy = band_top + band_h / 2;         /* ring centre, labels live below  */
    build_radar(w / 2, cy, r);
}

int udisp_lvgl_init(void)
{
    lv_init();
    lv_tick_set_cb(&rt_tick_get_millisecond);
    lv_port_disp_init();
    lv_port_indev_init();

    lv_display_t *disp = lv_display_get_default();
    if (!disp) {
        LOG_E("no default display");
        return -1;
    }
    int w = lv_display_get_horizontal_resolution(disp);
    int h = lv_display_get_vertical_resolution(disp);
    LOG_I("instrument cluster UI %dx%d", w, h);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* seed obstacle bearings around the upper hemisphere (rear). */
    struct sim_blip seed[BLIP_N] = {
        { -0.9f, 0.55f, 0.0f, 1 },
        { -0.4f, 0.85f, 1.2f, 0 },
        {  0.35f, 0.70f, 2.1f, 0 },
        {  0.85f, 0.95f, 0.7f, 1 },
    };
    memcpy(sim.blips, seed, sizeof(seed));
    sim.trace_head = 0;
    sim.trace_count = 0;

    build_ui(w, h);
    lv_timer_create(anim_timer_cb, 40, NULL);
    return 0;
}

void udisp_lvgl_run(void)
{
    while (1) {
        lv_task_handler();
        rt_thread_mdelay(5);
    }
}