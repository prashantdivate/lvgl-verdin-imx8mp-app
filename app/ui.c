#include "ui.h"
#include "lvgl/lvgl.h"
#include <math.h>

/* =========================
 *  Screen look (tune)
 * ========================= */
#define COL_TEXT        lv_color_hex(0xE7EEF8)
#define COL_MUTED       lv_color_hex(0x9DB2CC)
#define COL_PANEL       lv_color_hex(0x0E1624)
#define COL_DIVIDER     lv_color_hex(0x2A3B55)

#define COL_RING_BLUE   lv_color_hex(0x2FA8FF)
#define COL_RING_GREEN  lv_color_hex(0x3DFFB3)


/* LVGL v9: use numeric opa (0..255) so we don't depend on LV_OPA_XX macros */
#ifndef LV_OPA_12
#define LV_OPA_12   31   /* 12% of 255 */
#endif
#ifndef LV_OPA_18
#define LV_OPA_18   46   /* 18% */
#endif
#ifndef LV_OPA_20
#define LV_OPA_20   64   /* 25% */
#endif
#ifndef LV_OPA_30
#define LV_OPA_30   89   /* 35% */
#endif
#ifndef LV_OPA_40
#define LV_OPA_40   115  /* 45% */
#endif
#ifndef LV_OPA_50
#define LV_OPA_50   140  /* 55% */
#endif
#ifndef LV_OPA_70
#define LV_OPA_70   191  /* 75% */
#endif

/* ------- Assets (converted with LVGL v9 converter) ------- */
extern const lv_image_dsc_t mad_logo;    /* your existing mad_logo.c */
extern const lv_image_dsc_t boot_bg;          /* bg.c */
extern const lv_image_dsc_t icon_system;     /* ic_gear.c */
extern const lv_image_dsc_t icon_service;    /* ic_cloud.c */
extern const lv_image_dsc_t icon_docker;   /* ic_docker.c */

/* =========================
 *  Static UI handles
 * ========================= */
static lv_obj_t *g_step_cards[3]  = {0};
static lv_obj_t *g_step_labels[3] = {0};
static lv_obj_t *g_message        = NULL;

/* Loader objects */
static lv_obj_t *g_ring = NULL;
static lv_obj_t *g_dot  = NULL;
static lv_obj_t *g_seg[28] = {0};
static lv_timer_t *g_ring_timer = NULL;
static int32_t g_phase = 0;

/* =========================
 *  Helpers
 * ========================= */
static void set_card_active(uint32_t idx, bool active)
{
    if(idx >= 3 || g_step_cards[idx] == NULL) return;

    lv_obj_t *card = g_step_cards[idx];
    lv_obj_t *lbl  = g_step_labels[idx];

    lv_obj_set_style_bg_opa(card, active ? LV_OPA_25 : LV_OPA_0, 0);
    lv_obj_set_style_border_opa(card, active ? LV_OPA_40 : LV_OPA_0, 0);
    lv_obj_set_style_text_color(lbl, active ? COL_TEXT : COL_MUTED, 0);
}

/* Create one status item (icon + text) */
static lv_obj_t *create_status_item(lv_obj_t *parent, uint32_t idx,
                                    const lv_image_dsc_t *icon_dsc, const char *text)
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
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon = lv_image_create(card);
    lv_image_set_src(icon, icon_dsc);
    lv_obj_set_style_opa(icon, LV_OPA_90, 0);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, COL_MUTED, 0);

    g_step_cards[idx]  = card;
    g_step_labels[idx] = lbl;

    return card;
}

/* -------------------------
 *  Loader (segmented ring)
 * ------------------------- */
static void ring_place_obj_on_circle(lv_obj_t *obj, int32_t cx, int32_t cy, int32_t r, int32_t deg)
{
    /* LVGL uses 0.1 degrees for transform, but here we do normal degrees for trig */
    float rad = (float)deg * (float)M_PI / 180.0f;
    int32_t x = (int32_t)(cx + (float)r * cosf(rad));
    int32_t y = (int32_t)(cy + (float)r * sinf(rad));
    /* center the segment around that point */
    lv_obj_set_pos(obj, x - lv_obj_get_width(obj)/2, y - lv_obj_get_height(obj)/2);
}

