#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* UI entry */
void app_ui_init(void);

/* Called by boot_tracker (thread-safe via lv_async_call inside tracker) */
void ui_set_phase_text(const char *subtitle);
void ui_set_overall_percent(int pct);

/* Import progress */
void ui_set_import_status(const char *image_tag, int done_layers, int total_layers);

/* Container status */
void ui_set_container_active(const char *name);   /* waiting/starting */
void ui_set_container_ok(const char *name);       /* running */

#ifdef __cplusplus
} /* extern "C" */
#endif
