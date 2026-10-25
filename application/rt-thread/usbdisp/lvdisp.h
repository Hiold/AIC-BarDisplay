/* SPDX-License-Identifier: Apache-2.0 */
#ifndef USB_DISP_LVGL_H
#define USB_DISP_LVGL_H

/* Initialise LVGL and build the orientation / boundary test screen on the UI
 * layer. */
int udisp_lvgl_init(void);

/* Run the LVGL task loop forever. Never returns. */
void udisp_lvgl_run(void);

#endif