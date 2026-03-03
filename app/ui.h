#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPLASH_STEP_SYSTEM = 0,
    SPLASH_STEP_SERVICES = 1,
    SPLASH_STEP_DOCKER = 2,
    SPLASH_STEP_DONE = 3
} splash_step_t;

void app_ui_init(void);

/* High-level UI updates */
void splash_set_step(splash_step_t step);
void splash_set_message(const char *msg);

/* Progress helpers */
void splash_set_overall_progress(int percent);          /* 0..100 */
void splash_set_import_progress(const char *component,  /* e.g. "Chromium" */
                                int done_layers,
                                int total_layers);

/* Boot tracker control */
void boot_tracking_start(void);
void boot_tracking_stop(void);

#ifdef __cplusplus
}
#endif
