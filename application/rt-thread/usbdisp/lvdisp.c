/* SPDX-License-Identifier: Apache-2.0 */
/* LVGL orientation / display-boundary test screen.
 *
 * Replaces the JPEG pattern viewer that used to run on the VIDEO layer.
 * This module drives LVGL on the UI layer. It reads the panel resolution and
 * builds an explicit pattern that exposes:
 *   - which physical edge is "up" (coloured edge strips + TOP/DOWN text)
 *   - cropping / margins at the four corners (corner blocks + labels)
 *   - the reported resolution (centre text)
 *   - pixel alignment (a centred grid + ruler ticks)
 *
 * Only widgets present in the trimmed per-app LVGL build are used: lv_obj
 * (plain colour rectangles), lv_label and lv_arc. There is no canvas/line.
 */
#include <stdio.h>
#include <string.h>
#include "rtthread.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lvdisp.h"

/* lv_port_indev.c exposes no public header; declare its entry directly. */
extern void lv_port_indev_init(void);

#define DBG_TAG "udisp-lvgl"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* Palette: top=red, bottom=blue, left=green, right=yellow over a dark base. */
#define BG_COLOR    lv_color_hex(0x26292E)
#define TOP_COLOR   lv_color_hex(0xFF0000)
#define BOT_COLOR   lv_color_hex(0x0050FF)
#define LEFT_COLOR  lv_color_hex(0x00C000)
#define RIGHT_COLOR lv_color_hex(0xFFC000)
#define FRAME_COLOR lv_color_hex(0xFFD700)
#define INK         lv_color_hex(0xFFFFFF)
#define GRID_INK    lv_color_hex(0x4a90d9)

/* A plain solid-colour rectangle with default widget chrome stripped. */
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

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

/* A thin colour band along one physical edge; thick enough to reveal any
 * boundary inset / letterboxing. */
static void draw_edge_frame(lv_obj_t *parent, int w, int h, int thick)
{
    make_rect(parent, 0, 0, w, thick, FRAME_COLOR);              /* top    */
    make_rect(parent, 0, h - thick, w, thick, FRAME_COLOR);      /* bottom */
    make_rect(parent, 0, 0, thick, h, FRAME_COLOR);              /* left   */
    make_rect(parent, w - thick, 0, thick, h, FRAME_COLOR);      /* right  */
}

/* Full-width colour strips giving an unmistakable top/bottom cue. */
static void draw_direction_strips(lv_obj_t *parent, int w, int h, int band)
{
    make_rect(parent, 0, 0, w, band, TOP_COLOR);                 /* red top   */
    make_rect(parent, 0, h - band, w, band, BOT_COLOR);          /* blue bot  */
    /* a short left / right tick inside the frame marks the other axes */
    make_rect(parent, 0, h / 2 - 20, 8, 40, LEFT_COLOR);
    make_rect(parent, w - 8, h / 2 - 20, 8, 40, RIGHT_COLOR);
}

/* Four corner blocks + ASCII labels; the colour order plus the label texts
 * identify the orientation even if only part of the panel is visible. */
static void draw_corners(lv_obj_t *parent, int w, int h, int size)
{
    struct { int x, y; lv_color_t c; const char *tag; } corners[4] = {
        { 0,                   0,                  lv_color_hex(0xFF0000), "TL" },
        { w - size,            0,                  lv_color_hex(0x00A000), "TR" },
        { 0,                   h - size,           lv_color_hex(0x0000FF), "BL" },
        { w - size,            h - size,           lv_color_hex(0xFFC000), "BR" },
    };
    for (int i = 0; i < 4; i++) {
        make_rect(parent, corners[i].x, corners[i].y, size, size,
                  corners[i].c);
        lv_obj_t *l = make_label(parent, corners[i].tag,
                                 &lv_font_montserrat_16, lv_color_hex(0x000000));
        lv_obj_align_to(l, parent, LV_ALIGN_TOP_LEFT,
                        corners[i].x, corners[i].y + size / 2 - 8);
    }
}

/* A centred ruler grid + cross exposed via plain thin rectangles, plus a set
 * of tick marks along its top and left edge to test pixel alignment. */
static void draw_grid(lv_obj_t *parent, int w, int h, int size)
{
    int gx = (w - size) / 2;
    int gy = (h - size) / 2;
    int cell = size / 8;

    /* outer ruler border */
    make_rect(parent, gx, gy, 1, size, GRID_INK);
    make_rect(parent, gx + size - 1, gy, 1, size, GRID_INK);
    make_rect(parent, gx, gy, size, 1, GRID_INK);
    make_rect(parent, gx, gy + size - 1, size, 1, GRID_INK);

    /* interior grid lines */
    for (int i = 1; i < 8; i++) {
        int p = i * cell;
        make_rect(parent, gx, gy + p, size, 1, GRID_INK);        /* horizontal */
        make_rect(parent, gx + p, gy, 1, size, GRID_INK);        /* vertical   */
    }
    /* darker centre cross */
    make_rect(parent, gx, gy + size / 2 - 2, size, 4, INK);
    make_rect(parent, gx + size / 2 - 2, gy, 4, size, INK);

    /* ruler ticks on the two inner edges (opposite the labels) */
    for (int t = 1; t < 8; t++) {
        int tp = t * cell;
        make_rect(parent, gx, gy + tp, 6, 1, INK);               /* top edge  */
        make_rect(parent, gx + tp, gy, 1, 6, INK);               /* left edge */
    }
}

static void build_test_screen(void)
{
    lv_display_t *disp = lv_display_get_default();
    if (!disp) {
        LOG_E("no default display");
        return;
    }
    int w = lv_display_get_horizontal_resolution(disp);
    int h = lv_display_get_vertical_resolution(disp);
    LOG_I("LVGL test screen %dx%d", w, h);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    draw_edge_frame(scr, w, h, 3);

    int band = h / 20;
    if (band < 12) band = 12;
    draw_direction_strips(scr, w, h, band);

    char res[64];
    snprintf(res, sizeof(res), "%dx%d", w, h);

    /* resolution shown just below the top band */
    lv_obj_t *res_l = make_label(scr, res, &lv_font_montserrat_16,
                                 lv_color_hex(0xFFFF00));
    lv_obj_align(res_l, LV_ALIGN_TOP_MID, 0, band + 10);

    /* explicit TOP / DOWN text */
    lv_obj_t *top = make_label(scr, "TOP", &lv_font_montserrat_16, INK);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, band + 34);
    lv_obj_t *down = make_label(scr, "DOWN", &lv_font_montserrat_16, INK);
    lv_obj_align(down, LV_ALIGN_BOTTOM_MID, 0, -(band + 10));

    /* corner blocks, just inside the edge frame */
    int csize = w > 120 ? w * 3 / 10 : 48;
    draw_corners(scr, w, h, csize);

    /* centred grid sized to fit between the corner blocks on the long axis */
    int grid = h - 2 * csize - band - 200;
    if (grid > w * 3 / 4)
        grid = w * 3 / 4;
    if (grid < 80)
        grid = 80;
    draw_grid(scr, w, h, grid);
}

int udisp_lvgl_init(void)
{
    lv_init();
    lv_tick_set_cb(&rt_tick_get_millisecond);
    lv_port_disp_init();
    lv_port_indev_init();
    build_test_screen();
    return 0;
}

void udisp_lvgl_run(void)
{
    while (1) {
        lv_task_handler();
        rt_thread_mdelay(5);
    }
}