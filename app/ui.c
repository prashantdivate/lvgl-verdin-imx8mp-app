#include "ui.h"
#include "lvgl/lvgl.h"
#include <stdio.h>
#include <string.h>

/* SquareLine public headers (your exported UI under app/sq_ui) */
#include "sq_ui/sq_ui.h"
#include "sq_ui/screens/ui_Splash.h"
#include "sq_ui/screens/ui_Clock.h"

/* Include SquareLine sources so your build still works unchanged */
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

#include "boot_tracker.h"

/* ---------- Local “production” widgets on top of ui_Clock ---------- */
static lv_obj_t *g_card = NULL;
static lv_obj_t *g_title = NULL;
static lv_obj_t *g_subtitle = NULL;
static lv_obj_t *g_status = NULL;
static lv_obj_t *g_pbar = NULL;
static lv_obj_t *g_pbar_label = NULL;

static splash_step_t g_last_step = SPLASH_STEP_SYSTEM;
static char g_last_msg[256] = "Preparing System …";
static int g_last_percent = 0;

static void ensure_overlay_ui(void)
{
    if (!ui_Clock) return;
    if (g_card) return;

    /* Background already set by SquareLine; create a centered card */
    g_card = lv_obj_create(ui_Clock);
    lv_obj_set_size(g_card, 700, 320);
    lv_obj_center(g_card);
    lv_obj_set_style_radius(g_card, 18, 0);
    lv_obj_set_style_bg_opa(g_card, LV_OPA_20, 0);
    lv_obj_set_style_border_width(g_card, 1, 0);
    lv_obj_set_style_border_opa(g_card, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(g_card, 20, 0);

    g_title = lv_label_create(g_card);
    lv_label_set_text(g_title, "System Starting");
    lv_obj_set_style_text_font(g_title, &lv_font_montserrat_28, 0);
    lv_obj_align(g_title, LV_ALIGN_TOP_LEFT, 0, 0);

    g_subtitle = lv_label_create(g_card);
    lv_label_set_text(g_subtitle, "Preparing device …");
    lv_obj_set_style_text_font(g_subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_opa(g_subtitle, LV_OPA_70, 0);
    lv_obj_align_to(g_subtitle, g_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    g_pbar = lv_bar_create(g_card);
    lv_obj_set_width(g_pbar, 660);
    lv_obj_set_height(g_pbar, 18);
    lv_obj_align(g_pbar, LV_ALIGN_TOP_LEFT, 0, 90);
    lv_bar_set_range(g_pbar, 0, 100);
    lv_bar_set_value(g_pbar, 0, LV_ANIM_OFF);

    g_pbar_label = lv_label_create(g_card);
    lv_label_set_text(g_pbar_label, "0%");
    lv_obj_set_style_text_font(g_pbar_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_opa(g_pbar_label, LV_OPA_80, 0);
    lv_obj_align_to(g_pbar_label, g_pbar, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    g_status = lv_label_create(g_card);
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_status, 660);
    lv_obj_set_style_text_font(g_status, &lv_font_montserrat_20, 0);
    lv_obj_align(g_status, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_label_set_text(g_status, g_last_msg);

    /* Hide SquareLine’s simple label/spinner if you want a cleaner look */
    if (ui_Date) lv_obj_add_flag(ui_Date, LV_OBJ_FLAG_HIDDEN);
    /* Keep spinner visible if you like; otherwise hide it */
    /* if (ui_Spinner1) lv_obj_add_flag(ui_Spinner1, LV_OBJ_FLAG_HIDDEN); */
}

static void apply_message_everywhere(const char *msg)
{
    if (!msg) return;

    snprintf(g_last_msg, sizeof(g_last_msg), "%s", msg);

    /* Update our production card on Clock */
    ensure_overlay_ui();
    if (g_status) lv_label_set_text(g_status, g_last_msg);

    /* Also update SquareLine labels (so you always see *something*) */
    if (ui_Date) lv_label_set_text(ui_Date, g_last_msg);
    if (ui_Startup_H1) lv_label_set_text(ui_Startup_H1, g_last_msg);
}

static void apply_percent(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    g_last_percent = percent;

    ensure_overlay_ui();
    if (g_pbar) lv_bar_set_value(g_pbar, percent, LV_ANIM_OFF);
    if (g_pbar_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", percent);
        lv_label_set_text(g_pbar_label, buf);
    }
}

void app_ui_init(void)
{
    ui_init();                 /* SquareLine init */
    if (ui_Clock) {
        lv_scr_load(ui_Clock); /* Use Clock as the real startup UI */
    }

    ensure_overlay_ui();
    apply_message_everywhere("Preparing device …");
    apply_percent(0);

    boot_tracking_start();
}

/* Public API */
void splash_set_step(splash_step_t step) { g_last_step = step; (void)g_last_step; }

void splash_set_message(const char *msg) { apply_message_everywhere(msg); }

void splash_set_overall_progress(int percent) { apply_percent(percent); }

void splash_set_import_progress(const char *component, int done_layers, int total_layers)
{
    char compbuf[96];
    if (!component || !component[0]) snprintf(compbuf, sizeof(compbuf), "System component");
    else snprintf(compbuf, sizeof(compbuf), "%s", component);

    int pct = 0;
    if (total_layers > 0 && done_layers >= 0) pct = (done_layers * 100) / total_layers;

    char line[256];
    if (total_layers > 0) {
        snprintf(line, sizeof(line),
                 "Preparing %s — %d%% (%d/%d)",
                 compbuf, pct, done_layers, total_layers);
    } else {
        snprintf(line, sizeof(line), "Preparing %s …", compbuf);
    }

    apply_message_everywhere(line);
}

/* wrappers */
void boot_tracking_start(void) { boot_tracker_start(); }
void boot_tracking_stop(void)  { boot_tracker_stop();  }
