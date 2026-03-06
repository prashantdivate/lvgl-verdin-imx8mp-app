#include "ui.h"
#include "boot_tracker.h"

#include "lvgl/lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* ---------- UI objects ---------- */

static lv_obj_t *g_root;
static lv_obj_t *g_card;

static lv_obj_t *g_title;
static lv_obj_t *g_subtitle;

static lv_obj_t *g_bar;
static lv_obj_t *g_percent;

static lv_obj_t *g_spinner;

static lv_obj_t *g_detail_left;   /* "Preparing <image>…" / "Starting <container>…" */
static lv_obj_t *g_detail_right;  /* "72% (13/18)" */

static lv_style_t st_card;
static lv_style_t st_title;
static lv_style_t st_subtitle;
static lv_style_t st_detail;
static lv_style_t st_muted;

/* progress state */
static int g_pct = 0;

/* ---------- helpers ---------- */

static void styles_init(void) {
    lv_style_init(&st_card);
    lv_style_set_radius(&st_card, 18);
    lv_style_set_bg_opa(&st_card, LV_OPA_50);
    lv_style_set_bg_color(&st_card, lv_color_hex(0x1B2333));
    lv_style_set_border_width(&st_card, 1);
    lv_style_set_border_opa(&st_card, LV_OPA_30);
    lv_style_set_border_color(&st_card, lv_color_hex(0xFFFFFF));
    lv_style_set_pad_all(&st_card, 26);

    lv_style_init(&st_title);
    lv_style_set_text_color(&st_title, lv_color_hex(0xF4F7FF));
    lv_style_set_text_font(&st_title, LV_FONT_DEFAULT);

    lv_style_init(&st_subtitle);
    lv_style_set_text_color(&st_subtitle, lv_color_hex(0xB8C3D6));
    lv_style_set_text_font(&st_subtitle, LV_FONT_DEFAULT);

    lv_style_init(&st_detail);
    lv_style_set_text_color(&st_detail, lv_color_hex(0xEAF0FF));
    lv_style_set_text_font(&st_detail, LV_FONT_DEFAULT);

    lv_style_init(&st_muted);
    lv_style_set_text_color(&st_muted, lv_color_hex(0x8C98AE));
    lv_style_set_text_font(&st_muted, LV_FONT_DEFAULT);
}

