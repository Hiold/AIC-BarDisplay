/*
 * lvgl_compat.c - LVGL v9 observer API bridge for the ebike demo.
 *
 * The AIC-BarDisplay LVGL v9 bundle ships an older observer implementation
 * that lacks two functions the (newer) lv_demo_ebike demo calls:
 *   - lv_observer_get_user_data()
 *   - lv_subject_deinit()
 *
 * Both can be provided against the older API's data structures directly:
 * lv_observer_t already stores `user_data`, and lv_subject_t already keeps a
 * linked list (`subs_ll`) of observers plus a `type` field we can mark INVALID.
 * This file supplies minimal equivalent implementations so the demo links and
 * runs unchanged on the embedded target.
 */
#include "lvgl.h"

void * lv_observer_get_user_data(lv_observer_t * observer)
{
    if(observer == NULL) return NULL;
    return observer->user_data;
}

void lv_subject_deinit(lv_subject_t * subject)
{
    lv_observer_t * observer, * observer_next;
    if(subject == NULL) return;

    /* Free every subscribed observer (honouring auto_free_user_data), then
     * clear the list, mirroring the newer lv_subject_deinit(). */
    observer = _lv_ll_get_head(&subject->subs_ll);
    while(observer != NULL) {
        observer_next = _lv_ll_get_next(&subject->subs_ll, observer);
        if(observer->auto_free_user_data && observer->user_data) {
            lv_free(observer->user_data);
        }
        lv_free(observer);
        observer = observer_next;
    }
    _lv_ll_clear(&subject->subs_ll);
    subject->type = LV_SUBJECT_TYPE_INVALID;
}