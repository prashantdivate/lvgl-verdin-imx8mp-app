#include "ui.h"
#include "lvgl/lvgl.h"

#include <stdlib.h>

#define CMD_RESTART_CHROMIUM "sudo systemctl restart chromium"
#define CMD_REBOOT_DEVICE    "sudo reboot"
#define CMD_SSH_ON           "sudo systemctl start sshd.socket"
#define CMD_SSH_OFF          "sudo systemctl stop sshd.socket"

#define COL_BG      lv_color_hex(0xF6F5F4)
#define COL_TEXT    lv_color_hex(0x30383C)
#define COL_BORDER  lv_color_hex(0xD5CDC7)
#define COL_BTN     lv_color_hex(0xFAF9F7)

#define CONTENT_W   300
#define BUTTON_W    300
#define BUTTON_H    60
#define SSH_ROW_H   60

static lv_obj_t *status_label;

static void set_status(const char *txt)
{
    if(status_label && txt) {
        lv_label_set_text(status_label, txt);
    }
}

static void run_cmd(const char *cmd, const char *status)
{
    set_status(status);
    lv_refr_now(NULL);

    if(cmd && cmd[0]) {
        system(cmd);
    }
}

static void restart_chromium_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    run_cmd(CMD_RESTART_CHROMIUM, "Restarting Chromium...");
}

static void reboot_device_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    run_cmd(CMD_REBOOT_DEVICE, "Rebooting Device...");
}

static void close_menu_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_add_flag(lv_scr_act(), LV_OBJ_FLAG_HIDDEN);
}

static void ssh_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);

    if(lv_obj_has_state(sw, LV_STATE_CHECKED)) {
        run_cmd(CMD_SSH_ON, "SSH Enabled");
    } else {
        run_cmd(CMD_SSH_OFF, "SSH Disabled");
    }
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *txt, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, BUTTON_W, BUTTON_H);

    lv_obj_set_style_bg_color(btn, COL_BTN, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, COL_BORDER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 5, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);

    return btn;
}

static lv_obj_t *make_ssh_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, CONTENT_W, SSH_ROW_H);

    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, "SSH Access");
    lv_obj_set_style_text_color(label, COL_TEXT, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 60, 34);
    lv_obj_add_event_cb(sw, ssh_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return row;
}

static void recovery_menu_create(void)
{
    lv_obj_t *scr = lv_scr_act();

    lv_obj_clean(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Recovery Menu");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 115);

    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "Tap close or press Esc to exit");
    lv_obj_set_style_text_color(subtitle, COL_TEXT, 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 14);

    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, CONTENT_W, LV_SIZE_CONTENT);
    lv_obj_align(content, LV_ALIGN_CENTER, 0, 20);

    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 20, 0);

    make_ssh_row(content);
    make_button(content, "Restart Chromium", restart_chromium_cb);
    make_button(content, "Reboot Device", reboot_device_cb);
    make_button(content, "Close Menu", close_menu_cb);

    status_label = lv_label_create(content);
    lv_label_set_text(status_label, "Ready");
    lv_obj_set_style_text_color(status_label, COL_TEXT, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
}

void app_ui_init(void)
{
    recovery_menu_create();
}
