#include "ui.h"
#include "lvgl/lvgl.h"
#include <stdio.h>
#include <string.h>

/* ---------- Look & feel ---------- */
static lv_style_t st_screen;
static lv_style_t st_card;
static lv_style_t st_title;
static lv_style_t st_subtitle;
static lv_style_t st_label;
static lv_style_t st_dim;
static lv_style_t st_bar_bg;
static lv_style_t st_bar_ind;
static lv_style_t st_micro_bg;
static lv_style_t st_micro_ind;

typedef struct {
    char name[64];
    lv_obj_t *row;
    lv_obj_t *dot;
    lv_obj_t *txt;
    int state; /* 0=pending, 1=active, 2=ok */
} row_t;

#define MAX_ROWS 10
static row_t g_rows[MAX_ROWS];
static int g_row_count = 0;

/* ---------- Widgets ---------- */
static lv_obj_t *g_card;
static lv_obj_t *g_title;
static lv_obj_t *g_subtitle;
static lv_obj_t *g_overall;
static lv_obj_t *g_overall_txt;
static lv_obj_t *g_status_left;
static lv_obj_t *g_micro;
static lv_obj_t *g_status_right;
static lv_obj_t *g_list;
static lv_obj_t *g_spinner;

/* ---------- Helpers ---------- */
static int clampi(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi ? hi : v); }

static const char *friendly_image_prefix(const char *tag) {
    if(!tag || !tag[0]) return "System component";

    static char buf[128];
    snprintf(buf, sizeof(buf), "%s", tag);
    char *colon = strchr(buf, ':');
    if(colon) *colon = '\0';

    if(strcmp(buf, "chromium-imx8") == 0) return "Chromium";
    if(strcmp(buf, "lobby-panel-ems-db") == 0) return "Database";
    if(strcmp(buf, "lobby-panel-ems") == 0) return "Application";
    if(strcmp(buf, "weston-vivante") == 0) return "Display Server";
    return "System component";
}

static void styles_init(void) {
    lv_style_init(&st_screen);
    lv_style_set_bg_opa(&st_screen, LV_OPA_COVER);
    lv_style_set_bg_color(&st_screen, lv_color_hex(0x070C14));

    lv_style_init(&st_card);
    lv_style_set_radius(&st_card, 18);
    lv_style_set_bg_opa(&st_card, LV_OPA_40);
    lv_style_set_bg_color(&st_card, lv_color_hex(0xFFFFFF));
    lv_style_set_border_width(&st_card, 1);
    lv_style_set_border_opa(&st_card, LV_OPA_20);
    lv_style_set_border_color(&st_card, lv_color_hex(0xFFFFFF));
    lv_style_set_pad_all(&st_card, 22);
    lv_style_set_pad_row(&st_card, 14);
    lv_style_set_pad_column(&st_card, 14);

    lv_style_init(&st_title);
    lv_style_set_text_color(&st_title, lv_color_hex(0xF4F7FF));
    lv_style_set_text_font(&st_title, &lv_font_montserrat_28);

    lv_style_init(&st_subtitle);
    lv_style_set_text_color(&st_subtitle, lv_color_hex(0xF4F7FF));
    lv_style_set_text_opa(&st_subtitle, LV_OPA_70);
    lv_style_set_text_font(&st_subtitle, &lv_font_montserrat_16);

    lv_style_init(&st_label);
    lv_style_set_text_color(&st_label, lv_color_hex(0xEAF0FF));
    lv_style_set_text_font(&st_label, &lv_font_montserrat_16);

    lv_style_init(&st_dim);
    lv_style_set_text_color(&st_dim, lv_color_hex(0xF4F7FF));
    lv_style_set_text_opa(&st_dim, LV_OPA_60);
    lv_style_set_text_font(&st_dim, &lv_font_montserrat_14);

    lv_style_init(&st_bar_bg);
    lv_style_set_bg_opa(&st_bar_bg, LV_OPA_20);
    lv_style_set_bg_color(&st_bar_bg, lv_color_hex(0xFFFFFF));
    lv_style_set_radius(&st_bar_bg, 10);

    lv_style_init(&st_bar_ind);
    lv_style_set_bg_opa(&st_bar_ind, LV_OPA_COVER);
    lv_style_set_bg_color(&st_bar_ind, lv_color_hex(0x7AA7FF));
    lv_style_set_radius(&st_bar_ind, 10);

    lv_style_init(&st_micro_bg);
    lv_style_set_bg_opa(&st_micro_bg, LV_OPA_18);
    lv_style_set_bg_color(&st_micro_bg, lv_color_hex(0xFFFFFF));
    lv_style_set_radius(&st_micro_bg, 10);

    lv_style_init(&st_micro_ind);
    lv_style_set_bg_opa(&st_micro_ind, LV_OPA_COVER);
    lv_style_set_bg_color(&st_micro_ind, lv_color_hex(0x49E0FF));
    lv_style_set_radius(&st_micro_ind, 10);
}