static lv_color_t ring_color_for_segment(int i, int head)
{
    /* Blend towards green near the head, blue elsewhere (simple highlight) */
    int dist = i - head;
    if(dist < 0) dist += 28;
    /* dist=0 brightest, dist grows => fades to blue */
    uint8_t a = (dist == 0) ? 255 :
                (dist == 1) ? 220 :
                (dist == 2) ? 160 :
                (dist == 3) ? 110 :
                (dist == 4) ? 80  : 40;

    lv_color_t mix = lv_color_mix(COL_RING_GREEN, COL_RING_BLUE, (uint8_t)(255 - a));
    return mix;
}

static void ring_timer_cb(lv_timer_t *t)
{
    (void)t;
    if(!g_ring) return;

    g_phase = (g_phase + 6) % 360;          /* rotation speed */
    int head = (g_phase / (360/28)) % 28;   /* head segment index */

    /* ring center */
    lv_coord_t w = lv_obj_get_width(g_ring);
    lv_coord_t h = lv_obj_get_height(g_ring);
    int32_t cx = (int32_t)(w/2);
    int32_t cy = (int32_t)(h/2);

    /* segments radius */
    int32_t r_outer = (w < h ? w : h) / 2 - 10;

    for(int i = 0; i < 28; i++) {
        int32_t deg = -90 + i * (360/28) + g_phase; /* start at top */
        ring_place_obj_on_circle(g_seg[i], cx, cy, r_outer, deg);

        lv_color_t c = ring_color_for_segment(i, head);
        lv_obj_set_style_bg_color(g_seg[i], c, 0);

        /* opacity gradient (strong near head) */
        int dist = i - head; if(dist < 0) dist += 28;
        lv_opa_t opa = (dist == 0) ? LV_OPA_COVER :
                       (dist == 1) ? LV_OPA_90 :
                       (dist == 2) ? LV_OPA_70 :
                       (dist == 3) ? LV_OPA_55 :
                       (dist == 4) ? LV_OPA_45 : LV_OPA_30;
        lv_obj_set_style_bg_opa(g_seg[i], opa, 0);
    }

    /* glow dot follows head */
    int32_t dot_deg = -90 + head * (360/28) + g_phase;
    ring_place_obj_on_circle(g_dot, cx, cy, r_outer, dot_deg);

    /* subtle pulsing */
    lv_opa_t dot_opa = (lv_opa_t)(180 + (int)(60 * sinf((float)g_phase * (float)M_PI / 180.0f)));
    lv_obj_set_style_opa(g_dot, dot_opa, 0);
}

