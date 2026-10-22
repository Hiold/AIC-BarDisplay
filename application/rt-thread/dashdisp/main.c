/*
 * Copyright (c) 2026, 电摩仪表 固件应用入口
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * "整机只做仪表"：上电后直接以全屏 LVGL ARGB8888 渲染电摩仪表界面，
 * 不复用 USB 副屏 / 背景视频 / 传输解码 逻辑。
 *
 * 显示初始化复用 SDK 的 lv_port_disp_init()（内部绑定 AIC DE 图层与
 * framebuffer），应用层只负责创建仪表并把数据喂给 dash_* 接口。
 */

#include <rtthread.h>
#include "lvgl.h"
#include "dash_hmi.h"

/* 来自 packages/artinchip/lvgl-ui/lvgl_v9/lv_drivers/lv_port_disp.c */
extern void lv_port_disp_init(void);

static void dash_thread_entry(void *parameter)
{
    (void)parameter;

    lv_init();
    lv_tick_set_cb((lv_tick_get_cb_t)&rt_tick_get_millisecond);

    lv_port_disp_init();

    /* 整机仪表：占满 320x1280 屏，无叠加层、无背景视频 */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    dash_hmi_create(scr);
    dash_hmi_start_demo();

    rt_kprintf("dashdisp: 电摩仪表 UI running\n");

    while (1) {
        lv_timer_handler();
        rt_thread_mdelay(5);
    }
}

int main(void)
{
    rt_thread_t t = rt_thread_create("dashdisp", dash_thread_entry, RT_NULL,
                                     16384, 12, 10);
    if (t) {
        rt_thread_startup(t);
    } else {
        rt_kprintf("dashdisp: thread create failed\n");
    }
    return 0;
}