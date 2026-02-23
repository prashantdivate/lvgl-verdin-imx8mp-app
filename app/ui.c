#include "ui.h"
#include "lvgl/lvgl.h"

/* =========================
 *  Branding colors (tune)
 * ========================= */
#define COL_BG          lv_color_hex(0x0B1220)  /* deep navy */
#define COL_PANEL       lv_color_hex(0x111B2E)  /* bottom bar */
#define COL_TEXT        lv_color_hex(0xE7EEF8)  /* near-white */
#define COL_MUTED       lv_color_hex(0x8FA3BF)  /* muted text */
#define COL_ACCENT_A    lv_color_hex(0x2FA8FF)  /* blue */
#define COL_ACCENT_B    lv_color_hex(0x3DFFB3)  /* green-ish */
#define COL_DIVIDER     lv_color_hex(0x22314D)  /* divider */

/* ======================================
 *  Logo: replace this with converted
 *  LVGL image descriptor (logo PNG)
 * ====================================== */
/*
 * How to add your real logo:
 * 1) Convert your PNG to LVGL C array using LVGL image converter
 * 2) Put resulting .c/.h under app/assets/ (or app/)
 * 3) Include the header here and set LOGO_SRC to &your_img_dsc
 *
 * Example:
 *   #include "logo_240.h"
 *   #define LOGO_SRC (&logo_240)
 */
//#define LOGO_SRC NULL
//#include "logo.h"
extern const lv_img_dsc_t mad_logo;
#define LOGO_SRC (&mad_logo)

/* =========================
 *  Static UI handles
 * ========================= */
static lv_obj_t *g_step_cards[3] = {0};
static lv_obj_t *g_step_labels[3] = {0};
static lv_obj_t *g_message = NULL;

/* =========================
 *  Helpers
 * ========================= */
static void set_card_active(uint32_t idx, bool active)
{
    if(idx >= 3 || g_step_cards[idx] == NULL) return;

    lv_obj_t *card = g_step_cards[idx];
    lv_obj_t *lbl  = g_step_labels[idx];

    lv_obj_set_style_bg_opa(card, active ? LV_OPA_30 : LV_OPA_0, 0);
    lv_obj_set_style_border_opa(card, active ? LV_OPA_40 : LV_OPA_0, 0);
    lv_obj_set_style_text_color(lbl, active ? COL_TEXT : COL_MUTED, 0);
}

/* Create one status item (icon + text) */
static lv_obj_t *create_status_item(lv_obj_t *parent, uint32_t idx, const char *symbol, const char *text)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, LV_PCT(33), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_0, 0);
    lv_obj_set_style_border_color(card, lv_color_white(), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_0, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_gap(card, 8, 0);

    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon = lv_label_create(card);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_color(icon, COL_MUTED, 0);
    lv_obj_set_style_text_font(icon, LV_FONT_DEFAULT, 0);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, COL_MUTED, 0);

    /* Save handles for step highlight */
    g_step_cards[idx]  = card;
    g_step_labels[idx] = lbl;

    return card;
}

void app_ui_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_style_all(scr);

    /* Background */
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Starting Up...");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_set_style_text_font(title, LV_FONT_DEFAULT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 36);

    /* Spinner (animated) */
    lv_obj_t *spinner = lv_spinner_create(scr, 1100 /*ms*/, 70 /*arc length*/);
    lv_obj_set_size(spinner, 160, 160);
    lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, 120);

    /* Spinner styling: subtle background arc + accent indicator */
    lv_obj_set_style_arc_width(spinner, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x1A2A45), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(spinner, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_set_style_arc_width(spinner, 12, LV_PART_INDICATOR);
    /* Use one accent (blue) but we’ll add a slight glow using shadow */
    lv_obj_set_style_arc_color(spinner, COL_ACCENT_A, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(spinner, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(spinner, 22, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_color(spinner, COL_ACCENT_A, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_opa(spinner, LV_OPA_40, LV_PART_INDICATOR);

    /* Logo */
    lv_obj_t *logo_cont = lv_obj_create(scr);
    lv_obj_remove_style_all(logo_cont);
    lv_obj_set_size(logo_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(logo_cont, LV_ALIGN_TOP_MID, 0, 310);
    lv_obj_set_style_pad_all(logo_cont, 0, 0);

    if(LOGO_SRC) {
        lv_obj_t *img = lv_image_create(logo_cont);
        lv_image_set_src(img, LOGO_SRC);
        lv_obj_center(img);
    } else {
        /* Fallback: text logo if image not yet wired */
        lv_obj_t *fallback = lv_label_create(logo_cont);
        lv_label_set_text(fallback, "MAD");
        lv_obj_set_style_text_color(fallback, lv_color_hex(0xFF2A2A), 0); /* red like your logo */
        lv_obj_set_style_text_font(fallback, LV_FONT_DEFAULT, 0);
        lv_obj_center(fallback);
    }

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "Embedded Devices");
    lv_obj_set_style_text_color(sub, COL_TEXT, 0);
    lv_obj_set_style_text_opa(sub, LV_OPA_80, 0);
    lv_obj_align_to(sub, logo_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    /* Optional message line (changes as steps progress) */
    g_message = lv_label_create(scr);
    lv_label_set_text(g_message, "Preparing system...");
    lv_obj_set_style_text_color(g_message, COL_MUTED, 0);
    lv_obj_align_to(g_message, sub, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);

    /* Bottom status bar */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(92), 74);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_set_style_radius(bar, 18, 0);
    lv_obj_set_style_bg_color(bar, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_80, 0);
    lv_obj_set_style_border_color(bar, COL_DIVIDER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_opa(bar, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(bar, 10, 0);
    lv_obj_set_style_pad_gap(bar, 10, 0);

    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Status items */
    create_status_item(bar, 0, LV_SYMBOL_SETTINGS, "Loading System...");
    create_status_item(bar, 1, LV_SYMBOL_UPLOAD,   "Starting Services...");
    create_status_item(bar, 2, LV_SYMBOL_REFRESH,  "Initializing Docker...");

    /* Default state: step 0 active */
    splash_set_step(SPLASH_STEP_SYSTEM);
}

void splash_set_step(splash_step_t step)
{
    /* Reset all */
    for(uint32_t i = 0; i < 3; i++) set_card_active(i, false);

    if(step == SPLASH_STEP_DONE) {
        /* Optional: show a “Ready” message */
        if(g_message) lv_label_set_text(g_message, "Ready.");
        return;
    }

    if(step <= SPLASH_STEP_DOCKER) {
        set_card_active((uint32_t)step, true);

        if(g_message) {
            switch(step) {
            case SPLASH_STEP_SYSTEM:   lv_label_set_text(g_message, "Preparing system..."); break;
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
