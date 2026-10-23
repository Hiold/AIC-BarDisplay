/* SPDX-License-Identifier: Apache-2.0 */
#ifndef USB_DISP_APP_H
#define USB_DISP_APP_H

#include <stdbool.h>
#include <stdint.h>
#include "aic_osal.h"
#include "mpp_fb.h"
#include "mpp_decoder.h"
#include "artinchip_fb.h"

#define UDISP_PRESENT_PERIOD_MS 17
#define UDISP_FRAME_LIMIT 300000U

extern struct aicfb_screeninfo udisp_screen;

/* Open the display framebuffer and power the panel on. */
int udisp_fb_init(void);

/* Decode the JPEG file at @path and present it full-screen via the VIDEO
 * layer. The frame is centered and cropped to fill the panel. */
int udisp_show_jpeg(const char *path);

#endif