#include "ui.h"
#include "lvgl/lvgl.h"

void app_ui_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Hello Verdin iMX8MP!\nLVGL app skeleton is running.");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);

    /* A simple button for sanity */
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 180, 52);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "OK");
    lv_obj_center(btn_lbl);
}
