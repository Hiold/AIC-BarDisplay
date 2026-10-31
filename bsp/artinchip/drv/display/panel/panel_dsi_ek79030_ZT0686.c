/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * EK79030 (DSI driver IC) + ZT0686 panel driver for the ArtInChip D133 RT-Thread
 * display stack, ported from the Luban-Lite mipiPI board reference
 * (panel_dsi_ek79030_ZT0686.c) for the mipiPI-40pin MIPI screen development board.
 *
 * Panel: 480x1280 RGB888, 4-lane MIPI DSI, video burst mode, DSI
 * generic-command (Extended) initialisation. Resolution/timings are driven
 * through the FIRMWARE_* macros so variants.json remains the single source.
 *
 * mipiPI-40pin board wiring (SoC PD bank, matches SDK mipiPI defconfig):
 *   DSI lanes      : PD.18..PD.27  (D0..D3 P/N + CKP/N)
 *   DSI_EN (PD14)  : panel power / EK79030 enable  (sleep gpio, driven high)
 *   DSI_RST (PD15) : panel reset                    (reset pulse)
 */

#include "panel_com.h"
#include "panel_dsi.h"

#define ek79030_zt0686_gen(panel, seq...) do { \
    panel_dsi_generic_send_seq(panel, seq); \
    aic_delay_ms(1); \
} while (0)

static struct gpio_desc reset_gpio;
static struct gpio_desc sleep_gpio;

static void panel_gpio_init(struct aic_panel *panel)
{
    panel_get_gpio(&sleep_gpio, AIC_PANEL_EK79030_ZT0686_ENABLE_GPIO);
    panel_get_gpio(&reset_gpio, AIC_PANEL_EK79030_ZT0686_RESET_GPIO);

    /* EK79030 enable pin: on = panel powered/DSI awake */
    panel_gpio_set_value(&sleep_gpio, 1);

    /* reset pulse: high -> low -> high */
    panel_gpio_set_value(&reset_gpio, 1);
    aic_delay_ms(100);
    panel_gpio_set_value(&reset_gpio, 0);
    aic_delay_ms(20);
    panel_gpio_set_value(&reset_gpio, 1);
    aic_delay_ms(10);
}

