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

/* Entry */
void app_ui_init(void);

/* Thread-safe UI updates (safe to call from boot_tracker thread) */
void splash_set_step(splash_step_t step);
void splash_set_message(const char *msg);
void splash_set_subtitle(const char *subtitle);

/* Overall progress 0..100 */
void splash_set_overall_progress(int percent);

/* Image import progress (layers) */
void splash_set_import_progress(const char *image_tag, int done_layers, int total_layers);

/* Container checklist updates */
void splash_set_container_wait(const char *name);
void splash_set_container_running(const char *name);

/* Boot tracker control (wrappers) */
void boot_tracking_start(void);
void boot_tracking_stop(void);

#ifdef __cplusplus
}
#endif
