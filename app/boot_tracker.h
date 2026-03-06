#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Starts background tracking thread (safe to call multiple times; starts once). */
void boot_tracker_start(void);

#ifdef __cplusplus
}
#endif