static int panel_enable(struct aic_panel *panel)
{
    int ret;

    panel_gpio_init(panel);

    panel_di_enable(panel, 0);
    panel_dsi_send_perpare(panel);

    /* EK79030 Extended-register init sequence (from mipiPI reference). */
    ek79030_zt0686_gen(panel, 0xCD, 0xAA);

    ek79030_zt0686_gen(panel, 0x65, 0x08); /* 0xAC / 0x08 */

    ek79030_zt0686_gen(panel, 0x3A, 0x14);

    ek79030_zt0686_gen(panel, 0x32, 0x00); /* BIST */

    ek79030_zt0686_gen(panel, 0x36, 0x02);

    ek79030_zt0686_gen(panel, 0x67, 0x82);

    ek79030_zt0686_gen(panel, 0x69, 0x20);

    ek79030_zt0686_gen(panel, 0x6D, 0x01);

    ek79030_zt0686_gen(panel, 0x68, 0x00);

    ek79030_zt0686_gen(panel, 0x53,
                       0x1F, 0x19, 0x17, 0x09, 0x09,
                       0x10, 0x14, 0x14, 0x14, 0x09,
                       0x0A, 0x06, 0x05, 0x08, 0x07,
                       0x09, 0x06, 0x05, 0x04);

    ek79030_zt0686_gen(panel, 0x54,
                       0x1F, 0x19, 0x16, 0x17, 0x17,
                       0x12, 0x11, 0x14, 0x14, 0x16,
                       0x08, 0x08, 0x07, 0x07, 0x0A,
                       0x0B, 0x08, 0x06, 0x06);

    ek79030_zt0686_gen(panel, 0x29, 0x10);

    ek79030_zt0686_gen(panel, 0x2A, 0x0C);

    ek79030_zt0686_gen(panel, 0x6C, 0x81);

    ek79030_zt0686_gen(panel, 0x55,
                       0x00, 0x0F, 0x00, 0x0F,
                       0x00, 0x0F, 0x00, 0x0F);

    ek79030_zt0686_gen(panel, 0x56,
                       0x00, 0x0F, 0x00, 0x0F,
                       0x00, 0x0F, 0x00, 0x0F,
                       0x00, 0x0F, 0x00, 0x0F,
                       0x00, 0x0F, 0x00, 0x0F);

    ek79030_zt0686_gen(panel, 0x57,
                       0x00, 0x00, 0x00, 0x00);

    ek79030_zt0686_gen(panel, 0x30, 0x00);

    ek79030_zt0686_gen(panel, 0x39, 0x11);

    ek79030_zt0686_gen(panel, 0x33, 0x08);

    ek79030_zt0686_gen(panel, 0x35, 0x24);

    ek79030_zt0686_gen(panel, 0x4F, 0x51);

    ek79030_zt0686_gen(panel, 0x4E, 0x45);

    ek79030_zt0686_gen(panel, 0x41, 0x4E); /* 0x5E */

    ek79030_zt0686_gen(panel, 0x73, 0x30);

    ek79030_zt0686_gen(panel, 0x74, 0x10);

    ek79030_zt0686_gen(panel, 0x76, 0x40);

    ek79030_zt0686_gen(panel, 0x77, 0x00);

    ek79030_zt0686_gen(panel, 0x28, 0x31);

    ek79030_zt0686_gen(panel, 0x7C, 0x80);

    ek79030_zt0686_gen(panel, 0x2E, 0x04); /* PWM */

    ek79030_zt0686_gen(panel, 0x4C, 0x80); /* VCSW1 */

    ek79030_zt0686_gen(panel, 0x47, 0x16);

    ek79030_zt0686_gen(panel, 0x48, 0x6A);

    ek79030_zt0686_gen(panel, 0x50, 0xC0);

    ek79030_zt0686_gen(panel, 0x78, 0x6E);

    ek79030_zt0686_gen(panel, 0x2D, 0x31);

    ek79030_zt0686_gen(panel, 0x63, 0x04);

    ek79030_zt0686_gen(panel, 0x4D, 0x00);

    ret = panel_dsi_dcs_exit_sleep_mode(panel);
    if (ret < 0) {
        pr_err("Failed to exit sleep mode: %d\n", ret);
        return ret;
    }
    aic_delay_ms(120);

    ret = panel_dsi_dcs_set_display_on(panel);
    if (ret < 0) {
        pr_err("Failed to set display on: %d\n", ret);
        return ret;
    }
    aic_delay_ms(5);

    panel_dsi_setup_realmode(panel);

    panel_de_timing_enable(panel, 0);
    panel_backlight_enable(panel, 0);
    return 0;
}

static struct aic_panel_funcs panel_funcs = {
    .disable = panel_default_disable,
    .unprepare = panel_default_unprepare,
    .prepare = panel_default_prepare,
    .enable = panel_enable,
    .register_callback = panel_register_callback,
};

/* Reference timing from the mipiPI SDK EK79030_ZT0686 (480x1280). Driven through
 * FIRMWARE_* so variants.json stays the authority. */
static struct display_timing ek79030_zt0686_timing = {
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
    .mode = DSI_MOD_VID_BURST,
    .format = DSI_FMT_RGB888,
    .lane_num = FIRMWARE_LANE_NUM,
    .vc_num = VIRTUAL_CHANNEL,
    .ln_assign = FIRMWARE_LN_ASSIGN,
    .ln_polrs = FIRMWARE_LN_POLRS,
    .dc_inv = 0,
};

struct aic_panel dsi_ek79030_zt0686 = {
    .name = "panel-ek79030-zt0686",
    .timings = &ek79030_zt0686_timing,
    .funcs = &panel_funcs,
    .dsi = &dsi,
    .connector_type = AIC_MIPI_COM,
};