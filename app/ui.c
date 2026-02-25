#include "ui.h"
#include "lvgl/lvgl.h"
#include <string.h>
#include <stdio.h>

/* SquareLine public headers (your exported UI under app/sq_ui) */
#include "sq_ui/sq_ui.h"
#include "sq_ui/screens/ui_Splash.h"
#include "sq_ui/screens/ui_Clock.h"

/*
 * SquareLine sources compiled by inclusion to avoid build-system changes.
 * (So your existing CMake patch that only compiles app/*.c still works.)
 */

/* helpers */
#include "sq_ui/sq_ui_helpers.c"

/* components */
#include "sq_ui/components/ui_comp.c"
#include "sq_ui/components/ui_comp_alarm_comp.c"
#include "sq_ui/components/ui_comp_clock_dot.c"
#include "sq_ui/components/ui_comp_hook.c"
#include "sq_ui/components/ui_comp_scrolldots.c"
#include "sq_ui/components/ui_comp_small_label.c"

/* screens */
#include "sq_ui/screens/ui_Splash.c"
#include "sq_ui/screens/ui_Clock.c"

/* fonts + images */
#include "sq_ui/fonts/ui_font_Number.c"
#include "sq_ui/images/ui_img_mad_logo_png.c"

/* main SquareLine glue (keep last) */
#include "sq_ui/sq_ui.c"

/* =========================
 * Behavior configuration
 * ========================= */
#ifndef SPLASH_TO_CLOCK_DELAY_MS
#define SPLASH_TO_CLOCK_DELAY_MS 2000  /* change delay here if needed */
#endif

static lv_timer_t *g_to_clock_timer = NULL;
static splash_step_t g_last_step = SPLASH_STEP_SYSTEM;
static char g_last_msg[128] = "Preparing System ...";

/* Forward */
static void apply_step_to_clock(splash_step_t step);
static void apply_message_to_clock(const char *msg);

static void to_clock_cb(lv_timer_t *t)
{
    (void)t;
    g_to_clock_timer = NULL;

    /* Switch to Clock with a simple fade */
    if(ui_Clock) {
        lv_scr_load_anim(ui_Clock, LV_SCR_LOAD_ANIM_FADE_ON, 250, 0, false);
    }

    /* Ensure Clock screen shows current state */
    apply_step_to_clock(g_last_step);
    apply_message_to_clock(g_last_msg);
}

void app_ui_init(void)
{
    /* Init SquareLine UI (by default it loads ui_Splash) */
    ui_init();

    /* Initialize splash title if present */
    if(ui_Startup_H1) {
        lv_label_set_text(ui_Startup_H1, "Starting Up...");
    }

    /* Set default Clock state (even before we switch) */
    apply_step_to_clock(SPLASH_STEP_SYSTEM);
    apply_message_to_clock("Preparing System ...");

    /* Schedule Splash -> Clock transition */
    if(g_to_clock_timer) {
        lv_timer_del(g_to_clock_timer);
        g_to_clock_timer = NULL;
    }
    g_to_clock_timer = lv_timer_create(to_clock_cb, SPLASH_TO_CLOCK_DELAY_MS, NULL);
    lv_timer_set_repeat_count(g_to_clock_timer, 1);
}

static void apply_message_to_clock(const char *msg)
{
    if(msg == NULL) return;

    /* In your SquareLine export, the "Preparing System ..." label on Clock is ui_Date */
    if(ui_Date) {
        lv_label_set_text(ui_Date, msg);
    }
}

static void apply_step_to_clock(splash_step_t step)
{
    /* Show/hide spinner depending on step */
    if(ui_Spinner1) {
        if(step == SPLASH_STEP_DONE) lv_obj_add_flag(ui_Spinner1, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(ui_Spinner1, LV_OBJ_FLAG_HIDDEN);
    }

    /* Default step-to-text mapping on Clock label (ui_Date) */
    if(ui_Date) {
        switch(step) {
            case SPLASH_STEP_SYSTEM:
                lv_label_set_text(ui_Date, "Preparing System ...");
                break;
            case SPLASH_STEP_SERVICES:
                lv_label_set_text(ui_Date, "Starting Services ...");
                break;
            case SPLASH_STEP_DOCKER:
                lv_label_set_text(ui_Date, "Loading Containers ...");
                break;
            case SPLASH_STEP_DONE:
                lv_label_set_text(ui_Date, "Ready.");
                break;
            default:
                break;
        }
    }
}

void splash_set_step(splash_step_t step)
{
    g_last_step = step;

    /* If Clock exists, apply immediately (safe even if still on Splash) */
    apply_step_to_clock(step);
}

void splash_set_message(const char *msg)
{
    if(msg == NULL) return;

    /* Store for later (when Clock loads) */
    snprintf(g_last_msg, sizeof(g_last_msg), "%s", msg);

    /* If Clock label exists update it */
    if(ui_Date) {
        lv_label_set_text(ui_Date, msg);
        return;
    }

    /* Otherwise update Splash title while still on Splash */
    if(ui_Startup_H1) {
        lv_label_set_text(ui_Startup_H1, msg);
    }
}
