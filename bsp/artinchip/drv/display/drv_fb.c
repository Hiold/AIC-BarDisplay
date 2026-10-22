/*
 * Copyright (C) 2024-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: huahui.mai@artinchip.com
 */

#include <aic_core.h>
#include <aic_hal_ge.h>
#include "drv_fb.h"
#include "drv_fb_helper.h"
#ifdef AIC_FB_ROTATE_EN
#include <aic_drv_ge.h>
#endif
#if defined(KERNEL_RTTHREAD)
#include <drivers/pm.h>
#endif

#undef pr_debug
#ifdef AIC_FB_DRV_DEBUG
#define pr_debug    pr_info
#else
#define pr_debug(fmt, ...)
#endif

static struct aicfb_info *g_aicfb_info;
static bool aicfb_probed = false;

static inline struct aicfb_info *aicfb_get_drvdata(void)
{
    return g_aicfb_info;
}

static inline void aicfb_set_drvdata(struct aicfb_info *fbi)
{
    g_aicfb_info = fbi;
}

#ifdef AIC_FB_ROTATE_EN
static int aicfb_rotate(struct aicfb_info *fbi, struct aicfb_layer_data *layer,
                u32 buf_id)
{
    struct ge_bitblt blt = {0};
    struct aic_ge_client *client = NULL;

    /* source buffer */
    blt.src_buf.buf_type = MPP_PHY_ADDR;
    blt.src_buf.phy_addr[0] = (uintptr_t)fbi->fb_start + fbi->fb_size * buf_id;
    blt.src_buf.stride[0] = fbi->stride;
    blt.src_buf.size.width = fbi->width;
    blt.src_buf.size.height = fbi->height;
    blt.src_buf.format = layer->buf.format;

    /* destination buffer */
    blt.dst_buf.buf_type = MPP_PHY_ADDR;
    blt.dst_buf.phy_addr[0] = layer->buf.phy_addr[0];
    blt.dst_buf.stride[0] = layer->buf.stride[0];
    blt.dst_buf.size.width = layer->buf.size.width;
    blt.dst_buf.size.height = layer->buf.size.height;
    blt.dst_buf.format = layer->buf.format;

    switch (fbi->fb_rotate) {
    case 0:
        blt.ctrl.flags = 0;
        break;
    case 90:
        blt.ctrl.flags = MPP_ROTATION_90;
        break;
    case 180:
        blt.ctrl.flags = MPP_ROTATION_180;
        break;
    case 270:
        blt.ctrl.flags = MPP_ROTATION_270;
        break;
    default:
        pr_err("Invalid rotation degree\n");
        return -EINVAL;
    };

    return hal_ge_control(client, IOC_GE_BITBLT, &blt);
}
#endif

static int aicfb_pan_display(struct aicfb_info *fbi, u32 buf_id)
{
    struct aicfb_layer_data layer = {0};
    struct platform_driver *de = fbi->de;

#if !defined(AIC_PAN_DISPLAY) && !defined(AIC_FB_ROTATE_EN)
#ifdef AIC_BOOTLOADER_CMD_PROGRESS_BAR
#else
    pr_err("pan display do not enabled\n");
    return -EINVAL;
#endif
#endif

    layer.layer_id = AICFB_LAYER_TYPE_UI;
    layer.rect_id = 0;
    de->de_funcs->get_layer_config(&layer);

    layer.enable = 1;
    layer.buf.phy_addr[0] = (uintptr_t)fbi->fb_start + fbi->fb_size * buf_id;

#ifdef AIC_FB_ROTATE_EN
    if (fbi->fb_rotate)
    {
        layer.buf.phy_addr[0] += fbi->fb_size * AIC_FB_DRAW_BUF_NUM;

        aicfb_rotate(fbi, &layer, buf_id);
    }
#endif

    de->de_funcs->update_layer_config(&layer);
    return 0;
}

static int aicfb_ioctl_wait_for_vsync(struct aicfb_info *fbi, void *args)
{
    return fbi->de->de_funcs->wait_for_vsync();
}

static int aicfb_ioctl_get_screen_register(struct aicfb_info *fbi, void *args)
{
    if (!fbi->di->di_funcs->read_cmd) {
        pr_err("display interface do not supports AICFB_GET_SCREENREG\n");
        return -EINVAL;
    }

    return fbi->di->di_funcs->read_cmd(*(u32 *)args);
}

static int aicfb_ioctl_get_screen_info(struct aicfb_info *fbi, void *args)
{
    struct aicfb_screeninfo *info;

    info = (struct aicfb_screeninfo *) args;
    info->format = AICFB_FORMAT;
    info->bits_per_pixel = fbi->bits_per_pixel;
    info->stride = fbi->stride;
    info->framebuffer = (unsigned char *)fbi->fb_start;
    info->width = fbi->width;
    info->height = fbi->height;
    info->smem_len = fbi->fb_size;
    return 0;
}

