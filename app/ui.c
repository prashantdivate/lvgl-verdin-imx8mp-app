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

static lv_obj_t *g_detail_left;
static lv_obj_t *g_detail_right;

static lv_obj_t *g_list_title;
static lv_obj_t *g_list;

static lv_style_t st_card;
static lv_style_t st_title;
static lv_style_t st_subtitle;
static lv_style_t st_detail;
static lv_style_t st_muted;
static lv_style_t st_list_title;

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

/* ---------- helpers ---------- */

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static const char *friendly_image_prefix(const char *tag) {
    if(!tag || !tag[0]) return "System service";

    static char buf[128];
    snprintf(buf, sizeof(buf), "%s", tag);

    char *colon = strchr(buf, ':');
    if(colon) *colon = '\0';

    if(strcmp(buf, "chromium-imx8") == 0)       return "Web Runtime";
    if(strcmp(buf, "lobby-panel-ems-db") == 0)  return "Messaging Database Service";
    if(strcmp(buf, "lobby-panel-ems") == 0)     return "Video Messaging Service";
    if(strcmp(buf, "weston-vivante") == 0)      return "Display System";

    return "System service";
}

static const char *friendly_container_name(const char *name) {
    if(!name || !name[0]) return "Service";

    if(strcmp(name, "weston") == 0)   return "Display System";
    if(strcmp(name, "lcp-db") == 0)   return "Messaging Data Service";
    if(strcmp(name, "lcp") == 0)      return "Video Messaging Service";
    if(strcmp(name, "chromium") == 0) return "Web Runtime";

    return name;
}

static void styles_init(void) {
    lv_style_init(&st_card);
    lv_style_set_radius(&st_card, 22);
    lv_style_set_bg_opa(&st_card, LV_OPA_60);
    lv_style_set_bg_color(&st_card, lv_color_hex(0x1A1F2B));
    lv_style_set_border_width(&st_card, 1);
    lv_style_set_border_opa(&st_card, LV_OPA_20);
    lv_style_set_border_color(&st_card, lv_color_hex(0xFFFFFF));
    lv_style_set_pad_all(&st_card, 28);

    lv_style_init(&st_title);
    lv_style_set_text_color(&st_title, lv_color_hex(0xF4F7FF));
    lv_style_set_text_font(&st_title, LV_FONT_DEFAULT);

    lv_style_init(&st_subtitle);
    lv_style_set_text_color(&st_subtitle, lv_color_hex(0xC6D1E4));
    lv_style_set_text_font(&st_subtitle, LV_FONT_DEFAULT);

    lv_style_init(&st_detail);
    lv_style_set_text_color(&st_detail, lv_color_hex(0xF4F7FF));
    lv_style_set_text_font(&st_detail, LV_FONT_DEFAULT);

    lv_style_init(&st_muted);
    lv_style_set_text_color(&st_muted, lv_color_hex(0x9BA8BE));
    lv_style_set_text_font(&st_muted, LV_FONT_DEFAULT);

    lv_style_init(&st_list_title);
    lv_style_set_text_color(&st_list_title, lv_color_hex(0xB9C6DA));
    lv_style_set_text_font(&st_list_title, LV_FONT_DEFAULT);
}

