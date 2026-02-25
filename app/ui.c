#include "ui.h"
#include "lvgl/lvgl.h"
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

/* =========================
 *  Screen look (tune)
 * ========================= */
#define COL_TEXT        lv_color_hex(0xE7EEF8)
#define COL_MUTED       lv_color_hex(0x9DB2CC)
#define COL_PANEL       lv_color_hex(0x0E1624)
#define COL_DIVIDER     lv_color_hex(0x2A3B55)
#define COL_RING_DARK   lv_color_hex(0x1D2A3D)

#define COL_RING_BLUE   lv_color_hex(0x2FA8FF)
#define COL_RING_GREEN  lv_color_hex(0x3DFFB3)

/* =========================
 *  LVGL v9-safe opacities
 *  (0..255)
 * ========================= */
#define OPA_05  13
#define OPA_10  26
#define OPA_12  31
#define OPA_18  46
#define OPA_20  51
#define OPA_25  64
#define OPA_30  77
#define OPA_35  89
#define OPA_40  102
#define OPA_45  115
#define OPA_50  128
#define OPA_55  140
#define OPA_60  153
#define OPA_70  179
#define OPA_75  191
#define OPA_80  204
#define OPA_90  230

/* If M_PI not defined by toolchain */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------- Assets (converted with LVGL v9 converter) ------- */
extern const lv_image_dsc_t mad_logo;     /* your existing mad_logo.c */
extern const lv_image_dsc_t boot_bg;      /* bg image (full screen) */
extern const lv_image_dsc_t icon_system;  /* gear */
extern const lv_image_dsc_t icon_services;/* cloud */
extern const lv_image_dsc_t icon_docker;  /* docker */

/* =========================
 *  Static UI handles
 * ========================= */
static lv_obj_t *g_step_cards[3] = {0};
static lv_obj_t *g_step_labels[3] = {0};
static lv_obj_t *g_message = NULL;

/* Loader objects */
static lv_obj_t *g_spinner = NULL;

/* =========================
 * Helpers
 * ========================= */
static void set_card_active_obj(uint32_t idx, bool active)
{
    if(idx >= 3) return;
    lv_obj_t *card = g_step_cards[idx];
    lv_obj_t *lbl = g_step_labels[idx];
    if(!card || !lbl) return;

    lv_obj_set_style_bg_opa(card, active ? OPA_25 : LV_OPA_0, 0);
    lv_obj_set_style_border_opa(card, active ? OPA_55 : LV_OPA_0, 0);
    lv_obj_set_style_text_color(lbl, active ? COL_TEXT : COL_MUTED, 0);
}

/* Create one status item (icon + text) */
static lv_obj_t *create_status_item(lv_obj_t *parent,
                                    uint32_t idx,
                                    const lv_image_dsc_t *icon_dsc,
                                    const char *text)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);

    lv_obj_set_size(card, LV_PCT(33), LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_gap(card, 10, 0);

    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_0, 0);

    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, COL_DIVIDER, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_0, 0);

    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon = lv_image_create(card);
    lv_image_set_src(icon, icon_dsc);
    lv_obj_set_style_opa(icon, OPA_90, 0);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, COL_MUTED, 0);
    lv_obj_set_style_text_opa(lbl, OPA_90, 0);

    g_step_cards[idx] = card;
    g_step_labels[idx] = lbl;
    return card;
}

/* -------------------------
 *  Loader (LVGL spinner)
 * ------------------------- */
