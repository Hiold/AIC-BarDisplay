/*
 * ebike_assets.c - resident LVGL draw-buffer holders for the ebike UI images.
 *
 * See ebike_assets.h.  Images are loaded once at first use via
 * uires_res_load() and kept for the UI's lifetime (they are not freed).  The
 * tiny helper keeps the look-up simple: a small open-addressing cache keyed by
 * the image name string.  A fixed public API (ebike_asset_img_bg / _scale /
 * _arrow_left / _arrow_right / _lamp / _clock / _global_bg) is used by the
 * demo .c patches so the demo only calls this module.
 */
#include <string.h>
#include <stdio.h>
#include "lvgl.h"
#include "../uires_res.h"
#include "ebike_assets.h"

/* Path prefix; the drive letter is stripped by uires_res_load(). */
#define EBIKE_ASSET_PATH "L:/uires/ebike/"

#define EBIKE_ASSET_CACHE 8

struct ebike_asset_slot {
    const char * name;          /* canonical asset name (string literal) */
    lv_draw_buf_t * buf;        /* resident loaded buffer, NULL until loaded */
};

static struct ebike_asset_slot s_slots[EBIKE_ASSET_CACHE];
static unsigned s_slot_count;

lv_draw_buf_t * ebike_asset(const char * name)
{
    unsigned i;
    char path[128];

    if(!name) return NULL;

    for(i = 0; i < s_slot_count; i++) {
        if(strcmp(s_slots[i].name, name) == 0) {
            return s_slots[i].buf;
        }
    }

    if(s_slot_count >= EBIKE_ASSET_CACHE) {
        printf("ebike: asset cache full (%s)\n", name);
        return NULL;
    }

    if(snprintf(path, sizeof(path), "%s%s.bin", EBIKE_ASSET_PATH, name) >=
       (int)sizeof(path))
        return NULL;

    s_slots[s_slot_count].name = name;
    s_slots[s_slot_count].buf = uires_res_load(path);
    printf("ebike: asset %s -> %s\n", name,
           s_slots[s_slot_count].buf ? "ok" : "MISSING");
    return s_slots[s_slot_count++].buf;
}