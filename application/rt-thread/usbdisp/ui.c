/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * USB display UI overlay. Default idle interface is a single full-screen
 * image presented by the hardware VIDEO layer (tools/make_static_video.py);
 * the LVGL UI layer stays blank/transparent so no original widgets, panels or
 * settings are drawn. History: idle/settings widgets were adapted from
 * main/app_lcd_ui.c upstream 17b774453ef106ab58d5ed9b885e759e0d67879f.
 */
#include <string.h>
#include <stdio.h>
#include "usbdisp.h"
static struct {
    lv_obj_t *countdown_arc;
    bool settings_open, page_hidden, hid_touch_enabled, stream_detected;
    char last_status[64];
} s_ui_ctx;
static bool s_countdown_anim_started;
static struct aicfb_layer_data ui_layer;
static uint8_t *ui_buffer;
static uint8_t *ui_buffers[2];
static lv_display_t *ui_display;
static bool lv_started;
static uint16_t ui_w, ui_h;      /* runtime UI canvas size (pixels) */
static uint32_t ui_buf_bytes;    /* = ui_w * ui_h * 4 */
static volatile bool frame_ready;

static void ui_pick_canvas(void)
{
    /* Long vertical panels (e.g. 320x1280) must use a canvas height that lets
     * both ARGB buffers fit inside the framebuffer allocation, otherwise
     * udisp_ui_init() bails out and no UI is shown.  Use the full panel width
     * with a 480 high window (320*480*4*2 == the 320*1280*3 framebuffer).
     * Wide panels keep the original 400x480 canvas. */
    if (udisp_screen.width < udisp_screen.height) {
        ui_w = udisp_screen.width;
        ui_h = 480;
    } else {
        ui_w = UDISP_UI_WIDTH;
        ui_h = UDISP_UI_HEIGHT;
    }
    ui_buf_bytes = (uint32_t)ui_w * ui_h * 4U;
}
static uint32_t countdown_start;
static bool counting, was_stream;
static bool ui_active;
static void app_lcd_ui_update_waiting_status_locked(void);
static void app_lcd_ui_open_settings_locked(void)
{
    /* Image-only mode has no settings page; entering/leaving the settings
     * screen is a no-op so the full-screen image stays visible. */
    s_ui_ctx.settings_open = true;
}

static void app_lcd_ui_close_settings_locked(void)
{
    s_ui_ctx.settings_open = false;
}

static void app_lcd_ui_build_screen_locked(void)
{
    lv_obj_t *screen = lv_screen_active();

    lv_obj_clean(screen);
    /* The default interface is a single full-screen image presented by the
     * hardware VIDEO layer (see the static idle clip in
     * application/rt-thread/usbdisp/assets/video/).  The LVGL UI overlay is
     * intentionally left blank/transparent so nothing is drawn over the image;
     * all widgets, panels and settings that made up the previous UI are gone. */
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0x203040), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    s_ui_ctx.last_status[0] = '\0';
    s_countdown_anim_started = false;
    /* No widgets are created: the UI layer stays fully transparent so the
     * full-screen image on the VIDEO layer is the only thing on screen. */
}

static void app_lcd_ui_update_waiting_status_locked(void)
{
    /* No waiting status / spinner widgets exist in image-only mode. */
    (void)0;
}

static void app_lcd_ui_update_countdown_locked(int seconds_left)
{
    /* No countdown widgets exist in image-only mode. */
    (void)seconds_left;
}


