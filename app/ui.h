#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Entry point used by main.c (call ONLY this; it starts the tracker automatically). */
void app_ui_init(void);

/* Thread-safe setters (internally marshalled via lv_async_call) */
void ui_set_phase_text(const char *subtitle);
void ui_set_overall_percent(int pct);

/* Import: shows "Preparing <image>" and (done/total) */
void ui_set_import_status(const char *image_tag, int done_layers, int total_layers);

/* Container status */
void ui_set_container_active(const char *name);
void ui_set_container_ok(const char *name);

#ifdef __cplusplus
}
#endif