static int aicfb_ioctl_pan_display(struct aicfb_info *fbi, void *args)
{
    return aicfb_pan_display(fbi, *(int *)args);
}

static int aicfb_ioctl_power_on(struct aicfb_info *fbi, void *args)
{
    struct aicfb_layer_data layer = {0};
    struct platform_driver *de = fbi->de;

    if (fbi->power_on)
        return 0;

    aicfb_enable_clk(fbi, AICFB_ON);

    layer.layer_id = AICFB_LAYER_TYPE_UI;
    layer.rect_id = 0;
    de->de_funcs->get_layer_config(&layer);

    layer.enable = 1;
    de->de_funcs->update_layer_config(&layer);

    aicfb_enable_panel(fbi, AICFB_ON);
    fbi->power_on = true;
    return 0;
}

static int aicfb_ioctl_power_off(struct aicfb_info *fbi, void *args)
{
    struct aicfb_layer_data layer = {0};
    struct platform_driver *de = fbi->de;

    if (!fbi->power_on)
        return 0;

    aicfb_enable_panel(fbi, AICFB_OFF);

    layer.layer_id = AICFB_LAYER_TYPE_UI;
    layer.rect_id = 0;
    de->de_funcs->get_layer_config(&layer);

    layer.enable = 0;
    de->de_funcs->update_layer_config(&layer);

    aicfb_enable_clk(fbi, AICFB_OFF);
    fbi->power_on = false;
    return 0;
}

static int aicfb_ioctl_get_layer_cfg(struct aicfb_info *fbi, void *args)
{
    return fbi->de->de_funcs->get_layer_config(args);
}

static int aicfb_ioctl_update_layer_cfg(struct aicfb_info *fbi, void *args)
{
    return fbi->de->de_funcs->update_layer_config(args);
}

static int aicfb_ioctl_update_layer_cfg_list(struct aicfb_info *fbi, void *args)
{
    return fbi->de->de_funcs->update_layer_config_list(args);
}

static int aicfb_ioctl_get_alpha_cfg(struct aicfb_info *fbi, void *args)
{
    return fbi->de->de_funcs->get_alpha_config(args);
}

static int aicfb_ioctl_update_alpha_cfg(struct aicfb_info *fbi, void *args)
{
    return fbi->de->de_funcs->update_alpha_config(args);
}

static int aicfb_ioctl_get_ck_cfg(struct aicfb_info *fbi, void *args)
{
    return fbi->de->de_funcs->get_ck_config(args);
}

static int aicfb_ioctl_update_ck_cfg(struct aicfb_info *fbi, void *args)
{
    return fbi->de->de_funcs->update_ck_config(args);
}

static int aicfb_ioctl_set_disp_prop(struct aicfb_info *fbi, void *args)
{
    return fbi->de->de_funcs->set_display_prop(args);
}

static int aicfb_ioctl_get_disp_prop(struct aicfb_info *fbi, void *args)
{
    u32 ret = 0;
    struct aicfb_disp_prop prop;

    ret = fbi->de->de_funcs->get_display_prop(&prop);
    if (ret)
        return ret;

    memcpy(args, &prop, sizeof(struct aicfb_disp_prop));
    return ret;
}

static int aicfb_ioctl_get_ccm_cfg(struct aicfb_info *fbi, void *args)
{
    return fbi->de->de_funcs->get_ccm_config(args);
}

static int aicfb_ioctl_update_ccm_cfg(struct aicfb_info *fbi, void *args)
{
    return fbi->de->de_funcs->set_ccm_config(args);
}

static int aicfb_ioctl_update_gamma_cfg(struct aicfb_info *fbi, void *args)
{
    return fbi->de->de_funcs->set_gamma_config(args);
}

static int aicfb_ioctl_get_gamma_cfg(struct aicfb_info *fbi, void *args)
{
    return fbi->de->de_funcs->get_gamma_config(args);
}

static int aicfb_ioctl_update_bg_blend_ui_cfg(struct aicfb_info *fbi, void *args)
{
    return fbi->de->de_funcs->update_bg_blend_ui_config(*(u32 *)args);
}

static int aicfb_ioctl_get_bg_blend_ui_cfg(struct aicfb_info *fbi, void *args)
{
    return fbi->de->de_funcs->get_bg_blend_ui_config(args);
}

#ifdef AIC_DISPLAY_TEST
static int aicfb_ioctl_pq_set_cfg(struct aicfb_info *fbi, void *args)
{
    struct aicfb_pq_config *config = args;

    aicfb_pq_set_config(fbi, config);

    return 0;
}
static int aicfb_ioctl_pq_get_cfg(struct aicfb_info *fbi, void *args)
{
    struct aicfb_pq_config *config = args;

    aicfb_pq_get_config(fbi, config);
    return 0;
}
#endif /* AIC_DISPLAY_TEST */

struct aicfb_ioctl_cmd {
    u32 cmd;
    int (*f)(struct aicfb_info *fbi, void *args);
    char desc[32];
};

struct aicfb_ioctl_cmd aicfb_ioctl_cmds[] = {
{AICFB_WAIT_FOR_VSYNC, aicfb_ioctl_wait_for_vsync, "Wait for vsync"},
{AICFB_PAN_DISPLAY, aicfb_ioctl_pan_display, "Pan display"},
{AICFB_UPDATE_LAYER_CONFIG, aicfb_ioctl_update_layer_cfg, "Update layer cfg"},
{AICFB_GET_LAYER_CONFIG, aicfb_ioctl_get_layer_cfg,"Get layer cfg"},
{AICFB_GET_SCREENINFO, aicfb_ioctl_get_screen_info, "Get screen info"},
{AICFB_UPDATE_LAYER_CONFIG_LISTS, aicfb_ioctl_update_layer_cfg_list, "Update layer cfg list"},
{AICFB_GET_ALPHA_CONFIG, aicfb_ioctl_get_alpha_cfg, "Get alpha cfg"},
{AICFB_UPDATE_ALPHA_CONFIG, aicfb_ioctl_update_alpha_cfg, "Update alpha cfg"},
{AICFB_GET_CK_CONFIG, aicfb_ioctl_get_ck_cfg, "Get Color Key cfg"},
{AICFB_UPDATE_CK_CONFIG, aicfb_ioctl_update_ck_cfg, "Update Color Key cfg"},
{AICFB_SET_DISP_PROP, aicfb_ioctl_set_disp_prop, "Set display prop"},
{AICFB_GET_DISP_PROP, aicfb_ioctl_get_disp_prop, "Get display prop"},
{AICFB_GET_CCM_CONFIG, aicfb_ioctl_get_ccm_cfg, "Get CCM cfg"},
{AICFB_UPDATE_CCM_CONFIG, aicfb_ioctl_update_ccm_cfg, "Update CCM cfg"},
{AICFB_UPDATE_GAMMA_CONFIG, aicfb_ioctl_update_gamma_cfg, "Update gamma cfg"},
{AICFB_GET_GAMMA_CONFIG, aicfb_ioctl_get_gamma_cfg, "Get gamma cfg"},
{AICFB_UPDATE_BG_BLEND_UI_CONFIG, aicfb_ioctl_update_bg_blend_ui_cfg, "Update bg blend ui cfg"},
{AICFB_GET_BG_BLEND_UI_CONFIG, aicfb_ioctl_get_bg_blend_ui_cfg, "Get bg blend ui cfg"},
{AICFB_POWERON, aicfb_ioctl_power_on, "Power on screen"},
{AICFB_POWEROFF, aicfb_ioctl_power_off, "Power off screen"},
{AICFB_GET_SCREENREG, aicfb_ioctl_get_screen_register, "Get screen register"},
#ifdef AIC_DISPLAY_TEST
{AICFB_PQ_SET_CONFIG, aicfb_ioctl_pq_set_cfg, "PQ Tools set cfg"},
{AICFB_PQ_GET_CONFIG, aicfb_ioctl_pq_get_cfg, "PQ Tools get cfg"},
#endif
{0xFFFF, NULL, ""}
};

int aicfb_ioctl(int cmd, void *args)
{
    struct aicfb_ioctl_cmd *ioctl = aicfb_ioctl_cmds;
    struct aicfb_info *fbi = aicfb_get_drvdata();

    do {
        if ((ioctl->cmd == cmd) && ioctl->f)
            return ioctl->f(fbi, args);

        ioctl++;
    } while (ioctl->cmd != 0xFFFF);

    pr_err("Invalid ioctl cmd %#x\n", cmd);
    return -EINVAL;
}

#if defined(KERNEL_RTTHREAD)
#ifdef RT_USING_PM
static int aicfb_suspend(const struct rt_device *device, rt_uint8_t mode)
{
    struct aicfb_info *fbi = device->user_data;
    struct platform_driver *de = fbi->de;

    if (!aicfb_probed)
        return 0;

    switch (mode)
    {
    case PM_SLEEP_MODE_IDLE:
        break;
    case PM_SLEEP_MODE_LIGHT:
    case PM_SLEEP_MODE_DEEP:
    case PM_SLEEP_MODE_STANDBY:
    {
        struct aicfb_layer_data layer = {0};

        #ifdef AIC_PM_INDEPENDENT_POWER_KEY
        fbi->panel->independent_pwkey = 1;
        #endif
        aicfb_enable_panel(fbi, AICFB_OFF);

        layer.layer_id = AICFB_LAYER_TYPE_UI;
        layer.rect_id = 0;
        de->de_funcs->get_layer_config(&layer);

        layer.enable = 0;
        de->de_funcs->update_layer_config(&layer);

        aicfb_enable_clk(fbi, AICFB_OFF);
    }
        break;
    default:
        break;
    }

    return 0;
}

static void aicfb_resume(const struct rt_device *device, rt_uint8_t mode)
{
    struct aicfb_info *fbi = device->user_data;
    struct platform_driver *de = fbi->de;

    if (!aicfb_probed)
        return;

    switch (mode)
    {
    case PM_SLEEP_MODE_IDLE:
        break;
    case PM_SLEEP_MODE_LIGHT:
    case PM_SLEEP_MODE_DEEP:
    case PM_SLEEP_MODE_STANDBY:
    {
        struct aicfb_layer_data layer = {0};

        aicfb_enable_clk(fbi, AICFB_ON);

        layer.layer_id = AICFB_LAYER_TYPE_UI;
        layer.rect_id = 0;
        de->de_funcs->get_layer_config(&layer);

        layer.enable = 1;
        de->de_funcs->update_layer_config(&layer);
        aicfb_enable_panel(fbi, AICFB_ON);
        #ifdef AIC_PM_INDEPENDENT_POWER_KEY
        fbi->panel->independent_pwkey = 0;
        #endif
    }
        break;
    default:
        break;
    }
}

static struct rt_device_pm_ops aicfb_pm_ops =
{
    SET_DEVICE_PM_OPS(aicfb_suspend, aicfb_resume)
    NULL,
};
#endif /* RT_USING_PM */