static row_t *get_row(const char *name) {
    for(int i = 0; i < g_row_count; i++) {
        if(strcmp(g_rows[i].name, name) == 0) return &g_rows[i];
    }
    if(g_row_count >= MAX_ROWS) return NULL;

    row_t *r = &g_rows[g_row_count++];
    memset(r, 0, sizeof(*r));
    snprintf(r->name, sizeof(r->name), "%s", name);

    r->row = lv_obj_create(g_list);
    lv_obj_set_size(r->row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(r->row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r->row, 0, 0);
    lv_obj_set_style_pad_all(r->row, 0, 0);
    lv_obj_set_style_pad_row(r->row, 0, 0);
    lv_obj_set_style_pad_column(r->row, 10, 0);
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r->row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    r->dot = lv_obj_create(r->row);
    lv_obj_set_size(r->dot, 10, 10);
    lv_obj_set_style_radius(r->dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(r->dot, 0, 0);
    lv_obj_set_style_bg_opa(r->dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r->dot, lv_color_hex(0x64748B), 0);

    r->txt = lv_label_create(r->row);
    lv_obj_add_style(r->txt, &st_muted, 0);
    lv_label_set_text(r->txt, friendly_container_name(name));

    return r;
}

static void set_row_state(row_t *r, int state) {
    if(!r) return;
    r->state = state;

    if(state == 2) {
        lv_obj_set_style_bg_color(r->dot, lv_color_hex(0x48D38A), 0);
        lv_obj_set_style_text_color(r->txt, lv_color_hex(0xEAF0FF), 0);
        lv_obj_set_style_text_opa(r->txt, LV_OPA_COVER, 0);
        lv_label_set_text_fmt(r->txt, "%s  (Ready)", friendly_container_name(r->name));
    } else if(state == 1) {
        lv_obj_set_style_bg_color(r->dot, lv_color_hex(0x7AA7FF), 0);
        lv_obj_set_style_text_color(r->txt, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_opa(r->txt, LV_OPA_COVER, 0);
        lv_label_set_text_fmt(r->txt, "%s  (Starting)", friendly_container_name(r->name));
    } else {
        lv_obj_set_style_bg_color(r->dot, lv_color_hex(0x64748B), 0);
        lv_obj_set_style_text_color(r->txt, lv_color_hex(0x9BA8BE), 0);
        lv_obj_set_style_text_opa(r->txt, LV_OPA_80, 0);
        lv_label_set_text(r->txt, friendly_container_name(r->name));
    }
}

/* ---------- Build UI ---------- */

static void ui_build(void) {
    g_root = lv_scr_act();
    lv_obj_clean(g_root);
    lv_obj_set_style_bg_color(g_root, lv_color_hex(0x05070B), 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);

    g_card = lv_obj_create(g_root);
    lv_obj_add_style(g_card, &st_card, 0);
    lv_obj_set_size(g_card, 860, 360);
    lv_obj_center(g_card);

    lv_obj_set_flex_flow(g_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    g_title = lv_label_create(g_card);
    lv_obj_add_style(g_title, &st_title, 0);
    lv_label_set_text(g_title, "System Starting");

    g_subtitle = lv_label_create(g_card);
    lv_obj_add_style(g_subtitle, &st_subtitle, 0);
    lv_label_set_text(g_subtitle, "Waiting for podkeeper...");
    lv_obj_set_style_pad_top(g_subtitle, 6, 0);

    lv_obj_t *space1 = lv_obj_create(g_card);
    lv_obj_set_size(space1, 1, 8);
    lv_obj_set_style_bg_opa(space1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(space1, 0, 0);

    lv_obj_t *row = lv_obj_create(g_card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_bar = lv_bar_create(row);
    lv_obj_set_size(g_bar, 600, 18);
    lv_bar_set_range(g_bar, 0, 100);
    lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);

    g_percent = lv_label_create(row);
    lv_obj_add_style(g_percent, &st_detail, 0);
    lv_label_set_text(g_percent, "0%");

    lv_obj_t *row_sp = lv_obj_create(row);
    lv_obj_set_flex_grow(row_sp, 1);
    lv_obj_set_height(row_sp, 1);
    lv_obj_set_style_bg_opa(row_sp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_sp, 0, 0);

    g_spinner = lv_spinner_create(row);
    lv_obj_set_size(g_spinner, 52, 52);

    lv_obj_t *space2 = lv_obj_create(g_card);
    lv_obj_set_size(space2, 1, 10);
    lv_obj_set_style_bg_opa(space2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(space2, 0, 0);

    lv_obj_t *drow = lv_obj_create(g_card);
    lv_obj_set_size(drow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(drow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(drow, 0, 0);
    lv_obj_set_style_pad_all(drow, 0, 0);
    lv_obj_set_style_pad_column(drow, 12, 0);
    lv_obj_set_flex_flow(drow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(drow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_detail_left = lv_label_create(drow);
    lv_obj_add_style(g_detail_left, &st_detail, 0);
    lv_label_set_text(g_detail_left, "Waiting for podkeeper...");
    lv_label_set_long_mode(g_detail_left, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_detail_left, 620);

    g_detail_right = lv_label_create(drow);
    lv_obj_add_style(g_detail_right, &st_muted, 0);
    lv_label_set_text(g_detail_right, "");

    lv_obj_t *space3 = lv_obj_create(g_card);
    lv_obj_set_size(space3, 1, 14);
    lv_obj_set_style_bg_opa(space3, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(space3, 0, 0);

    g_list_title = lv_label_create(g_card);
    lv_obj_add_style(g_list_title, &st_list_title, 0);
    lv_label_set_text(g_list_title, "Starting services");

    g_list = lv_obj_create(g_card);
    lv_obj_set_size(g_list, LV_PCT(100), 110);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_list, 0, 0);
    lv_obj_set_style_pad_all(g_list, 0, 0);
    lv_obj_set_style_pad_row(g_list, 10, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);

    g_row_count = 0;
}

/* ---------- async message structs ---------- */

typedef struct {
    char *s;
} str_msg_t;

typedef struct {
    int pct;
} pct_msg_t;

typedef struct {
    char *friendly;
    int done;
    int total;
} imp_msg_t;

/* ---------- async callbacks ---------- */

static void async_set_subtitle(void *p) {
    str_msg_t *m = (str_msg_t *)p;
    if(m && m->s) lv_label_set_text(g_subtitle, m->s);
    if(m) {
        free(m->s);
        free(m);
    }
}

static void async_set_detail_left(void *p) {
    str_msg_t *m = (str_msg_t *)p;
    if(m && m->s) lv_label_set_text(g_detail_left, m->s);
    if(m) {
        free(m->s);
        free(m);
    }
}

static void async_set_pct(void *p) {
    pct_msg_t *m = (pct_msg_t *)p;
    if(!m) return;

    int pct = clampi(m->pct, 0, 100);
    lv_bar_set_value(g_bar, pct, LV_ANIM_OFF);
    lv_label_set_text_fmt(g_percent, "%d%%", pct);

    free(m);
}

static void async_set_import(void *p) {
    imp_msg_t *m = (imp_msg_t *)p;
    if(!m) return;

    if(m->friendly && m->friendly[0]) {
        lv_label_set_text_fmt(g_detail_left, "Preparing %s...", m->friendly);

        if(m->total > 0) {
            int pct = clampi((int)((m->done * 100.0f) / (float)m->total), 0, 100);
            lv_label_set_text_fmt(g_detail_right, "%d%% (%d/%d)", pct, m->done, m->total);
        } else {
            lv_label_set_text(g_detail_right, "...");
        }
    }

    free(m->friendly);
    free(m);
}

static void async_set_container_active(void *p) {
    str_msg_t *m = (str_msg_t *)p;
    if(m && m->s) {
        row_t *r = get_row(m->s);
        set_row_state(r, 1);
        lv_label_set_text_fmt(g_detail_left, "Starting %s...", friendly_container_name(m->s));
        lv_label_set_text(g_detail_right, "");
    }
    if(m) {
        free(m->s);
        free(m);
    }
}

static void async_set_container_ok(void *p) {
    str_msg_t *m = (str_msg_t *)p;
    if(m && m->s) {
        row_t *r = get_row(m->s);
        set_row_state(r, 2);
        lv_label_set_text_fmt(g_detail_left, "%s is ready", friendly_container_name(m->s));
        lv_label_set_text(g_detail_right, "");
    }
    if(m) {
        free(m->s);
        free(m);
    }
}

/* ---------- public API ---------- */

void ui_set_phase_text(const char *subtitle) {
    str_msg_t *m = calloc(1, sizeof(*m));
    if(!m) return;
    m->s = strdup(subtitle ? subtitle : "");
    lv_async_call(async_set_subtitle, m);
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
    m->friendly = strdup(friendly_image_prefix(image_tag));
    m->done = done_layers;
    m->total = total_layers;
    lv_async_call(async_set_import, m);
}

void ui_set_container_active(const char *name) {
    str_msg_t *m = calloc(1, sizeof(*m));
    if(!m) return;
    m->s = strdup(name ? name : "");
    lv_async_call(async_set_container_active, m);
}

void ui_set_container_ok(const char *name) {
    str_msg_t *m = calloc(1, sizeof(*m));
    if(!m) return;
    m->s = strdup(name ? name : "");
    lv_async_call(async_set_container_ok, m);
}

void app_ui_init(void) {
    static bool inited = false;
    if(inited) return;
    inited = true;

    styles_init();
    ui_build();

    boot_tracker_start();
}
