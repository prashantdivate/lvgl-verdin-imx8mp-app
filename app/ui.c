#include "ui.h"
#include "lvgl/lvgl.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>

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

/* -----------------------------
 *  Production overlay widgets
 * ----------------------------- */

#define MAX_CONTAINERS_UI 10

static lv_obj_t *g_card = NULL;
static lv_obj_t *g_title = NULL;
static lv_obj_t *g_subtitle = NULL;

static lv_obj_t *g_overall_bar = NULL;
static lv_obj_t *g_overall_pct = NULL;

static lv_obj_t *g_status_left = NULL;
static lv_obj_t *g_micro_bar = NULL;
static lv_obj_t *g_status_right = NULL;

static lv_obj_t *g_list = NULL; /* container checklist parent */
static lv_obj_t *g_spinner = NULL;

/* container row widgets */
typedef struct {
    char name[64];
    lv_obj_t *row;
    lv_obj_t *icon;
    lv_obj_t *label;
    int state; /* 0=pending, 1=active(wait), 2=ok(running) */
} container_row_t;

static container_row_t g_rows[MAX_CONTAINERS_UI];
static int g_rows_count = 0;

/* -----------------------------
 *  Thread-safe state + apply
 * ----------------------------- */

typedef struct {
    splash_step_t step;
    char subtitle[128];

    int overall_percent;

    char image_tag[128];
    int done_layers;
    int total_layers;

    char status_left[256];
    char status_right[64];

    /* container events */
    char last_wait_container[64];
    char last_running_container[64];
    int has_wait;
    int has_running;
} ui_state_t;

static ui_state_t g_state;
static pthread_mutex_t g_state_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_apply_scheduled = 0;

static const char *friendly_phase_from_step(splash_step_t s)
{
    switch (s) {
        case SPLASH_STEP_SYSTEM:   return "Preparing device…";
        case SPLASH_STEP_SERVICES: return "Starting device services…";
        case SPLASH_STEP_DOCKER:   return "Final system checks…";
        case SPLASH_STEP_DONE:     return "Starting…";
        default:                   return "Preparing device…";
    }
}

/* Best-effort friendly name based on image tag prefix */
static const char *friendly_image_prefix(const char *tag)
{
    if (!tag) return "System component";
    static char buf[128];
    snprintf(buf, sizeof(buf), "%s", tag);
    char *colon = strchr(buf, ':');
    if (colon) *colon = '\0';

    if (strcmp(buf, "chromium-imx8") == 0) return "Chromium";
    if (strcmp(buf, "lobby-panel-ems-db") == 0) return "Database";
    if (strcmp(buf, "lobby-panel-ems") == 0) return "Application";
    if (strcmp(buf, "weston-vivante") == 0) return "Display Server";

    return "System component";
}

