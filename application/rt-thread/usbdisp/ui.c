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
#ifdef AIC_USB_DISP_EBIKE_UI
#include "mpp_ge.h"
#include "ui/ebike/lv_demo_ebike.h"
#include "ui/ebike/ebike_assets.h"
#endif
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
#ifdef AIC_USB_DISP_EBIKE_UI
/* LVGL 9.1's lv_display_set_rotation() only swaps the *logical* resolution;
 * rotating (and here also scaling) the rendered pixels is the flush
 * callback's job.  Every completed landscape canvas is rotated 90 deg and
 * uniformly upscaled into ui_scanout — the portrait (ui_scan_w x ui_scan_h)
 * buffer the DE UI layer actually scans out.  GE2D performs the blit (its
 * cmdq ops auto-derive the scaler phases when dst != src, and the rot0
 * path swaps the dst dimensions for 90/270 deg, so rotation + scaling run
 * in one pass); ui_ge == NULL or a failed bitblt falls back to the fused
 * nearest-neighbour ui_scale_rotate(). */
static struct mpp_ge *ui_ge;
static uint8_t *ui_scanout;
/* The landscape ebike Home page is authored for 480x320 (the dial
 * img_ebike_scale is 180x320; lv_demo_ebike.h documents 480x320 as the max
 * landscape size; the page ships no wallpaper image — the video layer shows
 * through).  The LVGL
 * display is created at exactly that design box so pct(100) layouts render
 * with zero stretch; the flush path then scales the whole page
 * ui_scale_num/ui_scale_den = screen.width/EBIKE_DESIGN_HEIGHT (1.375 on the
 * 440-wide BOE1126) while rotating, so the UI fills the panel short axis
 * edge-to-edge and the long-axis window adapts (480*440/320 = 660 px). */
#define EBIKE_DESIGN_WIDTH  480
#define EBIKE_DESIGN_HEIGHT 320
static uint32_t ui_scale_num;    /* canvas -> scanout uniform ratio */
static uint32_t ui_scale_den;
#endif
static bool lv_started;
static uint16_t ui_w, ui_h;      /* runtime LVGL canvas size (pixels) */
static uint16_t ui_scan_w, ui_scan_h; /* portrait DE scanout size */
static uint32_t ui_buf_bytes;    /* = ui_w * ui_h * 4 */
static uint32_t ui_scan_bytes;   /* = ui_scan_w * ui_scan_h * 4 */
static volatile bool frame_ready;