static void spinner_create(lv_obj_t *parent, int32_t size_px)
{
    g_spinner = lv_spinner_create(parent);
    lv_obj_set_size(g_spinner, size_px, size_px);

    /* 1 revolution duration (ms) + arc sweep (degrees) */
    lv_spinner_set_anim_params(g_spinner, 1100, 270);

    /* Background ring */
    lv_obj_set_style_arc_width(g_spinner, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_spinner, COL_RING_DARK, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(g_spinner, OPA_55, LV_PART_MAIN);

    /* Foreground arc */
    lv_obj_set_style_arc_width(g_spinner, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g_spinner, COL_RING_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(g_spinner, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g_spinner, true, LV_PART_INDICATOR);

    /* Subtle glow */
    lv_obj_set_style_shadow_width(g_spinner, 28, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(g_spinner, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(g_spinner, COL_RING_GREEN, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(g_spinner, OPA_30, LV_PART_MAIN);
}

/* =========================
 *  Main UI
 * ========================= */
void app_ui_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Background image */
    lv_obj_t *bg_img = lv_image_create(scr);
    lv_image_set_src(bg_img, &boot_bg);
    lv_obj_align(bg_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_background(bg_img);

    /* Dark vignette overlay */
    lv_obj_t *vign = lv_obj_create(scr);
    lv_obj_remove_style_all(vign);
    lv_obj_set_size(vign, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(vign, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(vign, OPA_25, 0);
    lv_obj_move_foreground(vign);

    /* Outer frame */
    lv_obj_t *frame = lv_obj_create(scr);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, LV_PCT(96), LV_PCT(92));
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_0, 0);
    lv_obj_set_style_border_width(frame, 3, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0x93A8C6), 0);
    lv_obj_set_style_border_opa(frame, OPA_30, 0);
    lv_obj_set_style_radius(frame, 14, 0);

    /* Inner frame */
    lv_obj_t *frame2 = lv_obj_create(scr);
    lv_obj_remove_style_all(frame2);
    lv_obj_set_size(frame2, LV_PCT(94), LV_PCT(90));
    lv_obj_align(frame2, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(frame2, LV_OPA_0, 0);
    lv_obj_set_style_border_width(frame2, 2, 0);
    lv_obj_set_style_border_color(frame2, lv_color_hex(0x93A8C6), 0);
    lv_obj_set_style_border_opa(frame2, OPA_18, 0);
    lv_obj_set_style_radius(frame2, 12, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Starting Up...");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_set_style_text_opa(title, OPA_90, 0);

#if LV_FONT_MONTSERRAT_36
    lv_obj_set_style_text_font(title, &lv_font_montserrat_36, 0);
#elif LV_FONT_MONTSERRAT_34
    lv_obj_set_style_text_font(title, &lv_font_montserrat_34, 0);
#elif LV_FONT_MONTSERRAT_32
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
#elif LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
#else
    lv_obj_set_style_text_font(title, LV_FONT_DEFAULT, 0);
#endif

    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 56);

    /* Spinner */
    spinner_create(scr, 180);
    lv_obj_align(g_spinner, LV_ALIGN_TOP_MID, 0, 140);

    /* Logo */
    lv_obj_t *logo = lv_image_create(scr);
    lv_image_set_src(logo, &mad_logo);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 355);

    /* Subtitle */
    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "Embedded Devices");
    lv_obj_set_style_text_color(sub, COL_TEXT, 0);
    lv_obj_set_style_text_opa(sub, OPA_75, 0);
    lv_obj_align_to(sub, logo, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    /* Message */
    g_message = lv_label_create(scr);
    lv_label_set_text(g_message, "Preparing system...");
    lv_obj_set_style_text_color(g_message, COL_MUTED, 0);
    lv_obj_set_style_text_opa(g_message, OPA_80, 0);
    lv_obj_align_to(g_message, sub, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    /* Bottom bar */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(90), 86);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -42);

    lv_obj_set_style_radius(bar, 16, 0);
    lv_obj_set_style_bg_color(bar, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(bar, OPA_35, 0);

    lv_obj_set_style_border_width(bar, 2, 0);
    lv_obj_set_style_border_color(bar, COL_DIVIDER, 0);
    lv_obj_set_style_border_opa(bar, OPA_40, 0);

    lv_obj_set_style_shadow_width(bar, 24, 0);
    lv_obj_set_style_shadow_color(bar, lv_color_hex(0x20344F), 0);
    lv_obj_set_style_shadow_opa(bar, OPA_50, 0);

    lv_obj_set_style_pad_all(bar, 10, 0);
    lv_obj_set_style_pad_gap(bar, 10, 0);

    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    /* Build bar contents in correct visual order: item | divider | item | divider | item */
    create_status_item(bar, 0, &icon_system, "Loading System...");

    lv_obj_t *div1 = lv_obj_create(bar);
    lv_obj_remove_style_all(div1);
    lv_obj_set_size(div1, 2, 46);
    lv_obj_set_style_bg_color(div1, lv_color_hex(0x7C90AE), 0);
    lv_obj_set_style_bg_opa(div1, OPA_12, 0);

    create_status_item(bar, 1, &icon_services, "Starting Services...");

    lv_obj_t *div2 = lv_obj_create(bar);
    lv_obj_remove_style_all(div2);
    lv_obj_set_size(div2, 2, 46);
    lv_obj_set_style_bg_color(div2, lv_color_hex(0x7C90AE), 0);
    lv_obj_set_style_bg_opa(div2, OPA_12, 0);

    create_status_item(bar, 2, &icon_docker, "Initializing Docker...");

    splash_set_step(SPLASH_STEP_SYSTEM);
}

void splash_set_step(splash_step_t step)
{
    /* reset */
    for(uint32_t i = 0; i < 3; i++) {
        set_card_active_obj(i, false);
    }

    if(step == SPLASH_STEP_DONE) {
        if(g_message) lv_label_set_text(g_message, "Ready.");
        if(g_spinner) lv_obj_add_flag(g_spinner, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if(g_spinner) lv_obj_clear_flag(g_spinner, LV_OBJ_FLAG_HIDDEN);

    if(step <= SPLASH_STEP_DOCKER) {
        set_card_active_obj((uint32_t)step, true);
        if(g_message) {
            switch(step) {
                case SPLASH_STEP_SYSTEM:   lv_label_set_text(g_message, "Preparing system...");   break;
                case SPLASH_STEP_SERVICES: lv_label_set_text(g_message, "Starting services..."); break;
                case SPLASH_STEP_DOCKER:   lv_label_set_text(g_message, "Loading containers..."); break;
                default: break;
            }
        }
    }
}

void splash_set_message(const char *msg)
{
    if(g_message && msg) lv_label_set_text(g_message, msg);
}
