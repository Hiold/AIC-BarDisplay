/**
 * @file lv_demo_ebike.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_demo_ebike.h"
#if LV_USE_DEMO_EBIKE

#include "lv_demo_ebike_home.h"
#include "lv_demo_ebike_private.h"
#include "translations/lv_i18n.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void menu_delete_event_cb(lv_event_t * e);
static void language_observer_cb(lv_observer_t * observer, lv_subject_t * subject);

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t * main_cont;
#if LV_USE_LOTTIE
    #if LV_DEMO_EBIKE_PORTRAIT
        LV_DRAW_BUF_DEFINE_STATIC(lottie_ebike_draw_buf, 120, 120, LV_COLOR_FORMAT_ARGB8888);
    #else
        LV_DRAW_BUF_DEFINE_STATIC(lottie_ebike_draw_buf, 100, 100, LV_COLOR_FORMAT_ARGB8888);
    #endif
#endif

static lv_theme_t * theme_original;

/**********************
 *  GLOBAL VARIABLES
 **********************/

/*The language subject is globally available*/
lv_subject_t ebike_subject_language;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_demo_ebike(void)
{
    lv_demo_ebike_create(lv_screen_active());
}

/**
 * Create the ebike demo as a component inside `parent`.
 * All content (background, main content area, menu bar) is placed inside a
 * returned root container that fills `parent`, so the demo can be embedded
 * and centered on any canvas. The caller owns `parent` (layout, position).
 * @param parent    the parent container to embed the demo into
 * @return          the created root container (holds all ebike content)
 */
lv_obj_t * lv_demo_ebike_create(lv_obj_t * parent)
{
    lv_i18n_init(lv_i18n_language_pack);
    /*Default to Simplified Chinese*/
    lv_i18n_set_locale("zh");

#if LV_USE_LOTTIE
    LV_DRAW_BUF_INIT_STATIC(lottie_ebike_draw_buf);
#endif

    lv_subject_init_int(&ebike_subject_language, 1); /*1 = 简体中文 (zh)*/
    lv_subject_add_observer_obj(&ebike_subject_language, language_observer_cb, parent, NULL);
    lv_demo_ebike_home_init();

    /*Use the simple the to make styling simpler*/
    lv_display_t * display = lv_display_get_default();
    theme_original = lv_display_get_theme(display);
    lv_theme_t * theme = lv_theme_simple_init(display);
    lv_display_set_theme(display, theme);

    /* Root container: fills parent, holds background + main content + menu.
     * Strip any default container padding/border/radius so content exactly
     * fills the passed-in host. */
    lv_obj_t * root = lv_obj_create(parent);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(root, LV_OPA_0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_text_color(root, lv_color_white(), 0);
    lv_obj_set_flex_flow(root, LV_DEMO_EBIKE_PORTRAIT ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);

    /* 不使用页面背景图，也不加半透明白色底板：root 与 parent 均透明，
     * 中间带背景完全透出 DE 下层 VIDEO 图层的背景视频。
     * 画布 alpha 由 GE 透传（ui.c ui_rotate_canvas），透明像素真正露出视频。 */
    lv_obj_set_style_bg_opa(parent, LV_OPA_0, 0);

    /*Create a container for the main content*/
    main_cont = lv_obj_create(root);
    lv_obj_set_size(main_cont, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_grow(main_cont, 1);
    lv_obj_set_style_bg_opa(main_cont, 0, 0);
    /*The main content always exists. If it's deleted, the demo was deleted, so clean up*/
    lv_obj_add_event_cb(main_cont, menu_delete_event_cb, LV_EVENT_DELETE, NULL);

    lv_demo_ebike_home_create(main_cont);

    return root;
}

#if LV_USE_LOTTIE
lv_draw_buf_t * lv_demo_ebike_get_lottie_draw_buf(void)
{
    return &lottie_ebike_draw_buf;
}
#endif

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void menu_delete_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    lv_subject_deinit(&ebike_subject_language);
    lv_demo_ebike_home_deinit();

    lv_display_set_theme(lv_display_get_default(), theme_original);
}

static void language_observer_cb(lv_observer_t * observer, lv_subject_t * subject)
{
    LV_UNUSED(observer);

    static uint32_t lang_current = 1; /*Match the default zh locale*/
    uint32_t lang_new = lv_subject_get_int(subject);
    if(lang_new != lang_current) {
        lang_current = lang_new;
        switch(lang_current) {
            case 0:
                lv_i18n_set_locale("en");
                break;
            case 1:
                lv_i18n_set_locale("zh");
                break;
            case 2:
                lv_i18n_set_locale("ar");
                break;
        }
        lv_obj_clean(main_cont);
    }
}

#endif /*LV_USE_DEMO_EBIKE*/
