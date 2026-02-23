#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Create the initial UI (screen) for the application.
 * This is called after LVGL is initialized and a display backend is ready.
 */
void app_ui_init(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif
