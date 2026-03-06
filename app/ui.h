#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void app_ui_init(void);

void ui_set_phase_text(const char *subtitle);
void ui_set_overall_percent(int pct);
void ui_set_import_status(const char *image_tag, int done_layers, int total_layers);
void ui_set_container_active(const char *name);
void ui_set_container_ok(const char *name);

#ifdef __cplusplus
}
#endif
