#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPLASH_STEP_SYSTEM   = 0,
    SPLASH_STEP_SERVICES = 1,
    SPLASH_STEP_DOCKER   = 2,
    SPLASH_STEP_DONE     = 3
} splash_step_t;

void app_ui_init(void);

void splash_set_step(splash_step_t step);
void splash_set_message(const char *msg);

/* Boot tracker control (starts log follower + container polling) */
void boot_tracking_start(void);
void boot_tracking_stop(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
