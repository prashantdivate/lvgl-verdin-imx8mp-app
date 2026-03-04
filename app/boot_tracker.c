#include "boot_tracker.h"
#include "ui.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PODKEEPER_LOG "/var/log/podkeeper.log"

static pthread_t g_thr;
static int g_run = 0;

static int g_total_images = 0;
static int g_images_done = 0;

static char g_current_image[128] = {0};
static int g_done_layers = 0;
static int g_total_layers = 0;

static void trim_newline(char *s)
{
    if (!s) return;
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r')) {
        s[n-1] = '\0';
        n--;
    }
}

static void update_overall_progress(void)
{
    /* Imports 65%, Services 35% (same logic as your Python concept) */
    float img_weight = 0.65f;
    float svc_weight = 0.35f;

    float img_part = 0.0f;
    if (g_total_images > 0) {
        img_part = (float)g_images_done / (float)g_total_images;

        /* partial inside current image */
        if (g_current_image[0] && g_total_layers > 0) {
            float partial = (float)g_done_layers / (float)g_total_layers;
            if (partial < 0) partial = 0;
            if (partial > 1) partial = 1;
            img_part = (float)g_images_done + partial;
            img_part /= (float)g_total_images;
            if (img_part > 1) img_part = 1;
        }
    }

    /* service progress is driven by container events; we keep it “slow” by just letting imports dominate
       unless you want to add container counting here. */
    float svc_part = 0.0f;

    float overall = img_weight * img_part + svc_weight * svc_part;
    int pct = (int)(overall * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    splash_set_overall_progress(pct);
}

static void handle_line(const char *line_in)
{
    if (!line_in) return;

    /* We only care about BOOTPROG lines */
    const char *p = strstr(line_in, "BOOTPROG ");
    if (!p) return;
    p += strlen("BOOTPROG ");

    /* event is first token */
    char event[64] = {0};
    const char *sp = strchr(p, ' ');
    if (!sp) {
        snprintf(event, sizeof(event), "%s", p);
        sp = p + strlen(p);
    } else {
        size_t elen = (size_t)(sp - p);
        if (elen >= sizeof(event)) elen = sizeof(event) - 1;
        memcpy(event, p, elen);
        event[elen] = '\0';
    }

    const char *rest = (*sp == ' ') ? (sp + 1) : sp;

    if (strcmp(event, "PHASE") == 0) {
        /* Map phases to nicer subtitle text */
        if (strstr(rest, "IMAGE_IMPORT_BEGIN")) splash_set_subtitle("Preparing system software…");
        else if (strstr(rest, "VERIFY_IMAGES_BEGIN")) splash_set_subtitle("Verifying system software…");
        else if (strstr(rest, "COMPOSE_UP_BEGIN")) splash_set_subtitle("Starting device services…");
        else if (strstr(rest, "CONTAINERS_WAIT_BEGIN")) splash_set_subtitle("Final system checks…");
        else if (strstr(rest, "START")) splash_set_subtitle("Preparing device…");
        else splash_set_subtitle("Preparing device…");
        return;
    }

    if (strcmp(event, "TOTAL") == 0) {
        /* TOTAL images=4 containers=4 */
        const char *k = strstr(rest, "images=");
        if (k) g_total_images = atoi(k + 7);
        return;
    }

    if (strcmp(event, "IMAGE_IMPORT_START") == 0) {
        snprintf(g_current_image, sizeof(g_current_image), "%s", rest);
        g_done_layers = 0;
        g_total_layers = 0;
        trim_newline(g_current_image);

        /* show immediately (even before IMAGE_PLAN) */
        splash_set_import_progress(g_current_image, g_done_layers, g_total_layers);
        update_overall_progress();
        return;
    }

    if (strcmp(event, "IMAGE_PLAN") == 0) {
        /* IMAGE_PLAN <tag> total_layers=.. total_bytes=.. */
        char tag[128] = {0};
        sscanf(rest, "%127s", tag);
        if (tag[0]) {
            snprintf(g_current_image, sizeof(g_current_image), "%s", tag);
        }

        const char *tl = strstr(rest, "total_layers=");
        if (tl) g_total_layers = atoi(tl + strlen("total_layers="));

        splash_set_import_progress(g_current_image, g_done_layers, g_total_layers);
        update_overall_progress();
        return;
    }

    if (strcmp(event, "BLOB_DONE") == 0) {
        const char *dl = strstr(rest, "done_layers=");
        if (dl) g_done_layers = atoi(dl + strlen("done_layers="));

        const char *tl = strstr(rest, "total_layers=");
        if (tl) g_total_layers = atoi(tl + strlen("total_layers="));

        splash_set_import_progress(g_current_image, g_done_layers, g_total_layers);
        update_overall_progress();
        return;
    }

    if (strcmp(event, "IMAGE_IMPORT_DONE") == 0 || strcmp(event, "IMAGE_IMPORT_EXISTS") == 0) {
        g_images_done++;
        g_current_image[0] = '\0';
        g_done_layers = 0;
        g_total_layers = 0;

        splash_set_import_progress(NULL, 0, 0);
        update_overall_progress();
        return;
    }

    if (strcmp(event, "CONTAINER_WAIT") == 0) {
        /* CONTAINER_WAIT weston 300 */
        char name[64] = {0};
        sscanf(rest, "%63s", name);
        if (name[0]) {
            splash_set_container_wait(name);
            splash_set_message("Starting services…");
        }
        return;
    }

    if (strcmp(event, "CONTAINER_RUNNING") == 0) {
        /* CONTAINER_RUNNING weston */
        char name[64] = {0};
        sscanf(rest, "%63s", name);
        if (name[0]) {
            splash_set_container_running(name);
        }
        return;
    }

    if (strcmp(event, "READY") == 0) {
        splash_set_step(SPLASH_STEP_DONE);
        splash_set_subtitle("Starting…");
        splash_set_message("Almost ready…");
        splash_set_overall_progress(99);
        return;
    }

    if (strcmp(event, "DONE") == 0) {
        splash_set_overall_progress(100);
        splash_set_message("Launching user interface…");
        return;
    }

    if (strcmp(event, "FATAL") == 0) {
        splash_set_subtitle("Startup issue detected");
        splash_set_message("Please contact support.");
        return;
    }
}

static void *tracker_thread(void *arg)
{
    (void)arg;

    FILE *f = fopen(PODKEEPER_LOG, "r");
    if (!f) {
        splash_set_message("Waiting for podkeeper log…");
        while (g_run) sleep(1);
        return NULL;
    }

    /* Default: follow new lines. For testing/replay: start from beginning */
    const char *from_start = getenv("BOOT_TRACKER_FROM_START");
    if (!from_start || strcmp(from_start, "1") != 0) {
        fseek(f, 0, SEEK_END);
    }

    char line[1024];

    while (g_run) {
        if (fgets(line, sizeof(line), f)) {
            trim_newline(line);
            handle_line(line);
        } else {
            clearerr(f);
            usleep(120 * 1000);
        }
    }

    fclose(f);
    return NULL;
}

void boot_tracker_start(void)
{
    if (g_run) return;
    g_run = 1;

    /* initial UI */
    splash_set_step(SPLASH_STEP_SYSTEM);
    splash_set_subtitle("Preparing device…");
    splash_set_message("Initializing…");
    splash_set_overall_progress(0);

    pthread_create(&g_thr, NULL, tracker_thread, NULL);
}

void boot_tracker_stop(void)
{
    if (!g_run) return;
    g_run = 0;
    pthread_join(g_thr, NULL);
}
