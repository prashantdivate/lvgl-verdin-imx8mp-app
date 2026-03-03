#include "boot_tracker.h"
#include "ui.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PODKEEPER_LOG "/var/log/podkeeper.log"

/* Tune how many lines to “catch up” on boot */
#define CATCHUP_MAX_LINES 3000

static pthread_t g_thread;
static volatile bool g_run = false;

/* Import state */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_import_active = false;
static char g_image_tag[128] = {0};
static int g_total_images = 0;
static int g_images_done = 0;
static int g_done_layers = 0;
static int g_total_layers = 0;

/* Friendly mapping (prefix -> name) */
static const struct { const char *prefix; const char *friendly; } kFriendly[] = {
    { "chromium-imx8",        "Chromium" },
    { "lobby-panel-ems-db",   "Database Service" },
    { "lobby-panel-ems",      "Lobby Panel" },
    { "weston-vivante",       "Display System" },
    { "library",              "Core System Libraries" },
    { "hermes",               "System Communication Service" },
    { "mosaicone",            "Cloud Connectivity Service" },
    { "matisse-voip-client",  "Video Messaging Service" },
    { "sceniq",               "User Interface" },
};

static void tag_prefix(const char *tag, char *out, size_t outsz)
{
    if (!tag || !out || outsz == 0) return;
    const char *colon = strchr(tag, ':');
    size_t n = colon ? (size_t)(colon - tag) : strlen(tag);
    if (n >= outsz) n = outsz - 1;
    memcpy(out, tag, n);
    out[n] = 0;
}

static const char *friendly_image(const char *tag)
{
    static char prefix[128];
    tag_prefix(tag, prefix, sizeof(prefix));
    for (size_t i = 0; i < sizeof(kFriendly)/sizeof(kFriendly[0]); i++) {
        if (strcmp(prefix, kFriendly[i].prefix) == 0)
            return kFriendly[i].friendly;
    }
    return prefix[0] ? prefix : "System component";
}

static int compute_overall_percent_locked(void)
{
    /* Same spirit as your Python: import dominates early so UI moves */
    const float img_weight = 0.70f;
    const float svc_weight = 0.30f;

    float img_part = 0.0f;
    if (g_total_images > 0) {
        img_part = (float)g_images_done / (float)g_total_images;
        if (g_import_active && g_total_layers > 0) {
            float partial = (float)g_done_layers / (float)g_total_layers;
            img_part = (float)g_images_done + partial;
            img_part /= (float)g_total_images;
            if (img_part > 1.0f) img_part = 1.0f;
        }
    }

    /* We’re not doing event-driven container % here (older approach),
       so keep it simple: show import % as the majority. */
    float svc_part = 0.0f; /* placeholder */
    float overall = img_weight * img_part + svc_weight * svc_part;

    int pct = (int)(overall * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

static void handle_line(const char *line)
{
    if (!line) return;
    const char *p = strstr(line, "BOOTPROG ");
    if (!p) return;
    p += strlen("BOOTPROG ");

    pthread_mutex_lock(&g_lock);

    if (strncmp(p, "TOTAL ", 6) == 0) {
        const char *m = strstr(p, "images=");
        if (m) g_total_images = atoi(m + 7);
    }
    else if (strncmp(p, "IMAGE_IMPORT_START ", 18) == 0) {
        const char *tag = p + 18;
        while (*tag == ' ') tag++;
        snprintf(g_image_tag, sizeof(g_image_tag), "%s", tag);
        g_import_active = true;
        g_done_layers = 0;
        g_total_layers = 0;
    }
    else if (strncmp(p, "IMAGE_PLAN ", 11) == 0) {
        /* Example:
           IMAGE_PLAN chromium-imx8:4 total_layers=18 total_bytes=...
         */
        const char *rest = p + 11;
        const char *sp = strchr(rest, ' ');
        if (sp) {
            size_t n = (size_t)(sp - rest);
            if (n >= sizeof(g_image_tag)) n = sizeof(g_image_tag) - 1;
            memcpy(g_image_tag, rest, n);
            g_image_tag[n] = 0;
        }

        const char *tl = strstr(p, "total_layers=");
        if (tl) g_total_layers = atoi(tl + 12);
    }
    else if (strncmp(p, "BLOB_DONE ", 10) == 0) {
        const char *dl = strstr(p, "done_layers=");
        const char *tl = strstr(p, "total_layers=");
        if (dl) g_done_layers = atoi(dl + 11);
        if (tl) g_total_layers = atoi(tl + 12);
    }
    else if (strncmp(p, "IMAGE_IMPORT_DONE ", 18) == 0 ||
             strncmp(p, "IMAGE_IMPORT_EXISTS ", 20) == 0) {
        g_images_done++;
        g_import_active = false;
        g_image_tag[0] = 0;
        g_done_layers = 0;
        g_total_layers = 0;
    }

    /* snapshot for UI */
    bool importing = g_import_active;
    char tag_copy[128]; snprintf(tag_copy, sizeof(tag_copy), "%s", g_image_tag);
    int done = g_done_layers;
    int total = g_total_layers;
    int overall_pct = compute_overall_percent_locked();

    pthread_mutex_unlock(&g_lock);

    /* UI update */
    if (importing) {
        splash_set_step(SPLASH_STEP_SYSTEM);
        splash_set_import_progress(friendly_image(tag_copy), done, total);
    }
    splash_set_overall_progress(overall_pct);
}

static void catchup_existing_log(void)
{
    FILE *f = fopen(PODKEEPER_LOG, "r");
    if (!f) return;

    /* Read last CATCHUP_MAX_LINES lines without loading huge files */
    char *ring[CATCHUP_MAX_LINES];
    memset(ring, 0, sizeof(ring));
    int idx = 0, count = 0;

    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        free(ring[idx]);
        ring[idx] = strdup(buf);
        idx = (idx + 1) % CATCHUP_MAX_LINES;
        if (count < CATCHUP_MAX_LINES) count++;
    }
    fclose(f);

    /* Replay in correct order */
    int start = (count == CATCHUP_MAX_LINES) ? idx : 0;
    for (int i = 0; i < count; i++) {
        int pos = (start + i) % CATCHUP_MAX_LINES;
        if (ring[pos]) handle_line(ring[pos]);
        free(ring[pos]);
    }
}

static void *thread_main(void *arg)
{
    (void)arg;

    /* 1) catch up (fixes your 0/18 issue when starting mid-import) */
    catchup_existing_log();

    /* 2) then follow live */
    FILE *pipe = popen("tail -n 0 -F " PODKEEPER_LOG, "r");
    if (!pipe) return NULL;

    char line[1024];
    while (g_run && fgets(line, sizeof(line), pipe)) {
        handle_line(line);
    }

    pclose(pipe);
    return NULL;
}

void boot_tracker_start(void)
{
    if (g_run) return;
    g_run = true;

    /* default UI */
    splash_set_step(SPLASH_STEP_SYSTEM);
    splash_set_message("Preparing device …");
    splash_set_overall_progress(0);

    pthread_create(&g_thread, NULL, thread_main, NULL);
}

void boot_tracker_stop(void)
{
    if (!g_run) return;
    g_run = false;
    pthread_join(g_thread, NULL);
}
