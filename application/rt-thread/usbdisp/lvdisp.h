/* SPDX-License-Identifier: Apache-2.0 */
#ifndef USB_DISP_LVGL_H
#define USB_DISP_LVGL_H

/* Initialise LVGL and build the instrument-cluster demo UI on the UI layer
 * (attitude indicator, rear obstacle radar, acceleration trajectory). The
 * layout adapts to the logical canvas orientation: left/middle/right on a
 * landscape panel (rotation enabled), bottom/middle/top-right on a portrait
 * panel. Data is generated internally and animated. */
int udisp_lvgl_init(void);

/* Run the LVGL task loop forever. Never returns. */
void udisp_lvgl_run(void);

#endif