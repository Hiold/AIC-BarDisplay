/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal application entry for the d133-boe1126-cdc panel bring-up image.
 *
 * Everything this image needs is driven by auto-init exports, so main() only
 * has to stay out of the way:
 *
 *   INIT_DEVICE_EXPORT(aicfb_probe)        -> fb/DE/DSI + panel init, paints
 *                                             the 5-color block test pattern
 *                                             and registers `dispdbg`
 *   INIT_DEVICE_EXPORT(cdc_console_register) -> "vcom" device + console bind
 *   INIT_APP_EXPORT(cdc_console_usb_init)  -> CherryUSB CDC-ACM controller up
 *
 * After USB enumeration the host sees a standard COM port; run `dispdbg` there
 * to tune the panel timing (see doc/未知屏调试指南-d133-boe1126.md).
 */

#include <rtthread.h>

int main(void)
{
    rt_kprintf("\n");
    rt_kprintf("=== D133 BOE1126 panel bring-up image ===\n");
    rt_kprintf("console: USB CDC-ACM (vcom)\n");
    rt_kprintf("type 'dispdbg' for panel timing debug commands\n");
    rt_kprintf("\n");
    return 0;
}
