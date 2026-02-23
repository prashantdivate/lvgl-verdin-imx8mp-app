#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPLASH_STEP_SYSTEM = 0,
    SPLASH_STEP_SERVICES = 1,
    SPLASH_STEP_DOCKER = 2,
    SPLASH_STEP_DONE = 3
} splash_step_t;

/* Create splash UI on active screen */
void app_ui_init(void);

/* Update which step is currently active (highlights the row and changes text subtly) */
void splash_set_step(splash_step_t step);

/* Optional: update the subtitle text on bottom row or under logo */
void splash_set_message(const char *msg);

#ifdef __cplusplus
} /* extern "C" */
#endif