static row_t *get_row(const char *name) {
    for(int i = 0; i < g_row_count; i++) {
        if(strcmp(g_rows[i].name, name) == 0) return &g_rows[i];
    }
    if(g_row_count >= MAX_ROWS) return NULL;

    row_t *r = &g_rows[g_row_count++];
    memset(r, 0, sizeof(*r));
    snprintf(r->name, sizeof(r->name), "%s", name);
    r->state = 0;

    r->row = lv_obj_create(g_list);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_width(r->row, LV_PCT(100));
    lv_obj_set_height(r->row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r->row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r->row, 10, 0);

    r->dot = lv_obj_create(r->row);
    lv_obj_set_size(r->dot, 10, 10);
    lv_obj_set_style_radius(r->dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(r->dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r->dot, lv_color_hex(0x7C8799), 0);

    r->txt = lv_label_create(r->row);
    lv_obj_add_style(r->txt, &st_label, 0);
    lv_label_set_text(r->txt, name);

    return r;
}

static void set_row_state(row_t *r, int state) {
    if(!r) return;
    r->state = state;

    if(state == 2) { /* ok */
        lv_obj_set_style_bg_color(r->dot, lv_color_hex(0x48D38A), 0);
        lv_label_set_text_fmt(r->txt, "%s  (OK)", r->name);
        lv_obj_set_style_text_color(r->txt, lv_color_hex(0xEAF0FF), 0);
        lv_obj_set_style_text_opa(r->txt, LV_OPA_COVER, 0);
    } else if(state == 1) { /* active */
        lv_obj_set_style_bg_color(r->dot, lv_color_hex(0x7AA7FF), 0);
        lv_label_set_text_fmt(r->txt, "%s  (starting…)", r->name);
        lv_obj_set_style_text_color(r->txt, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_opa(r->txt, LV_OPA_COVER, 0);
    } else { /* pending */
        lv_obj_set_style_bg_color(r->dot, lv_color_hex(0x7C8799), 0);
        lv_label_set_text(r->txt, r->name);
        lv_obj_set_style_text_color(r->txt, lv_color_hex(0xEAF0FF), 0);
        lv_obj_set_style_text_opa(r->txt, LV_OPA_70, 0);
    }
}

/* ---------- Build UI ---------- */
void app_ui_init(void) {
    styles_init();

    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);                 /* IMPORTANT: no overlay + no ghost widgets */
    lv_obj_add_style(scr, &st_screen, 0);

    g_card = lv_obj_create(scr);
    lv_obj_add_style(g_card, &st_card, 0);
    lv_obj_set_size(g_card, 760, 380);
    lv_obj_center(g_card);
    lv_obj_set_flex_flow(g_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* Header row */
    lv_obj_t *hdr = lv_obj_create(g_card);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_width(hdr, LV_PCT(100));
    lv_obj_set_height(hdr, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *hdr_left = lv_obj_create(hdr);
    lv_obj_remove_style_all(hdr_left);
    lv_obj_set_flex_flow(hdr_left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hdr_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    g_title = lv_label_create(hdr_left);
    lv_obj_add_style(g_title, &st_title, 0);
    lv_label_set_text(g_title, "System Starting");

    g_subtitle = lv_label_create(hdr_left);
    lv_obj_add_style(g_subtitle, &st_subtitle, 0);
    lv_label_set_text(g_subtitle, "Preparing device…");

    /* Spinner (LVGL API: create(parent) only) */
    g_spinner = lv_spinner_create(hdr);
    lv_obj_set_size(g_spinner, 44, 44);
#if LVGL_VERSION_MAJOR >= 8
    /* Safe on LVGL v8+ if enabled; if not, it compiles out */
    lv_spinner_set_anim_params(g_spinner, 900, 90);
#endif

    /* Overall progress bar */
    g_overall = lv_bar_create(g_card);
    lv_obj_set_width(g_overall, LV_PCT(100));
    lv_obj_set_height(g_overall, 16);
    lv_bar_set_range(g_overall, 0, 100);
    lv_bar_set_value(g_overall, 0, LV_ANIM_OFF);
    lv_obj_add_style(g_overall, &st_bar_bg, LV_PART_MAIN);
    lv_obj_add_style(g_overall, &st_bar_ind, LV_PART_INDICATOR);

    g_overall_txt = lv_label_create(g_card);
    lv_obj_add_style(g_overall_txt, &st_dim, 0);
    lv_label_set_text(g_overall_txt, "0%");

    /* Status row: left + microbar + right */
    lv_obj_t *status = lv_obj_create(g_card);
    lv_obj_remove_style_all(status);
    lv_obj_set_width(status, LV_PCT(100));
    lv_obj_set_height(status, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_status_left = lv_label_create(status);
    lv_obj_add_style(g_status_left, &st_label, 0);
    lv_label_set_text(g_status_left, "Waiting for podkeeper…");

    lv_obj_t *micro_wrap = lv_obj_create(status);
    lv_obj_remove_style_all(micro_wrap);
    lv_obj_set_flex_flow(micro_wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(micro_wrap, 10, 0);

    g_micro = lv_bar_create(micro_wrap);
    lv_obj_set_size(g_micro, 220, 8);
    lv_bar_set_range(g_micro, 0, 100);
    lv_bar_set_value(g_micro, 0, LV_ANIM_OFF);
    lv_obj_add_style(g_micro, &st_micro_bg, LV_PART_MAIN);
    lv_obj_add_style(g_micro, &st_micro_ind, LV_PART_INDICATOR);

    g_status_right = lv_label_create(micro_wrap);
    lv_obj_add_style(g_status_right, &st_dim, 0);
    lv_label_set_text(g_status_right, "");

    /* Container checklist */
    g_list = lv_obj_create(g_card);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_width(g_list, LV_PCT(100));
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_list, 10, 0);

    /* Defaults */
    lv_obj_add_flag(g_micro, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_status_right, LV_OBJ_FLAG_HIDDEN);
}

/* ---------- Public setters ---------- */
void ui_set_phase_text(const char *subtitle) {
    if(!g_subtitle) return;
    if(!subtitle) subtitle = "";
    lv_label_set_text(g_subtitle, subtitle);
}

void ui_set_overall_percent(int pct) {
    if(!g_overall || !g_overall_txt) return;
    pct = clampi(pct, 0, 100);
    lv_bar_set_value(g_overall, pct, LV_ANIM_OFF);

    if(pct >= 95) lv_label_set_text(g_overall_txt, "Finalizing…");
    else lv_label_set_text_fmt(g_overall_txt, "%d%%", pct);
}

void ui_set_import_status(const char *image_tag, int done_layers, int total_layers) {
    if(!g_status_left) return;

    if(image_tag && image_tag[0]) {
        const char *comp = friendly_image_prefix(image_tag);
        lv_label_set_text_fmt(g_status_left, "Preparing %s…", comp);

        if(total_layers > 0) {
            int lp = (int)((100.0f * (float)done_layers) / (float)total_layers);
            lp = clampi(lp, 0, 100);
            lv_bar_set_value(g_micro, lp, LV_ANIM_OFF);

            lv_obj_clear_flag(g_micro, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_status_right, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text_fmt(g_status_right, "%d%% (%d/%d)", lp, done_layers, total_layers);
        } else {
            lv_obj_clear_flag(g_micro, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(g_status_right, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(g_micro, 20, LV_ANIM_OFF);
            lv_label_set_text(g_status_right, "…");
        }
    } else {
        lv_label_set_text(g_status_left, "Starting device services…");
        lv_obj_add_flag(g_micro, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_status_right, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_set_container_active(const char *name) {
    row_t *r = get_row(name ? name : "");
    set_row_state(r, 1);
}

void ui_set_container_ok(const char *name) {
    row_t *r = get_row(name ? name : "");
    set_row_state(r, 2);
}
