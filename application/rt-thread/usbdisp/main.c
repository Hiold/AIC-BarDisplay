/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <string.h>
#include <rtthread.h>
#include <dfs_fs.h>
#include "usbdisp.h"

#ifdef UDISP_USE_LVGL_TEST
#include "lvdisp.h"
#endif

#define UDISP_PATTERN_PATH "/rodata/orientation_test.jpg"

int main(void)
{
    int retries;
    for (retries = 0; retries < 200; retries++) {
        struct dfs_filesystem *fs = dfs_filesystem_lookup("/rodata");
        if (fs && fs->path && !strcmp(fs->path, "/rodata")) break;
        aicos_msleep(10);
    }
    if (retries == 200) { printf("usbdisp: assets mount failed\n"); return -1; }

#ifdef UDISP_USE_LVGL_TEST
    if (udisp_lvgl_init()) { printf("usbdisp: LVGL test screen init failed\n"); return -1; }
    udisp_lvgl_run();
    return 0;
#else
    if (udisp_fb_init()) { printf("usbdisp: framebuffer init failed\n"); return -1; }
    if (udisp_show_jpeg(UDISP_PATTERN_PATH))
        printf("usbdisp: orientation pattern not shown\n");
    while (1)
        aicos_msleep(1000);
    return 0;
#endif
}