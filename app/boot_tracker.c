#include "boot_tracker.h"
#include "ui.h"
#include "lvgl/lvgl.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#define PODKEEPER_LOG "/var/log/podkeeper.log"

/* Containers (checklist order, but state is truth from `podman ps`) */
static const char *CONTAINERS[] = {
    "lib_cont",
    "hermes_cont",
    "mosaicone_cont",
    "vms_cont",
    "sceniq_cont",
};
static const int CONTAINER_COUNT = (int)(sizeof(CONTAINERS) / sizeof(CONTAINERS[0]));

static const char *friendly_container(const char *name)
{
    if(strcmp(name, "lib_cont") == 0) return "Core System Libraries";
    if(strcmp(name, "hermes_cont") == 0) return "System Communication Service";
    if(strcmp(name, "mosaicone_cont") == 0) return "Cloud Connectivity Service";
    if(strcmp(name, "vms_cont") == 0) return "Video Messaging Service";
    if(strcmp(name, "sceniq_cont") == 0) return "User Interface";
    return name;
}

/* Returns a user-visible component name for an image tag.
 * If unknown, returns the prefix itself (e.g. "chromium-imx8", "lobby-panel-ems-db"). */
static const char *friendly_image_tag(const char *tag)
{
    static char prefix[128];

    /* prefix = everything before ':' */
    const char *colon = strchr(tag, ':');
    size_t n = colon ? (size_t)(colon - tag) : strlen(tag);
    if(n >= sizeof(prefix)) n = sizeof(prefix) - 1;
    memcpy(prefix, tag, n);
    prefix[n] = '\0';

    /* known mappings (your original set) */
    if(strcmp(prefix, "library") == 0) return "Core System Libraries";
    if(strcmp(prefix, "hermes") == 0) return "System Communication Service";
    if(strcmp(prefix, "mosaicone") == 0) return "Cloud Connectivity Service";
    if(strcmp(prefix, "matisse-voip-client") == 0) return "Video Messaging Service";
    if(strcmp(prefix, "sceniq") == 0) return "User Interface";

    /* fallback: show actual prefix */
    return prefix[0] ? prefix : "System";
}

