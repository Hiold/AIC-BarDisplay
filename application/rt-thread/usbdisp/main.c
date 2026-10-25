/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <string.h>
#include <rtthread.h>
#include <dfs_fs.h>
#include "lvdisp.h"

int main(void)
{
    if (udisp_lvgl_init())
    {
        printf("usbdisp: LVGL test screen init failed\n");
        return -1;
    }
    udisp_lvgl_run();
    return 0;
}