/*
 * ebike_assets.h - resident LVGL draw-buffer holders for the ebike UI images.
 *
 * The ebike demo references its imagery as script-generated C-array
 * descriptors (img_ebike_scale, ...).  On the embedded target
 * those large bitmaps would blow past the 8 MiB PSRAM budget if compiled into
 * rodata, so instead they live as LVGL `.bin` files (12-byte header + raw
 * pixels) under the resource tree and are loaded once into RAM by
 * uires_res_load() (SRAM for small icons, CMA/PSRAM for big backdrops).
 * Each ebike_asset("name") returns the resident lv_draw_buf_t* so the demo
 * can be patched to source images from it instead of the C-array symbols.
 */
#ifndef EBIKE_ASSETS_H
#define EBIKE_ASSETS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* Lazy-load (and cache) the image `<name>.bin` from the uires resource tree.
 * Returns the resident lv_draw_buf_t*, or NULL if the file is missing / cannot
 * fit in RAM (in which case the demo falls back to a safe empty image). */
lv_draw_buf_t * ebike_asset(const char * name);

#ifdef __cplusplus
}
#endif

#endif /* EBIKE_ASSETS_H */