/* -------- shared state -------- */
static pthread_t g_log_thread;
static bool g_running = false;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* Import state (so container polling doesn't overwrite import messages) */
static bool g_import_active = false;
static char g_import_component[128] = {0};

/* Container running state */
static bool g_container_running[5] = {false, false, false, false, false};

/* LVGL polling timer (runs on LVGL thread) */
static lv_timer_t *g_poll_timer = NULL;

/* -------- LVGL async helpers -------- */
typedef struct {
    splash_step_t step;
    char msg[256];
    bool set_msg;
    bool set_step;
} ui_update_t;

static void ui_update_cb(void *p)
{
    ui_update_t *u = (ui_update_t *)p;
    if(!u) return;

    if(u->set_step) splash_set_step(u->step);
    if(u->set_msg)  splash_set_message(u->msg);

    free(u);
}

static void push_ui_step_and_message(splash_step_t step, const char *msg)
{
    ui_update_t *u = calloc(1, sizeof(*u));
    if(!u) return;
    u->set_step = true;
    u->step = step;
    u->set_msg = true;
    snprintf(u->msg, sizeof(u->msg), "%s", msg ? msg : "");
    lv_async_call(ui_update_cb, u);
}

/* -------- BOOTPROG parsing helpers -------- */

static void import_set_active(const char *image_tag)
{
    pthread_mutex_lock(&g_lock);
    g_import_active = true;
    snprintf(g_import_component, sizeof(g_import_component), "%s", friendly_image_tag(image_tag));
    pthread_mutex_unlock(&g_lock);

    /* IMPORTANT: immediate message (your “older fix”) */
    char buf[256];
    snprintf(buf, sizeof(buf), "Preparing %s... ", friendly_image_tag(image_tag));
    push_ui_step_and_message(SPLASH_STEP_SYSTEM, buf);
}

static void import_update_layers(int done, int total)
{
    pthread_mutex_lock(&g_lock);
    bool active = g_import_active;
    char comp[128];
    snprintf(comp, sizeof(comp), "%s", g_import_component);
    pthread_mutex_unlock(&g_lock);

    if(!active) return;
    if(total <= 0) return;

    int pct = (int)((done * 100) / total);
    char buf[256];
    snprintf(buf, sizeof(buf), "Preparing %s... %d%% (%d/%d)", comp, pct, done, total);
    push_ui_step_and_message(SPLASH_STEP_SYSTEM, buf);
}

static void import_clear(void)
{
    pthread_mutex_lock(&g_lock);
    g_import_active = false;
    g_import_component[0] = '\0';
    pthread_mutex_unlock(&g_lock);
}

/* Extract first token from a string (skips leading spaces).
 * Writes into out (null-terminated). */
static void first_token(const char *s, char *out, size_t out_sz)
{
    if(!out || out_sz == 0) return;
    out[0] = '\0';
    if(!s) return;

    while(*s == ' ') s++;
    if(*s == '\0') return;

    /* token ends at space or newline */
    size_t i = 0;
    while(*s && *s != ' ' && *s != '\n' && *s != '\r') {
        if(i + 1 < out_sz) out[i++] = *s;
        s++;
    }
    out[i] = '\0';
}

/* If we're not currently importing, infer import context from an image token. */
static void ensure_import_active_from_token(const char *image_token)
{
    if(!image_token || !image_token[0]) return;

    pthread_mutex_lock(&g_lock);
    bool active = g_import_active;
    pthread_mutex_unlock(&g_lock);

    if(!active) {
        import_set_active(image_token);
    }
}

/* Parse one full log line */
static void handle_podkeeper_line(const char *line)
{
    const char *p = strstr(line, "BOOTPROG ");
    if(!p) return;
    p += 9;

    /* IMAGE_IMPORT_START <tag> */
    if(strncmp(p, "IMAGE_IMPORT_START ", 18) == 0) {
        const char *rest = p + 18;
        char img[256];
        first_token(rest, img, sizeof(img));
        if(img[0]) import_set_active(img);
        return;
    }

    /* NEW: IMAGE_PLAN <tag> ... */
    if(strncmp(p, "IMAGE_PLAN ", 11) == 0) {
        const char *rest = p + 11;
        char img[256];
        first_token(rest, img, sizeof(img));
        ensure_import_active_from_token(img);
        return;
    }

    /* NEW: SKOPEO <tag> ... */
    if(strncmp(p, "SKOPEO ", 7) == 0) {
        const char *rest = p + 7;
        char img[256];
        first_token(rest, img, sizeof(img));
        ensure_import_active_from_token(img);
        return;
    }

    /* BLOB_DONE <tag> ... done_layers=.. total_layers=.. */
    if(strncmp(p, "BLOB_DONE ", 10) == 0) {
        const char *rest = p + 10;

        /* NEW: infer current import if we missed IMAGE_IMPORT_START */
        char img[256];
        first_token(rest, img, sizeof(img));
        ensure_import_active_from_token(img);

        /* Extract done_layers and total_layers */
        const char *dl = strstr(p, "done_layers=");
        const char *tl = strstr(p, "total_layers=");
        if(dl && tl) {
            int done = atoi(dl + 11);
            int total = atoi(tl + 13);
            if(done >= 0 && total > 0) import_update_layers(done, total);
        }
        return;
    }

    if(strncmp(p, "IMAGE_IMPORT_DONE ", 17) == 0 || strncmp(p, "IMAGE_IMPORT_EXISTS ", 19) == 0) {
        import_clear();
        return;
    }

    if(strncmp(p, "READY", 5) == 0) {
        import_clear();
        push_ui_step_and_message(SPLASH_STEP_DONE, "Ready.");
        return;
    }
}

/* Thread: follow /var/log/podkeeper.log */
static void *log_thread_fn(void *arg)
{
    (void)arg;

    /* -F handles log rotation */
    FILE *fp = popen("tail -n 0 -F " PODKEEPER_LOG, "r");
    if(!fp) return NULL;

    char buf[2048];
    while(true) {
        pthread_mutex_lock(&g_lock);
        bool run = g_running;
        pthread_mutex_unlock(&g_lock);
        if(!run) break;

        if(!fgets(buf, sizeof(buf), fp)) {
            usleep(100 * 1000);
            continue;
        }
        handle_podkeeper_line(buf);
    }

    pclose(fp);
    return NULL;
}

/* -------- Container polling (LVGL thread) -------- */

static void poll_podman_ps(void)
{
    FILE *fp = popen("podman ps --format \"{{.Names}}\"", "r");
    if(!fp) return;

    bool running_now[5] = {false, false, false, false, false};

    char line[512];
    while(fgets(line, sizeof(line), fp)) {
        /* strip newline */
        size_t n = strlen(line);
        while(n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) {
            line[n-1] = '\0';
            n--;
        }
        for(int i = 0; i < CONTAINER_COUNT; i++) {
            if(strcmp(line, CONTAINERS[i]) == 0) {
                running_now[i] = true;
            }
        }
    }

    pclose(fp);

    pthread_mutex_lock(&g_lock);
    for(int i = 0; i < CONTAINER_COUNT; i++) {
        g_container_running[i] = running_now[i];
    }
    bool importing = g_import_active;
    pthread_mutex_unlock(&g_lock);

    /* If importing images, do NOT overwrite the import UI */
    if(importing) return;

    int running_count = 0;
    for(int i = 0; i < CONTAINER_COUNT; i++) {
        if(running_now[i]) running_count++;
    }

    if(running_count == 0) {
        push_ui_step_and_message(SPLASH_STEP_SERVICES, "Starting services...");
        return;
    }

    if(running_count < CONTAINER_COUNT) {
        /* “active” = first not running (checklist style) */
        const char *active = NULL;
        for(int i = 0; i < CONTAINER_COUNT; i++) {
            if(!running_now[i]) {
                active = CONTAINERS[i];
                break;
            }
        }

        if(active) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Starting %s...", friendly_container(active));
            push_ui_step_and_message(SPLASH_STEP_DOCKER, msg);
        } else {
            push_ui_step_and_message(SPLASH_STEP_DOCKER, "Loading containers...");
        }
        return;
    }

    push_ui_step_and_message(SPLASH_STEP_DONE, "Launching user interface...");
}

static void poll_timer_cb(lv_timer_t *t)
{
    (void)t;

    pthread_mutex_lock(&g_lock);
    bool run = g_running;
    pthread_mutex_unlock(&g_lock);
    if(!run) return;

    poll_podman_ps();
}

/* -------- Public API -------- */

void boot_tracker_start(void)
{
    pthread_mutex_lock(&g_lock);
    if(g_running) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    g_running = true;
    pthread_mutex_unlock(&g_lock);

    pthread_create(&g_log_thread, NULL, log_thread_fn, NULL);

    if(!g_poll_timer) {
        g_poll_timer = lv_timer_create(poll_timer_cb, 800, NULL);
    }
}

void boot_tracker_stop(void)
{
    pthread_mutex_lock(&g_lock);
    g_running = false;
    pthread_mutex_unlock(&g_lock);

    if(g_poll_timer) {
        lv_timer_del(g_poll_timer);
        g_poll_timer = NULL;
    }

    pthread_join(g_log_thread, NULL);
}
