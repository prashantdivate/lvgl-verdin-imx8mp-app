#include "ui.h"
#include "lvgl/lvgl.h"

#include <stdio.h>
#include <stdlib.h>

/* Keep your SquareLine export compiled exactly like bg-overlay does */
#include "sq_ui/sq_ui.h"
#include "sq_ui/screens/ui_Splash.h"
#include "sq_ui/screens/ui_Clock.h"

#include "sq_ui/sq_ui_helpers.c"
#include "sq_ui/components/ui_comp.c"
#include "sq_ui/components/ui_comp_alarm_comp.c"
#include "sq_ui/components/ui_comp_clock_dot.c"
#include "sq_ui/components/ui_comp_hook.c"
#include "sq_ui/components/ui_comp_scrolldots.c"
#include "sq_ui/components/ui_comp_small_label.c"
#include "sq_ui/screens/ui_Splash.c"
#include "sq_ui/screens/ui_Clock.c"
#include "sq_ui/fonts/ui_font_Number.c"
#include "sq_ui/images/ui_img_mad_logo_png.c"
#include "sq_ui/sq_ui.c"

/* Adjust these for your image */
#define CMD_RESTART_CHROMIUM "systemctl restart chromium"
#define CMD_REBOOT_DEVICE    "reboot"
#define CMD_SSH_ON           "systemctl start sshd"
#define CMD_SSH_OFF          "systemctl stop sshd"

#define COL_BG      lv_color_hex(0xF6F5F4)
#define COL_TEXT    lv_color_hex(0x30383C)
#define COL_BORDER  lv_color_hex(0xD5CDC7)
#define COL_BTN     lv_color_hex(0xFAF9F7)

static lv_obj_t *recovery_root;
static lv_obj_t *status_label;

static void set_status(const char *txt)
{
    if(status_label) lv_label_set_text(status_label, txt);
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

static void close_menu(void)
{
    if(recovery_root) lv_obj_add_flag(recovery_root, LV_OBJ_FLAG_HIDDEN);
}

static void close_menu_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    close_menu();
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

static void key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);

    if(key == LV_KEY_ESC) {
        close_menu();
    }
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *txt, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 260, 60);

    lv_obj_set_style_bg_color(btn, COL_BTN, 0);
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

static void recovery_menu_create(void)
{
    recovery_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(recovery_root);
    lv_obj_set_size(recovery_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(recovery_root, COL_BG, 0);
    lv_obj_set_style_bg_opa(recovery_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(recovery_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(recovery_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(recovery_root, key_cb, LV_EVENT_KEY, NULL);

    lv_obj_t *title = lv_label_create(recovery_root);
    lv_label_set_text(title, "Recovery Menu");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 105);

    lv_obj_t *subtitle = lv_label_create(recovery_root);
    lv_label_set_text(subtitle, "Tap close or press Esc to exit");
    lv_obj_set_style_text_color(subtitle, COL_TEXT, 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 18);

    lv_obj_t *ssh_label = lv_label_create(recovery_root);
    lv_label_set_text(ssh_label, "SSH Access");
    lv_obj_set_style_text_color(ssh_label, COL_TEXT, 0);
    lv_obj_set_style_text_font(ssh_label, &lv_font_montserrat_14, 0);
    lv_obj_align(ssh_label, LV_ALIGN_TOP_LEFT, 153, 248);

    lv_obj_t *ssh_sw = lv_switch_create(recovery_root);
    lv_obj_set_size(ssh_sw, 50, 60);
    lv_obj_align(ssh_sw, LV_ALIGN_TOP_LEFT, 353, 223);
    lv_obj_add_event_cb(ssh_sw, ssh_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *b1 = make_button(recovery_root, "Restart Chromium", restart_chromium_cb);
    lv_obj_align(b1, LV_ALIGN_TOP_MID, 0, 303);

    lv_obj_t *b2 = make_button(recovery_root, "Reboot Device", reboot_device_cb);
    lv_obj_align(b2, LV_ALIGN_TOP_MID, 0, 383);

    lv_obj_t *b3 = make_button(recovery_root, "Close Menu", close_menu_cb);
    lv_obj_align(b3, LV_ALIGN_TOP_MID, 0, 463);

    status_label = lv_label_create(recovery_root);
    lv_label_set_text(status_label, "Ready");
    lv_obj_set_style_text_color(status_label, COL_TEXT, 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 568);

    lv_group_t *g = lv_group_create();
    lv_group_add_obj(g, recovery_root);
    lv_group_focus_obj(recovery_root);

    lv_indev_t *indev = NULL;
    while((indev = lv_indev_get_next(indev)) != NULL) {
        if(lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD ||
           lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
            lv_indev_set_group(indev, g);
        }
    }
}

void app_ui_init(void)
{
    ui_init();              /* keep bg-overlay SquareLine/startup code alive */
    recovery_menu_create(); /* recovery UI appears above it */
}

void splash_set_step(splash_step_t step)
{
    LV_UNUSED(step);
}

void splash_set_message(const char *msg)
{
    LV_UNUSED(msg);
}