static void ui_pick_canvas(void)
{
#ifdef AIC_USB_DISP_EBIKE_UI
    /* lv_display_set_rotation(90) swaps the logical resolution, so creating
     * the display as ui_w x ui_h = 320 x 480 gives LVGL exactly the demo's
     * 480x320 landscape design box: the pct(100) root fills it with no
     * stretch and the 480x320 wallpaper covers it edge-to-edge. */
    ui_w = EBIKE_DESIGN_HEIGHT;
    ui_h = EBIKE_DESIGN_WIDTH;
    /* Uniform scale so the rotated window fills the panel short axis; the
     * long-axis window length adapts (660 px on the 440x1920 BOE1126).  On
     * exotic panels where that would exceed the long axis, scale down to the
     * long axis instead, keeping the aspect ratio. */
    ui_scale_num = udisp_screen.width;
    ui_scale_den = ui_w;
    if ((uint32_t)ui_h * ui_scale_num > (uint32_t)udisp_screen.height * ui_scale_den) {
        ui_scale_num = udisp_screen.height;
        ui_scale_den = ui_h;
    }
    ui_scan_w = (uint16_t)((uint32_t)ui_w * ui_scale_num / ui_scale_den);
    ui_scan_h = (uint16_t)((uint32_t)ui_h * ui_scale_num / ui_scale_den);
#else
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
    ui_scan_w = ui_w;
    ui_scan_h = ui_h;
#endif
    ui_buf_bytes = (uint32_t)ui_w * ui_h * 4U;
    ui_scan_bytes = (uint32_t)ui_scan_w * ui_scan_h * 4U;
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
    /* Keep the overlay's own surface fully transparent so the hardware VIDEO
     * layer (idle background video) composes through every transparent pixel.
     * In electric-motorcycle-dashboard mode the ebike Home page is drawn on
     * top; it ships no wallpaper image, and its translucent frosted-glass
     * panel lets the video keep showing underneath. */
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(0x203040), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

#ifdef AIC_USB_DISP_EBIKE_UI
    /* Electric-motorcycle dashboard: embed the ebike demo into the active
     * screen.  The video layer plays on behind the transparent parts.  The
     * logical canvas is created at exactly the demo's 480x320 design box
     * (ui_pick_canvas), so the lv_pct(100) root fills it edge-to-edge with
     * zero stretch.  The proportional scale-up to panel size happens in
     * ui_flush(). */
    lv_demo_ebike_create(screen);
#endif

    s_ui_ctx.last_status[0] = '\0';
    s_countdown_anim_started = false;
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


#ifdef AIC_USB_DISP_EBIKE_UI
/* GE2D pass gate.  With ctrl.alpha_en=0 the GE wrote the ARGB8888 scanout
 * fully opaque (video never showed through); with the vendor alpha recipe
 * (alpha_en=1, GE_PD_SRC, pixel*global=255) the panel STILL shows the band
 * opaque white, so the cmdq-ops scaler/blitter output-alpha behaviour is
 * unproven on this silicon.  Until that is settled the fused software path
 * (byte-exact copy, alpha always preserved) is authoritative: 0 = software,
 * 1 = try GE first with CPU fallback. */
#define UI_EBIKE_USE_GE 0

/* Software fallback: fused nearest-neighbour 90/270 deg rotation + uniform
 * ui_scale_num/ui_scale_den upscale, mirroring the GE2D hardware pass.  The
 * source is the logical landscape canvas (ui_h x ui_w, stride ui_h*4), the
 * destination the portrait scanout (ui_scan_w x ui_scan_h, stride
 * ui_scan_w*4).  Mapping mirrors LVGL's rotate90_argb8888()/
 * rotate270_argb8888(): for ROTATION_90 scanout[row][col] =
 * canvas[ui_w - 1 - col][row]; for ROTATION_270 the axes flip. */
static void ui_scale_rotate(const uint8_t *src, lv_display_rotation_t rot)
{
    const uint32_t *s = (const uint32_t *)src;
    uint32_t *d = (uint32_t *)ui_scanout;
    /* 16.16 fixed-point native pixels per destination pixel (rounded up);
     * the +0.5 bias samples the canvas at destination pixel centres. */
    uint32_t step = ((uint32_t)ui_scale_den << 16) / ui_scale_num + 1U;
    uint32_t row, col;
    for (row = 0; row < ui_scan_h; row++) {
        uint32_t py = ((uint32_t)row * step + 0x8000U) >> 16; /* pre-scale portrait row */
        uint32_t *drow = d + (size_t)row * ui_scan_w;
        for (col = 0; col < ui_scan_w; col++) {
            uint32_t px = ((uint32_t)col * step + 0x8000U) >> 16; /* pre-scale portrait col */
            if (rot == LV_DISPLAY_ROTATION_90)
                drow[col] = s[(size_t)(ui_w - 1 - px) * ui_h + py];
            else
                drow[col] = s[(size_t)px * ui_h + (ui_h - 1 - py)];
        }
    }
}

/* Rotate + uniformly scale the just-rendered landscape canvas (ui_h x ui_w,
 * stride ui_h*4) into the portrait scanout buffer (ui_scan_w x ui_scan_h,
 * stride ui_scan_w*4).  The GE flag mapping mirrors lv_draw_ge2d_rotate():
 * LVGL's ROTATION_90 means the panel shows the content rotated 90 deg
 * counterclockwise = MPP_ROTATION_270.  Because src and dst sizes differ,
 * the GE cmdq ops enable the hardware scaler automatically (phases derived
 * from the size ratio, dst dimensions swapped for 90/270 deg rot0), so the
 * whole operation is a single GE2D pass.
 *
 * Alpha: with ctrl.alpha_en = 0 the GE writes the ARGB8888 dst like an
 * XRGB8888 frame, forcing every scanout pixel opaque -- the DE then covers
 * the whole band with the UI layer and the video below never shows through.
 * So enable Porter/Duff with the vendor ARGB8888 recipe (see
 * lv_draw_ge2d_img.c): alpha_en = 1, src_alpha_mode = 2 (pixel * global)
 * and src_global_alpha = 255, i.e. the source pixel alpha passes through
 * unchanged.  The PD rule is GE_PD_SRC (coeffs fs=1.0, fd=0.0): the result
 * is the source pixel verbatim, dst is never read, so the scanout buffer
 * needs no per-frame clear and the UI layer's alpha decides pixel by pixel
 * how much of the VIDEO layer shows through the DE compositor. */
static void ui_rotate_canvas(const uint8_t *src, lv_display_rotation_t rot)
{
    struct ge_bitblt blt = {0};
    bool ge_ok = UI_EBIKE_USE_GE && ui_ge != NULL &&
                 (rot == LV_DISPLAY_ROTATION_90 || rot == LV_DISPLAY_ROTATION_270);
    if (ge_ok) {
        blt.ctrl.alpha_en = 1;
        blt.ctrl.alpha_rules = GE_PD_SRC;
        blt.ctrl.src_alpha_mode = 2;
        blt.ctrl.src_global_alpha = 255;
        blt.ctrl.flags = (rot == LV_DISPLAY_ROTATION_90) ?
                             MPP_ROTATION_270 : MPP_ROTATION_90;
        blt.src_buf.buf_type = MPP_PHY_ADDR;
        blt.src_buf.phy_addr[0] = (uintptr_t)src;
        blt.src_buf.stride[0] = (int32_t)ui_h * 4;
        blt.src_buf.size.width = ui_h;
        blt.src_buf.size.height = ui_w;
        blt.src_buf.format = MPP_FMT_ARGB_8888;
        blt.dst_buf.buf_type = MPP_PHY_ADDR;
        blt.dst_buf.phy_addr[0] = (uintptr_t)ui_scanout;
        blt.dst_buf.stride[0] = (int32_t)ui_scan_w * 4;
        blt.dst_buf.size.width = ui_scan_w;
        blt.dst_buf.size.height = ui_scan_h;
        blt.dst_buf.format = MPP_FMT_ARGB_8888;
        if (mpp_ge_bitblt(ui_ge, &blt) || mpp_ge_emit(ui_ge) || mpp_ge_sync(ui_ge))
            ge_ok = false;
    }
    if (!ge_ok) {
        ui_scale_rotate(src, rot);
        /* CPU wrote the scanout: push it out before the DE starts scanning. */
        aicos_dcache_clean_range(ui_scanout, ui_scan_bytes);
        /* One-shot proof for the UART6 console: the alpha statistics of the
         * scanout buffer the DE will scan.  A healthy transparent overlay
         * must show min=0 and a large transparent count; min=255 with zero
         * transparent pixels means LVGL itself rendered opaque and the DE is
         * being asked to show a fully opaque band. */
        static bool alpha_checked;
        if (!alpha_checked) {
            alpha_checked = true;
            const uint8_t *p = (const uint8_t *)ui_scanout;
            uint32_t n = ui_scan_bytes / 4U;
            uint32_t transp = 0, min_a = 255;
            for (uint32_t i = 0; i < n; i++) {
                uint8_t a = p[i * 4U + 3U]; /* A = MSB byte of ARGB8888 */
                if (a == 0) transp++;
                if (a < min_a) min_a = a;
            }
            printf("usbdisp: scanout alpha min=%u transparent=%u/%u\n",
                   min_a, transp, n);
        }
    }
}
#endif

static void ui_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    if (lv_display_flush_is_last(disp)) {
        aicos_dcache_clean_range(pixels, ui_buf_bytes);
#ifdef AIC_USB_DISP_EBIKE_UI
        ui_rotate_canvas(pixels, lv_display_get_rotation(ui_display));
        ui_layer.buf.phy_addr[0] = (uintptr_t)ui_scanout;
#else
        ui_layer.buf.phy_addr[0] = (uintptr_t)pixels;
#endif
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
    /* Two ARGB canvases plus (ebike build) the portrait rotate+scale scanout
     * are carved from the driver's full-screen framebuffer allocation.  For
     * the 440x1920 boe1126 ebike panel that is 480*320*4*2 + 440*660*4 =
     * 2,390,400 of the 440*1920*3 = 2,534,400 available bytes; non-ebike
     * builds keep the original 2 x canvas window with scanout == canvas. */
    uint32_t ui_total_bytes = ui_buf_bytes * 2 + ui_scan_bytes;
#ifdef AIC_USB_DISP_EBIKE_UI
    if (udisp_screen.smem_len < ui_total_bytes && ui_scale_num != ui_scale_den) {
        /* Exotic panel where the scaled window would not fit: fall back to
         * the plain 1:1 rotate-only window (smaller but geometrically
         * correct) instead of losing the UI entirely. */
        ui_scale_num = ui_scale_den;
        ui_scan_w = ui_w;
        ui_scan_h = ui_h;
        ui_scan_bytes = ui_buf_bytes;
        ui_total_bytes = ui_buf_bytes * 2 + ui_scan_bytes;
    }
#endif
    if (udisp_screen.smem_len < ui_total_bytes) {
        printf("usbdisp: framebuffer too small for UI windows\n");
        return;
    }
    /* Reuse the driver's full-screen allocation for the UI windows.
     * Full rendering swaps only at VSYNC and never draws into scanout. */
    ui_buffer = udisp_screen.framebuffer;
    ui_buffers[0] = ui_buffer;
    ui_buffers[1] = ui_buffer + ui_buf_bytes;
#ifdef AIC_USB_DISP_EBIKE_UI
    ui_scanout = ui_buffer + ui_buf_bytes * 2;
    if (!ui_ge) ui_ge = mpp_ge_open();
    if (!ui_ge) printf("usbdisp: GE open failed, UI rotation falls back to software\n");
#endif
    disp = ui_display;
    if (!disp)
        disp = ui_display = lv_display_create(ui_w, ui_h);
    /* Keep LVGL's per-pixel opacity in the ARGB8888 UI layer. The base
     * framebuffer is RGB888; the UI layer has its own four-byte pixels. */
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_flush_cb(disp, ui_flush);
#ifdef AIC_USB_DISP_EBIKE_UI
    /* Zero the scanout buffer too: the CPU must leave it clean afterwards,
     * because GE2D writes it behind the cache and a later write-back of
     * stale dirty lines would corrupt the panel image. */
    memset(ui_buffer, 0, ui_total_bytes);
    aicos_dcache_clean_range(ui_buffer, ui_total_bytes);
    /* Electric-motorcycle dashboard: the ebike Home page is authored
     * landscape for 480x320.  lv_display_set_rotation() (LVGL 9.1) only
     * swaps the logical resolution, so LVGL renders an upright 480x320
     * canvas (exactly the demo design box); the pixel rotation and the
     * proportional 440/320 upscale run in ui_flush() into ui_scanout. */
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
#endif
    lv_display_set_buffers(disp, ui_buffers[0], ui_buffers[1],
                          ui_buf_bytes, LV_DISPLAY_RENDER_MODE_FULL);
    ui_layer.layer_id = AICFB_LAYER_TYPE_UI;
    ui_layer.enable = 1;
    ui_layer.pos.x = (udisp_screen.width - ui_scan_w) / 2;
    ui_layer.pos.y = (udisp_screen.height - ui_scan_h) / 2;
    ui_layer.buf.buf_type = MPP_PHY_ADDR;
#ifdef AIC_USB_DISP_EBIKE_UI
    /* DE always scans the rotated portrait buffer, never a raw canvas. */
    ui_layer.buf.phy_addr[0] = (uintptr_t)ui_scanout;
#else
    ui_layer.buf.phy_addr[0] = (uintptr_t)ui_buffer;
#endif
    ui_layer.buf.stride[0] = ui_scan_w * 4;
    ui_layer.buf.size.width = ui_scan_w;
    ui_layer.buf.size.height = ui_scan_h;
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
#ifdef AIC_USB_DISP_EBIKE_UI
    ui_scanout = NULL; /* ui_ge stays open: re-opened lazily and kept forever */
#endif
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