rt_err_t aicfb_control(rt_device_t dev, int cmd, void *args)
{
    struct aicfb_info *fbi = aicfb_get_drvdata();
    int command;

    if (!fbi)
        return RT_EINVAL;

    switch (cmd)
    {
    case RTGRAPHIC_CTRL_WAIT_VSYNC:
        command = AICFB_WAIT_FOR_VSYNC;
        break;
    case RTGRAPHIC_CTRL_GET_INFO:
    {
        struct rt_device_graphic_info *info;

        info = (struct rt_device_graphic_info *) args;
        info->pixel_format = AICFB_FORMAT;
        info->bits_per_pixel = fbi->bits_per_pixel;
        info->pitch = (rt_uint16_t)fbi->stride;
        info->framebuffer = (rt_uint8_t*)fbi->fb_start;
        info->width = (rt_uint16_t)fbi->width;
        info->height = (rt_uint16_t)fbi->height;
        info->smem_len = fbi->fb_size;

        return RT_EOK;
    }
    case RTGRAPHIC_CTRL_PAN_DISPLAY:
        command = AICFB_PAN_DISPLAY;
        break;
    case RTGRAPHIC_CTRL_POWERON:
        command = AICFB_POWERON;
        break;
    case RTGRAPHIC_CTRL_POWEROFF:
        command = AICFB_POWEROFF;
        break;
    default:
        command = cmd;
        break;
    }

    return aicfb_ioctl(command, args);
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops aicfb_ops =
{
    RT_NULL,
    RT_NULL,
    RT_NULL,
    RT_NULL,
    RT_NULL,
    aicfb_control
};
#endif /* RT_USING_DEVICE_OPS */

static int aic_rt_fb_device_init(struct aicfb_info *fbi)
{
    struct rt_device *device = &fbi->device;
    int ret;

    device->type = RT_Device_Class_Graphic;

#ifdef RT_USING_DEVICE_OPS
    device->ops = &aicfb_ops;
#else
    device->init = RT_NULL;
    device->open = RT_NULL;
    device->close = RT_NULL;
    device->read = RT_NULL;
    device->write = RT_NULL;
    device->control = aicfb_control;
#endif /* RT_USING_DEVICE_OPS */

    device->user_data = fbi;

    /* register to device manager */
    ret = rt_device_register(device, "aicfb", RT_DEVICE_FLAG_RDWR);
    if(ret != RT_EOK)
    {
        pr_err("aicfb registered fail!\n");
        return ret;
    }

#ifdef RT_USING_PM
    rt_pm_device_register(device, &aicfb_pm_ops);
#endif
    return RT_EOK;
}
#endif /* KERNEL_RTTHREAD */

static struct platform_driver *drivers[] = {
#ifdef AIC_DISP_DE_DRV
    &artinchip_de_driver,
#endif
#ifdef AIC_DISP_RGB
    &artinchip_rgb_driver,
#endif
#ifdef AIC_DISP_LVDS
    &artinchip_lvds_driver,
#endif
#ifdef AIC_DISP_MIPI_DSI
    &artinchip_dsi_driver,
#endif
#ifdef AIC_DISP_MIPI_DBI
    &artinchip_dbi_driver,
#endif
};

static struct platform_driver *
aicfb_find_component(struct platform_driver **drv, int id, int len)
{
    struct platform_driver **driver = drv;
    int ret, i;

    for (i = 0; i < len; i++)
    {
        if (driver[i]->component_type == id)
            break;
    }

    if (i >= len || !driver[i] || !driver[i]->probe)
        return NULL;

    ret = driver[i]->probe();
    if (ret)
        return NULL;

    pr_debug("find component: %s\n", driver[i]->name);

    return driver[i];
}

static inline size_t aicfb_calc_fb_size(struct aicfb_info *fbi)
{
    unsigned int draw_buf_num = 0;
    unsigned int disp_buf_num = 0;

#ifdef AIC_FB_ROTATE_EN
    draw_buf_num = AIC_FB_DRAW_BUF_NUM;
#else
    draw_buf_num = 0;
#endif

#ifdef AIC_PAN_DISPLAY
    disp_buf_num = 2;
#else
    disp_buf_num = 1;
#endif

     return fbi->fb_size * (disp_buf_num + draw_buf_num);
}

int aicfb_probe(void)
{
    struct aicfb_info *fbi;
#ifndef AIC_MPP_VIN_DEV
    size_t fb_size = 0;
#endif
    int ret = -EINVAL;

    if (aicfb_probed)
        return 0;

    fbi = aicos_malloc(0, sizeof(*fbi));
    if (!fbi)
    {
        pr_err("alloc fb_info failed\n");
        return -ENOMEM;
    }
    memset(fbi, 0x0, sizeof(*fbi));

    fbi->de = aicfb_find_component(drivers, AIC_DE_COM, ARRAY_SIZE(drivers));
    if (!fbi->de)
    {
        pr_err("failed to find de component\n");
        goto err;
    }

    fbi->di = aicfb_find_component(drivers, AIC_DI_TYPE, ARRAY_SIZE(drivers));
    if (!fbi->di)
    {
        pr_err("failed to find di component\n");
        goto err;
    }

    fbi->panel = aic_find_panel(AIC_DI_TYPE);
    if (!fbi->panel)
    {
        pr_err("failed to find panel component\n");
        goto err;
    }

    aicfb_get_panel_info(fbi);
    aicfb_fb_info_setup(fbi);
    aicfb_register_panel_callback(fbi);

#ifndef AIC_MPP_VIN_DEV
    fb_size = aicfb_calc_fb_size(fbi);
    /* fb_start must be cache line align */
    fbi->fb_start = aicos_malloc_align(MEM_CMA, fb_size, CACHE_LINE_SIZE);
    if (!fbi->fb_start)
    {
        ret = -ENOMEM;
        pr_err("alloc frame buffer failed\n");
        goto err;
    }
    pr_info("fb0 allocated at 0x%x\n", (u32)(uintptr_t)fbi->fb_start);

    fb_color_block(fbi, fb_size);
#endif

#if defined(KERNEL_RTTHREAD)
    ret = aic_rt_fb_device_init(fbi);
    if(ret != RT_EOK)
        goto err;
#endif

    aicfb_probed = true;
    fbi->power_on = false;
    aicfb_set_drvdata(fbi);

    aicfb_enable_clk(fbi, AICFB_ON);
    aicfb_update_alpha(fbi);
    aicfb_update_layer(fbi);

#if defined(AIC_DISP_COLOR_BLOCK) || defined(AIC_DISP_BLACK_SCREEN)
    fbi->power_on = true;
    aicfb_enable_panel(fbi, AICFB_ON);
#endif
    return 0;

err:
    free(fbi);
    fbi = NULL;
    return ret;
}
#if defined(KERNEL_RTTHREAD) && !defined(AIC_MPP_VIN_DEV)
INIT_DEVICE_EXPORT(aicfb_probe);
#endif

void aicfb_remove(void)
{
    struct aicfb_info *fbi = aicfb_get_drvdata();

    aicfb_probed = false;

    /*
     * FIXME: We should release fbi and framebuffer at the same time.
     * But we keep the fbi pointer to ensure that some ioctl still works properly.
     */
    if (fbi->fb_start)
        aicos_free_align(MEM_CMA, fbi->fb_start);
}

#if defined(KERNEL_RTTHREAD) && defined(RT_USING_FINSH)
#include <finsh.h>
#include <string.h>
#include <stdlib.h>
#include <mipi_display.h>

/*
 * On-board helper for bringing up an unknown panel: inspect/tweak the
 * active timing and DSI lane setup at runtime, read back panel registers
 * over the DSI generic interface, and paint simple test patterns.
 * DE and DSI both program from the panel->timings pointer, so editing the
 * structure in memory plus one panel OFF/ON cycle applies new parameters
 * without reflashing.
 */
static const char dispdbg_usage[] =
    "usage:\n"
    "  dispdbg info                    show current timing/DSI/fb parameters\n"
    "  dispdbg set <key> <value>       key: pclk hfp hbp hsync vfp vbp vsync fps lane assign polrs fmt\n"
    "                                  fmt: 0=RGB888 1=RGB666L 2=RGB666 3=RGB565\n"
    "  dispdbg res <w> <h>             change active resolution (guarded by fb size)\n"
    "  dispdbg cmd <c> [p0 p1 ...]     send DCS command, e.g.: cmd 0x3a 0x77 / cmd 0x36 0x00\n"
    "  dispdbg read <cmd>              read DSI_GEN_PD_CFG (NOTE: payload reg, not panel reply)\n"
    "  dispdbg pattern <id>            0:color block 1:white 2:black 3:red 4:green 5:blue 6:h-grad 7:v-grad\n"
    "  dispdbg reset                   re-init panel with current parameters\n";

static void dispdbg_reload(struct aicfb_info *fbi)
{
    /*
     * Mirror the driver's own aicfb_reset() sequence (drv_fb_helper.c) and
     * the aicfb_probe() bring-up order. Two steps are mandatory and were
     * missing from the first cut:
     *
     *   1. Stop the DE and cycle its clocks while the panel/PHY are down.
     *      Otherwise the DE keeps scanning a framebuffer that no longer has
     *      a live DSI stream behind it and reports DE UNDERFLOW, while the
     *      DSI PHY fails its stop-state handshake (dsi_hs_clk timeout).
     *   2. Reprogram alpha blending and the UI layer geometry from the
     *      possibly edited timings *before* the panel comes back, so the DE
     *      scans with the new hactive/vactive and stride.
     *
     * Also re-run attach_panel() before the clocks come back on: clk_enable()
     * programs the DSS/PLL from comp->sclk_rate / pll_disp_rate, which are
     * only derived inside dsi_attach_panel() from pixclk x bpp / lanes. The
     * "set pclk/fps/lane" path edits timings but the online scan never
     * refreshed those clock rates, so trying a higher pixelclock to lift the
     * DSI lane rate had no effect. Re-attaching makes the D-PHY HS clock and
     * PIX clock track the edited timing on every reload.
     */
    if (fbi->di && fbi->di->di_funcs && fbi->di->di_funcs->attach_panel)
        fbi->di->di_funcs->attach_panel(fbi->panel, fbi->desc);

    aicfb_enable_panel(fbi, AICFB_OFF);

    aicfb_enable_clk(fbi, AICFB_OFF);
    aic_delay_ms(50);
    aicfb_enable_clk(fbi, AICFB_ON);

    /*
     * aicfb_enable_clk(OFF) above hard-resets the whole DE (aic_de_clk_disable
     * -> hal_reset_assert(RESET_DE)), which clears the UI layer master-enable
     * and input-format bits in UI_LAYER_CTRL. The subsequent aicfb_update_layer
     * would normally re-arm those, but its gating (config_ui_layer_rect ->
     * is_all_rect_win_disabled) is decided from the software copy
     * comp->layers[].enable, which survives the reset as 1 and makes it skip
     * de_ui_layer_enable()/de_set_ui_layer_format(). The layer therefore stays
     * off and the framebuffer is never scanned (screen lights up but shows no
     * test pattern, and dispdbg pattern <id> has no effect). Force the layer
     * disabled first so the re-enable path below actually runs and re-arms
     * UI_LAYER_CTRL.
     */
    {
        struct platform_driver *de = fbi->de;
        struct aicfb_layer_data disable = {0};

        disable.layer_id = AICFB_LAYER_TYPE_UI;
        disable.rect_id = 0;
        disable.enable = 0;
        de->de_funcs->update_layer_config(&disable);
    }

    aicfb_update_alpha(fbi);
    aicfb_update_layer(fbi);

    aicfb_enable_panel(fbi, AICFB_ON);

    /*
     * the panel driver's enable() re-arms the DE timing (panel_de_timing_
     * enable). On a hot reload the DE clock was gated OFF/ON above, which
     * drops the UI-layer buffer association programmed just before; the layer
     * must be re-associated AFTER the DE timing is back up, otherwise the DE
     * scans an unattached layer and the screen goes black even though the
     * panel itself came back (observed: repeated dispdbg set/reset leaves the
     * framebuffer permanently dark). Re-issue the layer config now so the DE
     * has a valid buffer before the next vblank.
     */
    aicfb_update_layer(fbi);
}

static void dispdbg_pattern(struct aicfb_info *fbi, int id)
{
    u32 w = fbi->width;
    u32 h = fbi->height;
    u32 stride = fbi->stride;
    u32 frames = fbi->fb_size / (stride * h);
    u32 buf, f, i, j;
    u8 *base;

    if (!w || !h || !stride)
        return;
    if (!frames)
        frames = 1;

    /*
     * The DE may be scanning any of the AIC_FB_DRAW_BUF_NUM back buffers (the
     * driver pans fb_start + fb_size*buf_id), and dispdbg_pattern used to fill
     * only buf 0.  If the DE was on another buf the pattern never became
     * visible and the screen kept showing that other buf's stale frame --
     * which read as an "alternating two-frame" effect.  Fill every draw buffer
     * so a single static pattern is on screen regardless of which buf the DE
     * scans.
     */
    for (buf = 0; buf < AIC_FB_DRAW_BUF_NUM; buf++) {
        for (f = 0; f < frames; f++) {
            base = (u8 *)fbi->fb_start + buf * fbi->fb_size + f * stride * h;

            for (i = 0; i < h; i++) {
                u8 *p = base + i * stride;

                for (j = 0; j < w; j++) {
                    u8 r = 0, g = 0, b = 0;
                    u32 k;

                    switch (id) {
                    case 0:
                        k = (i / 100 + j / 100) % 5;
                        if (k == 0) b = 255;
                        else if (k == 1) g = 255;
                        else if (k == 2) r = 255;
                        else if (k == 4) r = g = b = 255;
                        break;
                    case 1: r = g = b = 255; break;
                    case 2: r = g = b = 0; break;
                    case 3: r = 255; break;
                    case 4: g = 255; break;
                    case 5: b = 255; break;
                    case 6: r = g = b = (u8)(j * 255 / w); break;
                    default: r = g = b = (u8)(i * 255 / h); break;
                    }
                    p[j * 3 + 0] = r;
                    p[j * 3 + 1] = g;
                    p[j * 3 + 2] = b;
                }
            }
        }
    }
    aicos_dcache_clean_range((unsigned long *)fbi->fb_start,
                             fbi->fb_size * AIC_FB_DRAW_BUF_NUM);
    pr_info("dispdbg: pattern %d filled (%ux%u, %u frame buf x%u draw buf)\n",
            id, w, h, frames, AIC_FB_DRAW_BUF_NUM);
}

static void cmd_dispdbg(int argc, char **argv)
{
    struct aicfb_info *fbi = aicfb_get_drvdata();
    struct display_timing *t;
    struct panel_dsi *dsi;

    if (!fbi || !fbi->panel || !fbi->panel->timings) {
        pr_err("dispdbg: fb not ready\n");
        return;
    }
    t = fbi->panel->timings;
    dsi = fbi->panel->dsi;

    if (argc < 2) {
        pr_info("%s", dispdbg_usage);
        return;
    }

    if (!strcmp(argv[1], "info")) {
        u32 htotal = t->hactive + t->hfront_porch + t->hback_porch + t->hsync_len;
        u32 vtotal = t->vactive + t->vfront_porch + t->vback_porch + t->vsync_len;
        u32 fps100 = htotal && vtotal ? (u32)((u64)t->pixelclock * 100 / htotal / vtotal) : 0;

        pr_info("dispdbg: active %ux%u pclk %u Hz\n", t->hactive, t->vactive, t->pixelclock);
        pr_info("dispdbg: hfp %u hbp %u hsync %u | vfp %u vbp %u vsync %u\n",
                t->hfront_porch, t->hback_porch, t->hsync_len,
                t->vfront_porch, t->vback_porch, t->vsync_len);
        pr_info("dispdbg: htotal %u vtotal %u fps %u.%02u\n", htotal, vtotal,
                fps100 / 100, fps100 % 100);
        if (dsi)
            pr_info("dispdbg: dsi lane %u assign 0x%03x polrs 0x%03x fmt %d mode %d\n",
                    dsi->lane_num, dsi->ln_assign, dsi->ln_polrs, dsi->format, dsi->mode);
        pr_info("dispdbg: fb %ux%u stride %u bpp %u size %u\n",
                fbi->width, fbi->height, fbi->stride, fbi->bits_per_pixel, fbi->fb_size);
    } else if (!strcmp(argv[1], "set") && argc == 4 && dsi) {
        u32 v = strtoul(argv[3], NULL, 0);
        u32 htotal = t->hactive + t->hfront_porch + t->hback_porch + t->hsync_len;
        u32 vtotal = t->vactive + t->vfront_porch + t->vback_porch + t->vsync_len;

        if (!strcmp(argv[2], "pclk"))
            t->pixelclock = v;
        else if (!strcmp(argv[2], "fps"))
            t->pixelclock = htotal * vtotal * v;
        else if (!strcmp(argv[2], "hfp"))
            t->hfront_porch = v;
        else if (!strcmp(argv[2], "hbp"))
            t->hback_porch = v;
        else if (!strcmp(argv[2], "hsync"))
            t->hsync_len = v;
        else if (!strcmp(argv[2], "vfp"))
            t->vfront_porch = v;
        else if (!strcmp(argv[2], "vbp"))
            t->vback_porch = v;
        else if (!strcmp(argv[2], "vsync"))
            t->vsync_len = v;
        else if (!strcmp(argv[2], "lane"))
            dsi->lane_num = v;
        else if (!strcmp(argv[2], "assign"))
            dsi->ln_assign = v;
        else if (!strcmp(argv[2], "polrs"))
            dsi->ln_polrs = v;
        else if (!strcmp(argv[2], "fmt"))
            dsi->format = v;
        else {
            pr_info("%s", dispdbg_usage);
            return;
        }
        pr_info("dispdbg: %s -> %u, re-init panel\n", argv[2], v);
        dispdbg_reload(fbi);
    } else if (!strcmp(argv[1], "res") && argc == 4) {
        /*
         * Change the active resolution at runtime to test whether the panel
         * is scanned in a different orientation than assumed (440x1920 and
         * 1920x440 hold the same pixel count, so the framebuffer budget is
         * unchanged for a swap). The DE scans hactive x vactive straight out
         * of the framebuffer, so a larger resolution than what was allocated
         * at probe time would read past the buffer; reject it.
         */
        static u32 fb_budget;
        u32 w = strtoul(argv[2], NULL, 0);
        u32 h = strtoul(argv[3], NULL, 0);
        u32 stride, need, htotal, vtotal;

        if (!w || !h) {
            pr_info("%s", dispdbg_usage);
            return;
        }
        stride = (w * fbi->bits_per_pixel / 8 + 7) & ~7u;
        need = stride * h;
        if (!fb_budget)
            fb_budget = fbi->fb_size;   /* bytes allocated for one frame */
        if (need > fb_budget) {
            pr_err("dispdbg: %ux%u needs %u bytes, framebuffer holds %u\n",
                   w, h, need, fb_budget);
            return;
        }

        t->hactive = w;
        t->vactive = h;
        /* Keep 60 Hz so the DSI lane rate stays in a sane range; override
         * afterwards with "set fps" or "set pclk" if needed. */
        htotal = w + t->hfront_porch + t->hback_porch + t->hsync_len;
        vtotal = h + t->vfront_porch + t->vback_porch + t->vsync_len;
        t->pixelclock = htotal * vtotal * 60;

        /* Refresh width/height/stride/fb_size from the new active size so the
         * test patterns and the DE layer match what the DE now scans. */
        aicfb_fb_info_setup(fbi);

        pr_info("dispdbg: res -> %ux%u stride %u pclk %u\n",
                fbi->width, fbi->height, fbi->stride, t->pixelclock);
        /* dispdbg_reload() now reprograms the layer geometry itself, after
         * aicfb_fb_info_setup() above refreshed width/height/stride. */
        dispdbg_reload(fbi);
    } else if (!strcmp(argv[1], "cmd") && argc >= 3) {
        /*
         * Send an arbitrary DCS command at runtime so panel assumptions can
         * be probed without reflashing, e.g. COLMOD (0x3A) to declare the
         * pixel format, or MADCTL (0x36) to change the scan direction.
         */
        u8 payload[16];
        int n = argc - 2;
        u32 dt;
        int i;

        if (n > (int)sizeof(payload)) {
            pr_err("dispdbg: too many bytes (max %u)\n", (u32)sizeof(payload));
            return;
        }
        if (!fbi->di || !fbi->di->di_funcs->send_cmd) {
            pr_err("dispdbg: interface has no send_cmd\n");
            return;
        }
        for (i = 0; i < n; i++)
            payload[i] = (u8)strtoul(argv[2 + i], NULL, 0);

        /* Pick the DCS packet type by payload length, as panel_dsi does. */
        if (n == 1)
            dt = MIPI_DSI_DCS_SHORT_WRITE;
        else if (n == 2)
            dt = MIPI_DSI_DCS_SHORT_WRITE_PARAM;
        else
            dt = MIPI_DSI_DCS_LONG_WRITE;

        /*
         * aic_dsi_send_cmd() writes the packet without touching the video
         * mode configuration. The panel driver only reaches the panel
         * because panel_dsi_send_perpare() first switches the controller to
         * command mode (which also enables DCS long write), and
         * panel_dsi_setup_realmode() switches back afterwards. Mirror that
         * here, otherwise the command is silently dropped in video mode.
         */
        if (fbi->di->di_funcs->set_videomode)
            fbi->di->di_funcs->set_videomode(t, 0);

        fbi->di->di_funcs->send_cmd(dt, 0, payload, (u32)n);

        if (fbi->di->di_funcs->set_videomode)
            fbi->di->di_funcs->set_videomode(t, 1);

        pr_info("dispdbg: sent DCS dt 0x%02x len %d:", dt, n);
        for (i = 0; i < n; i++)
            pr_info(" %02x", payload[i]);
        pr_info("\n");
    } else if (!strcmp(argv[1], "read") && argc == 3) {
        u32 reg = strtoul(argv[2], NULL, 0);
        int r;

        if (!fbi->di || !fbi->di->di_funcs->read_cmd) {
            pr_err("dispdbg: interface has no read_cmd\n");
            return;
        }
        r = fbi->di->di_funcs->read_cmd(reg);
        /*
         * aic_dsi_read_cmd() now pops the DSI RX FIFO (RX_HDR + up to two
         * 32-bit words) instead of returning the TX DSI_GEN_PD_CFG, so r is
         * the panel's 24-bit reply for GET_DISPLAY_ID-style reads.
         */
        pr_info("dispdbg: read 0x%02x -> 0x%06x (RX FIFO reply)\n",
                reg, (u32)r & 0xffffff);
    } else if (!strcmp(argv[1], "pattern") && argc == 3) {
        dispdbg_pattern(fbi, (int)strtoul(argv[2], NULL, 0));
    } else if (!strcmp(argv[1], "reset")) {
        dispdbg_reload(fbi);
    } else {
        pr_info("%s", dispdbg_usage);
    }
}
MSH_CMD_EXPORT_ALIAS(cmd_dispdbg, dispdbg, display bring-up: info/set/read/pattern/reset);
#endif