static void ui_build(void) {
    g_root = lv_scr_act();
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x070C14), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);

    g_card = lv_obj_create(g_root);
    lv_obj_add_style(g_card, &st_card, 0);
    lv_obj_set_size(g_card, 820, 320);
    lv_obj_center(g_card);

    /* Layout inside card */
    lv_obj_set_flex_flow(g_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Title */
    g_title = lv_label_create(g_card);
    lv_obj_add_style(g_title, &st_title, 0);
    lv_label_set_text(g_title, "System Starting");
    lv_obj_set_style_text_font(g_title, LV_FONT_DEFAULT, 0);

    /* Subtitle */
    g_subtitle = lv_label_create(g_card);
    lv_obj_add_style(g_subtitle, &st_subtitle, 0);
    lv_label_set_text(g_subtitle, "Waiting for podkeeper…");
    lv_obj_set_style_pad_top(g_subtitle, 8, 0);

    /* spacing */
    lv_obj_t *sp = lv_obj_create(g_card);
    lv_obj_set_size(sp, 1, 12);
    lv_obj_set_style_bg_opa(sp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sp, 0, 0);

    /* Row: progress bar + percent + spinner aligned right */
    lv_obj_t *row = lv_obj_create(g_card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_bar = lv_bar_create(row);
    lv_obj_set_size(g_bar, 520, 16);
    lv_bar_set_range(g_bar, 0, 100);
    lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);

    g_percent = lv_label_create(row);
    lv_obj_add_style(g_percent, &st_detail, 0);
    lv_label_set_text(g_percent, "0%");
    lv_obj_set_style_pad_left(g_percent, 12, 0);

    /* spacer */
    lv_obj_t *row_sp = lv_obj_create(row);
    lv_obj_set_flex_grow(row_sp, 1);
    lv_obj_set_style_bg_opa(row_sp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_sp, 0, 0);
    lv_obj_set_height(row_sp, 1);

    /* Spinner (LVGL signature in your build is lv_spinner_create(parent) only) */
    g_spinner = lv_spinner_create(row);
    lv_obj_set_size(g_spinner, 28, 28);

    /* Details row */
    lv_obj_t *drow = lv_obj_create(g_card);
    lv_obj_set_size(drow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(drow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(drow, 0, 0);
    lv_obj_set_style_pad_top(drow, 12, 0);
    lv_obj_set_flex_flow(drow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(drow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_detail_left = lv_label_create(drow);
    lv_obj_add_style(g_detail_left, &st_muted, 0);
    lv_label_set_text(g_detail_left, "Waiting for podkeeper…");
    lv_label_set_long_mode(g_detail_left, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_detail_left, 560);

    g_detail_right = lv_label_create(drow);
    lv_obj_add_style(g_detail_right, &st_muted, 0);
    lv_label_set_text(g_detail_right, "");
    lv_obj_set_style_pad_left(g_detail_right, 12, 0);
}

/* Clamp helper */
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---------- thread-safe async setters ---------- */

typedef struct {
    char *s;
} str_msg_t;

typedef struct {
    int pct;
} pct_msg_t;

typedef struct {
    char *tag;
    int done;
    int total;
} imp_msg_t;

static void async_set_subtitle(void *p) {
    str_msg_t *m = (str_msg_t *)p;
    if(m && m->s) lv_label_set_text(g_subtitle, m->s);
    if(m) { free(m->s); free(m); }
}

static void async_set_phase_detail(void *p) {
    str_msg_t *m = (str_msg_t *)p;
    if(m && m->s) lv_label_set_text(g_detail_left, m->s);
    if(m) { free(m->s); free(m); }
}

static void async_set_pct(void *p) {
    pct_msg_t *m = (pct_msg_t *)p;
    if(!m) return;

    g_pct = clampi(m->pct, 0, 100);
    lv_bar_set_value(g_bar, g_pct, LV_ANIM_OFF);

    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%d%%", g_pct);
    lv_label_set_text(g_percent, tmp);

    free(m);
}

static void async_set_import(void *p) {
    imp_msg_t *m = (imp_msg_t *)p;
    if(!m) return;

    /* Left text: Preparing <image>… */
    if(m->tag && m->tag[0]) {
        char left[512];
        snprintf(left, sizeof(left), "Preparing %s…", m->tag);
        lv_label_set_text(g_detail_left, left);

        /* Right text: x% (done/total) if possible */
        if(m->total > 0) {
            int pct = (int)((m->done * 100.0f) / (float)m->total);
            pct = clampi(pct, 0, 100);
            char right[64];
            snprintf(right, sizeof(right), "%d%% (%d/%d)", pct, m->done, m->total);
            lv_label_set_text(g_detail_right, right);

            /* Also drive overall bar a bit (import dominates early boot) */
            int overall = clampi((int)(pct * 0.75f), 0, 95);
            lv_bar_set_value(g_bar, overall, LV_ANIM_OFF);
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%d%%", overall);
            lv_label_set_text(g_percent, tmp);
        } else {
            lv_label_set_text(g_detail_right, "…");
        }
    }

    free(m->tag);
    free(m);
}

void ui_set_phase_text(const char *subtitle) {
    /* Update subtitle + left detail line consistently */
    str_msg_t *m1 = calloc(1, sizeof(*m1));
    str_msg_t *m2 = calloc(1, sizeof(*m2));
    if(!m1 || !m2) { free(m1); free(m2); return; }

    m1->s = strdup(subtitle ? subtitle : "");
    m2->s = strdup(subtitle ? subtitle : "");
    lv_async_call(async_set_subtitle, m1);
    lv_async_call(async_set_phase_detail, m2);
}

void ui_set_overall_percent(int pct) {
    pct_msg_t *m = calloc(1, sizeof(*m));
    if(!m) return;
    m->pct = pct;
    lv_async_call(async_set_pct, m);
}

void ui_set_import_status(const char *image_tag, int done_layers, int total_layers) {
    imp_msg_t *m = calloc(1, sizeof(*m));
    if(!m) return;
    m->tag = strdup(image_tag ? image_tag : "");
    m->done = done_layers;
    m->total = total_layers;
    lv_async_call(async_set_import, m);
}

void ui_set_container_active(const char *name) {
    char buf[256];
    snprintf(buf, sizeof(buf), "Starting %s…", name ? name : "service");
    str_msg_t *m = calloc(1, sizeof(*m));
    if(!m) return;
    m->s = strdup(buf);
    lv_async_call(async_set_phase_detail, m);
}

void ui_set_container_ok(const char *name) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s is running", name ? name : "service");
    str_msg_t *m = calloc(1, sizeof(*m));
    if(!m) return;
    m->s = strdup(buf);
    lv_async_call(async_set_phase_detail, m);
}

void app_ui_init(void) {
    static bool inited = false;
    if(inited) return;
    inited = true;

    styles_init();
    ui_build();

    /* IMPORTANT: start tracker automatically so main.c needs ONLY app_ui_init() */
    boot_tracker_start();
}
