/**
 * @file lv_demo_ebike.h
 *
 */

#ifndef LV_DEMO_EBIKE_H
#define LV_DEMO_EBIKE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lvgl.h"

#if LV_USE_DEMO_EBIKE

/* NOTE: The upstream demo hard-requires LV_USE_BIDI and
 * LV_USE_ARABIC_PERSIAN_CHARS for Arabic text shaping.  The AIC-BarDisplay
 * LVGL v9 bundle does not ship lv_bidi.c, and this embedded build targets a
 * Simplified-Chinese dashboard, so those checks are dropped here.  Arabic
 * locale selection (if ever used) renders LTR without glyph substitution. */

/*********************
 *      DEFINES
 *********************/


/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create an ebike demo.
 * If LV_DEMO_EBIKE_PORTRAIT is 0: the supported resolution as are: 320x240... 480x320
 * If LV_DEMO_EBIKE_PORTRAIT is 1: the supported resolution as are: 480x600... 480x858
 */
void lv_demo_ebike(void);

/**
 * Create the ebike demo as a component embedded in `parent`.
 * @param parent    the parent container (e.g. a centered host on a canvas)
 * @return          the created root container that holds all ebike content
 */
lv_obj_t * lv_demo_ebike_create(lv_obj_t * parent);


/**********************
 * GLOBAL VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

#endif /*LV_USE_DEMO_EBIKE*/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_DEMO_EBIKE_H*/