static void style_card(lv_obj_t *o)
{
    lv_obj_set_style_radius(o, 18, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_20, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(o, 20, 0);
    lv_obj_set_style_pad_row(o, 12, 0);
    lv_obj_set_style_pad_column(o, 12, 0);
}

static void ensure_overlay_ui(void)
{
    if (!ui_Clock) return;
    if (g_card) return;

    /* Hide SquareLine widgets that conflict with our overlay */
    if (ui_Date) lv_obj_add_flag(ui_Date, LV_OBJ_FLAG_HIDDEN);
    if (ui_Startup_H1) lv_obj_add_flag(ui_Startup_H1, LV_OBJ_FLAG_HIDDEN);
    if (ui_Spinner1) lv_obj_add_flag(ui_Spinner1, LV_OBJ_FLAG_HIDDEN);

    /* Create centered card */
    g_card = lv_obj_create(ui_Clock);
    lv_obj_set_size(g_card, 740, 360);
    lv_obj_center(g_card);
    style_card(g_card);

    lv_obj_set_flex_flow(g_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* Header row: title/subtitle left, spinner right */
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
    lv_label_set_text(g_title, "System Starting");
    lv_obj_set_style_text_font(g_title, &lv_font_montserrat_28, 0);

    g_subtitle = lv_label_create(hdr_left);
    lv_label_set_text(g_subtitle, "Preparing device…");
    lv_obj_set_style_text_font(g_subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_opa(g_subtitle, LV_OPA_70, 0);

    /* LVGL version in your build only supports lv_spinner_create(parent) */
    g_spinner = lv_spinner_create(hdr);
    lv_obj_set_size(g_spinner, 46, 46);

    /* If your LVGL has these APIs, keep them (they exist in v8/v9). */
#if defined(LV_USE_SPINNER) && LV_USE_SPINNER
    /* Typical “nice” spinner config */
    lv_spinner_set_anim_params(g_spinner, 900, 60);
#endif

    /* Overall progress row: bar + percent (inside row, no OUT_RIGHT) */
    lv_obj_t *prow = lv_obj_create(g_card);
    lv_obj_remove_style_all(prow);
    lv_obj_set_width(prow, LV_PCT(100));
    lv_obj_set_height(prow, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(prow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(prow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_overall_bar = lv_bar_create(prow);
    lv_obj_set_height(g_overall_bar, 18);
    lv_obj_set_width(g_overall_bar, 620);
    lv_bar_set_range(g_overall_bar, 0, 100);
    lv_bar_set_value(g_overall_bar, 0, LV_ANIM_OFF);

    g_overall_pct = lv_label_create(prow);
    lv_label_set_text(g_overall_pct, "0%");
    lv_obj_set_style_text_font(g_overall_pct, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_opa(g_overall_pct, LV_OPA_80, 0);

    /* Status row: left text, micro bar, right text */
    lv_obj_t *srow = lv_obj_create(g_card);
    lv_obj_remove_style_all(srow);
    lv_obj_set_width(srow, LV_PCT(100));
    lv_obj_set_height(srow, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(srow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(srow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_status_left = lv_label_create(srow);
    lv_label_set_long_mode(g_status_left, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_status_left, 380);
    lv_obj_set_style_text_font(g_status_left, &lv_font_montserrat_18, 0);

    g_micro_bar = lv_bar_create(srow);
    lv_obj_set_size(g_micro_bar, 220, 8);
    lv_bar_set_range(g_micro_bar, 0, 100);
    lv_bar_set_value(g_micro_bar, 0, LV_ANIM_OFF);

    g_status_right = lv_label_create(srow);
    lv_obj_set_style_text_font(g_status_right, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_opa(g_status_right, LV_OPA_80, 0);

    /* Container checklist title */
    lv_obj_t *lt = lv_label_create(g_card);
    lv_label_set_text(lt, "Starting services");
    lv_obj_set_style_text_font(lt, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_opa(lt, LV_OPA_70, 0);

    /* Container checklist list */
    g_list = lv_obj_create(g_card);
    lv_obj_remove_style_all(g_list);
    lv_obj_set_width(g_list, LV_PCT(100));
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(g_list, 10, 0);

    /* Default texts */
    lv_label_set_text(g_status_left, "Initializing…");
    lv_label_set_text(g_status_right, "");
}

/* Create/get a row for a container name */
static container_row_t *row_get_or_create(const char *name)
{
    if (!name || !name[0] || !g_list) return NULL;

    for (int i = 0; i < g_rows_count; i++) {
        if (strcmp(g_rows[i].name, name) == 0) return &g_rows[i];
    }

    if (g_rows_count >= MAX_CONTAINERS_UI) return NULL;

    container_row_t *r = &g_rows[g_rows_count++];
    memset(r, 0, sizeof(*r));
    snprintf(r->name, sizeof(r->name), "%s", name);
    r->state = 0;

    r->row = lv_obj_create(g_list);
    lv_obj_remove_style_all(r->row);
    lv_obj_set_width(r->row, LV_PCT(100));
    lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r->row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r->row, 10, 0);

    r->icon = lv_label_create(r->row);
    lv_label_set_text(r->icon, "●");
    lv_obj_set_style_text_opa(r->icon, LV_OPA_40, 0);

    r->label = lv_label_create(r->row);
    lv_label_set_text(r->label, r->name);
    lv_obj_set_style_text_font(r->label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_opa(r->label, LV_OPA_80, 0);

    return r;
}

static void row_set_state(container_row_t *r, int state)
{
    if (!r) return;
    r->state = state;

    if (state == 2) {
        lv_label_set_text(r->icon, "✓");
        lv_obj_set_style_text_opa(r->icon, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(r->icon, lv_color_hex(0x48D38A), 0);
        lv_obj_set_style_text_opa(r->label, LV_OPA_COVER, 0);
    } else if (state == 1) {
        lv_label_set_text(r->icon, ">");
        lv_obj_set_style_text_opa(r->icon, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(r->icon, lv_color_hex(0x7AA7FF), 0);
        lv_obj_set_style_text_opa(r->label, LV_OPA_COVER, 0);
    } else {
        lv_label_set_text(r->icon, "●");
        lv_obj_set_style_text_opa(r->icon, LV_OPA_40, 0);
        lv_obj_set_style_text_opa(r->label, LV_OPA_80, 0);
    }
}

/* Apply state -> LVGL objects (runs on LVGL thread) */
static void ui_apply_cb(void *arg)
{
    (void)arg;

    ensure_overlay_ui();
    if (!g_card) return;

    ui_state_t s;
    pthread_mutex_lock(&g_state_mu);
    s = g_state;
    g_apply_scheduled = 0;
    pthread_mutex_unlock(&g_state_mu);

    /* subtitle */
    const char *sub = s.subtitle[0] ? s.subtitle : friendly_phase_from_step(s.step);
    if (g_subtitle) lv_label_set_text(g_subtitle, sub);

    /* overall bar */
    if (s.overall_percent < 0) s.overall_percent = 0;
    if (s.overall_percent > 100) s.overall_percent = 100;
    if (g_overall_bar) lv_bar_set_value(g_overall_bar, s.overall_percent, LV_ANIM_OFF);
    if (g_overall_pct) {
        char b[16];
        snprintf(b, sizeof(b), "%d%%", s.overall_percent);
        lv_label_set_text(g_overall_pct, b);
    }

    /* import micro-progress + status line */
    if (s.image_tag[0]) {
        const char *comp = friendly_image_prefix(s.image_tag);

        char left[256];
        snprintf(left, sizeof(left), "Preparing %s…", comp);
        if (g_status_left) lv_label_set_text(g_status_left, left);

        if (s.total_layers > 0 && s.done_layers >= 0) {
            int pct = (s.done_layers * 100) / s.total_layers;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            if (g_micro_bar) lv_bar_set_value(g_micro_bar, pct, LV_ANIM_OFF);

            char right[64];
            snprintf(right, sizeof(right), "%d%% (%d/%d)", pct, s.done_layers, s.total_layers);
            if (g_status_right) lv_label_set_text(g_status_right, right);
        } else {
            if (g_micro_bar) lv_bar_set_value(g_micro_bar, 0, LV_ANIM_OFF);
            if (g_status_right) lv_label_set_text(g_status_right, "…");
        }
    } else {
        if (g_status_left) lv_label_set_text(g_status_left, s.status_left[0] ? s.status_left : "Preparing…");
        if (g_micro_bar) lv_bar_set_value(g_micro_bar, 0, LV_ANIM_OFF);
        if (g_status_right) lv_label_set_text(g_status_right, s.status_right[0] ? s.status_right : "");
    }

    /* container events */
    if (s.has_wait && s.last_wait_container[0]) {
        container_row_t *r = row_get_or_create(s.last_wait_container);
        if (r) row_set_state(r, 1);
    }
    if (s.has_running && s.last_running_container[0]) {
        container_row_t *r = row_get_or_create(s.last_running_container);
        if (r) row_set_state(r, 2);
    }
}

static void schedule_apply_locked(void)
{
    if (g_apply_scheduled) return;
    g_apply_scheduled = 1;
    lv_async_call(ui_apply_cb, NULL);
}

/* -----------------------------
 * Public API (thread-safe)
 * ----------------------------- */

void app_ui_init(void)
{
    ui_init();
    if (ui_Clock) lv_scr_load(ui_Clock);

    pthread_mutex_lock(&g_state_mu);
    memset(&g_state, 0, sizeof(g_state));
    g_state.step = SPLASH_STEP_SYSTEM;
    snprintf(g_state.subtitle, sizeof(g_state.subtitle), "%s", "Preparing device…");
    snprintf(g_state.status_left, sizeof(g_state.status_left), "%s", "Initializing…");
    g_state.overall_percent = 0;
    schedule_apply_locked();
    pthread_mutex_unlock(&g_state_mu);

    ensure_overlay_ui();
    boot_tracking_start();
}

void splash_set_step(splash_step_t step)
{
    pthread_mutex_lock(&g_state_mu);
    g_state.step = step;
    schedule_apply_locked();
    pthread_mutex_unlock(&g_state_mu);
}

void splash_set_message(const char *msg)
{
    if (!msg) return;
    pthread_mutex_lock(&g_state_mu);
    snprintf(g_state.status_left, sizeof(g_state.status_left), "%s", msg);
    g_state.image_tag[0] = '\0';
    g_state.has_wait = 0;
    g_state.has_running = 0;
    schedule_apply_locked();
    pthread_mutex_unlock(&g_state_mu);
}

void splash_set_subtitle(const char *subtitle)
{
    if (!subtitle) return;
    pthread_mutex_lock(&g_state_mu);
    snprintf(g_state.subtitle, sizeof(g_state.subtitle), "%s", subtitle);
    schedule_apply_locked();
    pthread_mutex_unlock(&g_state_mu);
}

void splash_set_overall_progress(int percent)
{
    pthread_mutex_lock(&g_state_mu);
    g_state.overall_percent = percent;
    schedule_apply_locked();
    pthread_mutex_unlock(&g_state_mu);
}

void splash_set_import_progress(const char *image_tag, int done_layers, int total_layers)
{
    pthread_mutex_lock(&g_state_mu);
    if (image_tag && image_tag[0]) snprintf(g_state.image_tag, sizeof(g_state.image_tag), "%s", image_tag);
    else g_state.image_tag[0] = '\0';
    g_state.done_layers = done_layers;
    g_state.total_layers = total_layers;
    schedule_apply_locked();
    pthread_mutex_unlock(&g_state_mu);
}

void splash_set_container_wait(const char *name)
{
    if (!name || !name[0]) return;
    pthread_mutex_lock(&g_state_mu);
    snprintf(g_state.last_wait_container, sizeof(g_state.last_wait_container), "%s", name);
    g_state.has_wait = 1;
    schedule_apply_locked();
    pthread_mutex_unlock(&g_state_mu);
}

void splash_set_container_running(const char *name)
{
    if (!name || !name[0]) return;
    pthread_mutex_lock(&g_state_mu);
    snprintf(g_state.last_running_container, sizeof(g_state.last_running_container), "%s", name);
    g_state.has_running = 1;
    schedule_apply_locked();
    pthread_mutex_unlock(&g_state_mu);
}

/* wrappers */
void boot_tracking_start(void) { boot_tracker_start(); }
void boot_tracking_stop(void)  { boot_tracker_stop();  }
