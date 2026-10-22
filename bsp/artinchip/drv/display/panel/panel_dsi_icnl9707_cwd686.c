/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * ICNL9707 panel driver for the ArtInChip D133 RT-Thread display stack.
 *
 * History:
 *  - First cut used the ClockworkPi cwd686 (panel-clockwork-cwd686.c, 480x1280)
 *    init sequence + MADCTL + sync-pulse; it left the panel dark.
 *  - Current: init sequence switched back to the TXW milk-v reference (the one
 *    that lights this panel up), DSI in burst mode.  The only experimental
 *    variable left is the resolution from variants.json, currently 480x1280,
 *    against the old 440x1920 builds.
 */

#include "panel_com.h"
#include "panel_dsi.h"

/* ICNL9707 reference tables use DSI Generic Long Write for extended pages. */
#define icnl9707_cwd686_gen(panel, seq...) do { \
    panel_dsi_generic_send_seq(panel, seq); \
    aic_delay_ms(1); \
} while (0)

static struct gpio_desc pen;
static struct gpio_desc reset;

static int panel_prepare(void)
{
    pr_info("cwd686 prepare start (PEN=%s, RESET=%s)\n",
            AIC_PANEL_ICNL9707_CWD686_PEN_GPIO,
            AIC_PANEL_ICNL9707_CWD686_RESET_GPIO);
    panel_get_gpio(&pen, AIC_PANEL_ICNL9707_CWD686_PEN_GPIO);
    /* ICNL9707 PEN is active low and enables panel power when asserted. */
    panel_gpio_set_value(&pen, 0);
    aic_delay_ms(50);

    panel_get_gpio(&reset, AIC_PANEL_ICNL9707_CWD686_RESET_GPIO);
    panel_gpio_set_value(&reset, 1);
    aic_delay_ms(10);
    panel_gpio_set_value(&reset, 0);
    aic_delay_ms(50);
    panel_gpio_set_value(&reset, 1);
    aic_delay_ms(200);
    pr_info("cwd686 prepare done\n");
    return 0;
}