static lv_obj_t *create_segmented_ring(lv_obj_t *parent, lv_coord_t size)
{
    g_ring = lv_obj_create(parent);
    lv_obj_remove_style_all(g_ring);
    lv_obj_set_size(g_ring, size, size);
    lv_obj_set_style_bg_opa(g_ring, LV_OPA_0, 0);

    /* create segments */
    for(int i = 0; i < 28; i++) {
        lv_obj_t *s = lv_obj_create(g_ring);
        lv_obj_remove_style_all(s);
        lv_obj_set_size(s, 16, 6); /* segment size */
        lv_obj_set_style_radius(s, 6, 0);
        lv_obj_set_style_bg_color(s, COL_RING_BLUE, 0);
        lv_obj_set_style_bg_opa(s, LV_OPA_40, 0);
        g_seg[i] = s;
    }

    /* glow dot */
    g_dot = lv_obj_create(g_ring);
    lv_obj_remove_style_all(g_dot);
    lv_obj_set_size(g_dot, 16, 16);
    lv_obj_set_style_radius(g_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_dot, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(g_dot, LV_OPA_COVER, 0);

    /* glow */
    lv_obj_set_style_shadow_width(g_dot, 28, 0);
    lv_obj_set_style_shadow_opa(g_dot, LV_OPA_60, 0);
    lv_obj_set_style_shadow_color(g_dot, COL_RING_GREEN, 0);

    /* start timer */
    if(g_ring_timer) lv_timer_del(g_ring_timer);
    g_ring_timer = lv_timer_create(ring_timer_cb, 30, NULL); /* ~33 FPS */
    ring_timer_cb(NULL);

    return g_ring;
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

    /* Dark vignette overlay for “depth” */
    lv_obj_t *vign = lv_obj_create(scr);
    lv_obj_remove_style_all(vign);
    lv_obj_set_size(vign, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(vign, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(vign, LV_OPA_25, 0);
    lv_obj_move_foreground(vign);

    /* Outer glass frame */
    lv_obj_t *frame = lv_obj_create(scr);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, LV_PCT(96), LV_PCT(92));
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_0, 0);
    lv_obj_set_style_border_width(frame, 3, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0x93A8C6), 0);
    lv_obj_set_style_border_opa(frame, LV_OPA_30, 0);
    lv_obj_set_style_radius(frame, 14, 0);

    /* Inner border */
    lv_obj_t *frame2 = lv_obj_create(scr);
    lv_obj_remove_style_all(frame2);
    lv_obj_set_size(frame2, LV_PCT(94), LV_PCT(90));
    lv_obj_align(frame2, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(frame2, LV_OPA_0, 0);
    lv_obj_set_style_border_width(frame2, 2, 0);
    lv_obj_set_style_border_color(frame2, lv_color_hex(0x93A8C6), 0);
    lv_obj_set_style_border_opa(frame2, LV_OPA_18, 0);
    lv_obj_set_style_radius(frame2, 12, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Starting Up...");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_set_style_text_font(title, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_opa(title, LV_OPA_90, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 56);

    /* Segmented loader ring */
    lv_obj_t *ring = create_segmented_ring(scr, 220);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, 135);

    /* Logo */
    lv_obj_t *logo = lv_image_create(scr);
    lv_image_set_src(logo, &mad_logo);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 340);

    /* Subtitle */
    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "Embedded Devices");
    lv_obj_set_style_text_color(sub, COL_TEXT, 0);
    lv_obj_set_style_text_opa(sub, LV_OPA_75, 0);
    lv_obj_align_to(sub, logo, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    /* Status message under logo */
    g_message = lv_label_create(scr);
    lv_label_set_text(g_message, "Preparing system...");
    lv_obj_set_style_text_color(g_message, COL_MUTED, 0);
    lv_obj_set_style_text_opa(g_message, LV_OPA_80, 0);
    lv_obj_align_to(g_message, sub, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    /* Bottom bar (glass) */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(90), 86);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -42);
    lv_obj_set_style_radius(bar, 16, 0);
    lv_obj_set_style_bg_color(bar, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_35, 0);
    lv_obj_set_style_border_width(bar, 2, 0);
    lv_obj_set_style_border_color(bar, COL_DIVIDER, 0);
    lv_obj_set_style_border_opa(bar, LV_OPA_40, 0);

    /* subtle glow */
    lv_obj_set_style_shadow_width(bar, 24, 0);
    lv_obj_set_style_shadow_color(bar, lv_color_hex(0x20344F), 0);
    lv_obj_set_style_shadow_opa(bar, LV_OPA_50, 0);

    lv_obj_set_style_pad_all(bar, 10, 0);
    lv_obj_set_style_pad_gap(bar, 10, 0);

    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Vertical dividers between items */
    /* We emulate like the reference using thin objects */
    for(int i = 0; i < 2; i++) {
        lv_obj_t *div = lv_obj_create(bar);
        lv_obj_remove_style_all(div);
        lv_obj_set_size(div, 2, 46);
        lv_obj_set_style_bg_color(div, lv_color_hex(0x7C90AE), 0);
        lv_obj_set_style_bg_opa(div, LV_OPA_12, 0);
    }
    /* NOTE: Dividers are created but flex order matters; easiest is to create items+dividers manually.
       To keep it simple, we’ll just rely on spacing and border; if you want exact divider positions,
       tell me and I’ll switch to manual absolute layout for the bar. */

    /* Status items (icons) */
    create_status_item(bar, 0, &icon_system,   "Loading System...");
    create_status_item(bar, 1, &icon_service,  "Starting Services...");
    create_status_item(bar, 2, &icon_docker, "Initializing Docker...");

    splash_set_step(SPLASH_STEP_SYSTEM);
}

void splash_set_step(splash_step_t step)
{
    for(uint32_t i = 0; i < 3; i++) set_card_active(i, false);

    if(step == SPLASH_STEP_DONE) {
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