static void ui_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    if (lv_display_flush_is_last(disp)) {
        aicos_dcache_clean_range(pixels, ui_buf_bytes);
        ui_layer.buf.phy_addr[0] = (uintptr_t)pixels;
        aicos_mutex_take(udisp_display_lock, AICOS_WAIT_FOREVER);
        mpp_fb_ioctl(udisp_fb, AICFB_UPDATE_LAYER_CONFIG, &ui_layer);
        mpp_fb_ioctl(udisp_fb, AICFB_WAIT_FOR_VSYNC, NULL);
        aicos_mutex_give(udisp_display_lock);
    }
    lv_display_flush_ready(disp);
}
void udisp_ui_visibility(bool enable)
{
    if (!ui_active) return;
    ui_layer.enable = enable;
    aicos_mutex_take(udisp_display_lock, AICOS_WAIT_FOREVER);
    mpp_fb_ioctl(udisp_fb, AICFB_UPDATE_LAYER_CONFIG, &ui_layer);
    aicos_mutex_give(udisp_display_lock);
}
void udisp_ui_frame_ready(void) { frame_ready = true; }
void udisp_ui_init(void)
{
    /* ARGB8888 already carries the LVGL opacity channel. Color-keying zero
     * would turn transparent pixels into a second, conflicting blend rule. */
    struct aicfb_ck_config ck = {AICFB_LAYER_TYPE_UI, 0, 0};
    struct aicfb_alpha_config alpha = {AICFB_LAYER_TYPE_UI, 1, AICFB_PIXEL_ALPHA_MODE, 255};
    lv_display_t *disp;
    if (ui_active) return;
    if (!lv_started) {
        lv_init();
        lv_tick_set_cb(rt_tick_get_millisecond);
        lv_started = true;
    }
    ui_pick_canvas();
    /* All rectangles must be disabled before DE changes the layer format.
     * The framebuffer format is selected by AICFB_RGB888 in usbdisp builds. */
    aicos_mutex_take(udisp_display_lock, AICOS_WAIT_FOREVER);
    for (unsigned rect = 0; rect < 4; rect++) {
        struct aicfb_layer_data off = {0};
        off.layer_id = AICFB_LAYER_TYPE_UI;
        off.rect_id = rect;
        mpp_fb_ioctl(udisp_fb, AICFB_UPDATE_LAYER_CONFIG, &off);
    }
    aicos_mutex_give(udisp_display_lock);
    if (udisp_screen.smem_len < ui_buf_bytes * 2) {
        printf("usbdisp: framebuffer too small for UI windows\n");
        return;
    }
    /* Reuse the driver's full-screen allocation for two unscaled windows.
     * Full rendering swaps only at VSYNC and never draws into scanout. */
    ui_buffer = udisp_screen.framebuffer;
    ui_buffers[0] = ui_buffer;
    ui_buffers[1] = ui_buffer + ui_buf_bytes;
    disp = ui_display;
    if (!disp)
        disp = ui_display = lv_display_create(ui_w, ui_h);
    /* Keep LVGL's per-pixel opacity in the ARGB8888 UI layer. The base
     * framebuffer is RGB888; the UI layer has its own four-byte pixels. */
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_flush_cb(disp, ui_flush);
    memset(ui_buffer, 0, ui_buf_bytes * 2);
    aicos_dcache_clean_range(ui_buffer, ui_buf_bytes * 2);
    lv_display_set_buffers(disp, ui_buffers[0], ui_buffers[1],
                          ui_buf_bytes, LV_DISPLAY_RENDER_MODE_FULL);
    ui_layer.layer_id = AICFB_LAYER_TYPE_UI;
    ui_layer.enable = 1;
    ui_layer.pos.x = (udisp_screen.width - ui_w) / 2;
    ui_layer.pos.y = (udisp_screen.height - ui_h) / 2;
    ui_layer.buf.buf_type = MPP_PHY_ADDR;
    ui_layer.buf.phy_addr[0] = (uintptr_t)ui_buffer;
    ui_layer.buf.stride[0] = ui_w * 4;
    ui_layer.buf.size.width = ui_w;
    ui_layer.buf.size.height = ui_h;
    ui_layer.buf.format = MPP_FMT_ARGB_8888;
    ui_layer.enable = 0;
    mpp_fb_ioctl(udisp_fb, AICFB_UPDATE_LAYER_CONFIG, &ui_layer);
    mpp_fb_ioctl(udisp_fb, AICFB_UPDATE_CK_CONFIG, &ck);
    mpp_fb_ioctl(udisp_fb, AICFB_UPDATE_ALPHA_CONFIG, &alpha);
    s_ui_ctx.page_hidden = udisp_page_hidden;
    s_ui_ctx.hid_touch_enabled = udisp_hid_enabled;
    app_lcd_ui_build_screen_locked();
    ui_active = true;
    udisp_ui_visibility(true);
}
void udisp_ui_deinit(void)
{
    if (!ui_active) return;
    ui_layer.enable = 0;
    aicos_mutex_take(udisp_display_lock, AICOS_WAIT_FOREVER);
    mpp_fb_ioctl(udisp_fb, AICFB_UPDATE_LAYER_CONFIG, &ui_layer);
    aicos_msleep(UDISP_PRESENT_PERIOD_MS);
    aicos_mutex_give(udisp_display_lock);
    /* Keep the LVGL core/display alive across the USB transition. Clearing
     * the active screen releases all page widgets and animations without
     * destroying the RT-Thread-backed global LVGL state. */
    if (ui_display)
        lv_obj_clean(lv_display_get_screen_active(ui_display));
    memset(&s_ui_ctx, 0, sizeof(s_ui_ctx));
    memset(&ui_layer, 0, sizeof(ui_layer));
    ui_buffer = NULL;
    ui_buffers[0] = NULL;
    ui_buffers[1] = NULL;
    frame_ready = false;
    counting = false;
    was_stream = false;
    ui_active = false;
}
void udisp_ui_command(int command, bool value)
{
    /* Image-only UI has no interactive widgets: host commands to open the
     * settings page or toggle virtual switches are intentionally ignored. */
    (void)command; (void)value;
    if (command == 1) app_lcd_ui_open_settings_locked();
    else if (command == 2) app_lcd_ui_close_settings_locked();
}
void udisp_ui_poll(void)
{
    uint32_t now = aic_get_time_ms();
    if (udisp_stream_enabled && ui_active) {
        udisp_ui_deinit();
        was_stream = true;
        return;
    }
    if (!udisp_stream_enabled && !ui_active) {
        udisp_ui_init();
        was_stream = false;
    }
    if (!ui_active) return;
    udisp_settings_poll();
    if (!udisp_host_alive() && !udisp_stream_requested) {
        udisp_stream_requested = false;
        udisp_stream_enabled = false;
        udisp_stream_hint = false;
        counting = false;
    }
    if (was_stream && !udisp_stream_enabled) {
        frame_ready = false;
        s_ui_ctx.stream_detected = false;
        s_countdown_anim_started = false;
        if (s_ui_ctx.countdown_arc)
            lv_anim_delete(s_ui_ctx.countdown_arc, NULL);
        app_lcd_ui_update_waiting_status_locked();
        udisp_ui_visibility(true);
    }
    if (!udisp_stream_enabled) {
        if (udisp_stream_hint && udisp_host_alive()) {
            s_ui_ctx.stream_detected = true;
            if (!counting) { countdown_start = now; counting = true; }
            uint32_t elapsed = now - countdown_start;
            int left = elapsed < UDISP_COUNTDOWN_MS ?
                       (UDISP_COUNTDOWN_MS - elapsed + 999) / 1000 : 0;
            app_lcd_ui_update_countdown_locked(left);
            if (!left) udisp_stream_requested = true;
        } else {
            s_ui_ctx.stream_detected = false;
            s_countdown_anim_started = false;
            app_lcd_ui_update_waiting_status_locked();
        }
    } else if (frame_ready) {
        /* Hide the waiting overlay only when a native USB frame is visible. */
        udisp_ui_visibility(false);
    }
    was_stream = udisp_stream_enabled;
    lv_timer_handler();
}