static int panel_enable(struct aic_panel *panel)
{
    int ret;

    pr_info("cwd686 enable start (width=%u, height=%u, 4-lane RGB888)\n",
            (unsigned int)panel->timings->hactive,
            (unsigned int)panel->timings->vactive);

    panel_di_enable(panel, 0);
    panel_de_timing_disable(panel, 0);
    panel_dsi_send_perpare(panel);

    /*
     * Init sequence switched back to the TXW milk-v reference so the panel
     * lights up (cwd686's sequence left it dark).  Only the debug logs keep
     * the cwd686 name.  Extended-page writes use DSI Generic Long Write.
     */
    icnl9707_cwd686_gen(panel, 0xF0, 0x5A, 0x59);
    icnl9707_cwd686_gen(panel, 0xF1, 0xA5, 0xA6);
    icnl9707_cwd686_gen(panel, 0xB4, 0x1D, 0x1C, 0x0B, 0x10, 0x11, 0x12,
                        0x13, 0x0C, 0x0D, 0x0E, 0x0F, 0x00, 0x1E, 0x1F,
                        0x04, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03);
    icnl9707_cwd686_gen(panel, 0xB0, 0x60, 0x00, 0x00, 0x08, 0x66, 0x66,
                        0x33, 0x33, 0x66, 0x00, 0x00, 0xAF, 0x00, 0x00, 0x0F);
    icnl9707_cwd686_gen(panel, 0xB1, 0x53, 0xA0, 0x00, 0x85, 0x0E, 0x00,
                        0x00, 0x62, 0x00, 0x00);
    icnl9707_cwd686_gen(panel, 0xB2, 0x37, 0x09, 0x08, 0x8B, 0x08, 0x00,
                        0x22, 0x00, 0x44, 0xD9);
    icnl9707_cwd686_gen(panel, 0xB6, 0x49, 0x49);
    icnl9707_cwd686_gen(panel, 0xB7, 0x01, 0x01, 0x09, 0x0D, 0x11, 0x19,
                        0x1D, 0x15, 0x00, 0x25, 0x21, 0x00, 0x00, 0x00,
                        0x00, 0x02, 0xF7, 0x38);
    icnl9707_cwd686_gen(panel, 0xB8, 0x34, 0x53, 0x02, 0xCC);
    icnl9707_cwd686_gen(panel, 0xBA, 0x27, 0x33);
    icnl9707_cwd686_gen(panel, 0xBD, 0x43, 0x0E, 0x0E, 0x4B, 0x4B, 0x14, 0x14);
    icnl9707_cwd686_gen(panel, 0xC1, 0x00, 0x0C, 0x20, 0x04, 0x00, 0x32, 0x32, 0x04);
    icnl9707_cwd686_gen(panel, 0xC2, 0x31, 0xC0);
    icnl9707_cwd686_gen(panel, 0xC3, 0x22, 0x31);
    icnl9707_cwd686_gen(panel, 0xC6, 0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0x00);
    icnl9707_cwd686_gen(panel, 0xC8, 0x7C, 0x66, 0x56, 0x49, 0x46, 0x37, 0x3A, 0x23,
                        0x3B, 0x38, 0x38, 0x55, 0x43, 0x4C, 0x40, 0x3C, 0x2F, 0x1C, 0x06,
                        0x7C, 0x65, 0x56, 0x4A, 0x46, 0x37, 0x3A, 0x23, 0x3A, 0x38, 0x38,
                        0x55, 0x43, 0x4C, 0x40, 0x3C, 0x2F, 0x1C, 0x06);
    icnl9707_cwd686_gen(panel, 0xD0, 0x07, 0xFF, 0xFF);
    icnl9707_cwd686_gen(panel, 0xD2, 0x63, 0x0B, 0x08, 0x88);
    icnl9707_cwd686_gen(panel, 0xD4, 0x00, 0x00, 0x00, 0x32, 0x04, 0x54);

    /* Close extended-command window */
    icnl9707_cwd686_gen(panel, 0xF1, 0x5A, 0x59);
    icnl9707_cwd686_gen(panel, 0xF0, 0xA5, 0xA6);

    /* COLMOD = RGB888 */
    ret = mipi_dsi_dcs_send(panel, (u8[]){ 0x3A, 0x77 }, 2);
    if (ret < 0)
        pr_err("cwd686 COLMOD failed: %d\n", ret);

    ret = panel_dsi_dcs_exit_sleep_mode(panel);
    if (ret < 0) {
        pr_err("cwd686 sleep-out failed: %d\n", ret);
        return ret;
    }
    aic_delay_ms(200);
    ret = panel_dsi_dcs_set_display_on(panel);
    if (ret < 0) {
        pr_err("cwd686 display-on failed: %d\n", ret);
        return ret;
    }
    aic_delay_ms(50);

    panel_dsi_setup_realmode(panel);
    panel_de_timing_enable(panel, 0);

    panel_backlight_enable(panel, 0);
    pr_info("cwd686 enable done\n");
    return 0;
}

static struct aic_panel_funcs panel_funcs = {
    .disable = panel_default_disable,
    .unprepare = panel_default_unprepare,
    .prepare = panel_prepare,
    .enable = panel_enable,
    .register_callback = panel_register_callback,
};

/* Reference timings from cwd686 default_mode (480x1280).  The variant still
 * drives hactive/vactive/pclk through FIRMWARE_* so a 440x1920 comparison
 * build can reuse the same init sequence. */
static struct display_timing cwd686_timing = {
    .pixelclock = FIRMWARE_PIXELCLOCK,
    .hactive = FIRMWARE_WIDTH,
    .hfront_porch = FIRMWARE_HFRONT_PORCH,
    .hback_porch = FIRMWARE_HBACK_PORCH,
    .hsync_len = FIRMWARE_HSYNC_LEN,
    .vactive = FIRMWARE_HEIGHT,
    .vfront_porch = FIRMWARE_VFRONT_PORCH,
    .vback_porch = FIRMWARE_VBACK_PORCH,
    .vsync_len = FIRMWARE_VSYNC_LEN,
};

static struct panel_dsi dsi = {
    /* Match the TXW init sequence: burst video mode (cwd686's sync-pulse was
     * tried together with its init and left the panel dark). */
    .mode = DSI_MOD_VID_BURST,
    .format = DSI_FMT_RGB888,
    .lane_num = FIRMWARE_LANE_NUM,
    .vc_num = VIRTUAL_CHANNEL,
    .ln_assign = FIRMWARE_LN_ASSIGN,
    .ln_polrs = FIRMWARE_LN_POLRS,
    .dc_inv = 0,
};

struct aic_panel dsi_icnl9707_cwd686 = {
    .name = "panel-icnl9707-cwd686",
    .timings = &cwd686_timing,
    .funcs = &panel_funcs,
    .dsi = &dsi,
    .connector_type = AIC_MIPI_COM,
};