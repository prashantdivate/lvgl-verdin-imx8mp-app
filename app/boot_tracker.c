#include "boot_tracker.h"
#include "ui.h"
#include "lvgl/lvgl.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

/* Config */
#define PODKEEPER_LOG "/var/log/podkeeper.log"
#define CATCHUP_BYTES (1024 * 1024)   /* read last 1MB on startup */

/* State */
static pthread_t g_thr;

static char g_current_image[128];
static int g_total_layers = 0;
static int g_done_layers = 0;

static int g_total_images = 0;
static int g_images_done  = 0;

static int g_overall_pct = 0;

static void *xstrdup0(const char *s) {
    if(!s) return NULL;
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if(!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

static void ui_async_phase(void *p) {
    char *s = (char*)p;
    ui_set_phase_text(s ? s : "");
    free(s);
}
static void ui_async_overall(void *p) {
    int pct = (int)(intptr_t)p;
    ui_set_overall_percent(pct);
}
static void ui_async_import(void *p) {
    /* packed as "tag|done|total" */
    char *s = (char*)p;
    if(!s) return;

    char *tag = s;
    char *a = strchr(s, '|');
    if(!a) { free(s); return; }
    *a++ = 0;

    char *b = strchr(a, '|');
    if(!b) { free(s); return; }
    *b++ = 0;

    int done = atoi(a);
    int total = atoi(b);

    ui_set_import_status(tag, done, total);
    free(s);
}
static void ui_async_active(void *p) {
    char *s = (char*)p;
    ui_set_container_active(s ? s : "");
    free(s);
}
static void ui_async_ok(void *p) {
    char *s = (char*)p;
    ui_set_container_ok(s ? s : "");
    free(s);
}

static long long parse_ll_key(const char *s, const char *key, long long defv) {
    const char *p = strstr(s, key);
    if(!p) return defv;
    p += strlen(key);
    return strtoll(p, NULL, 10);
}

/* Map podkeeper phases to clean UX text */
static const char *phase_text(const char *phase_key) {
    if(!phase_key) return "Preparing device…";
    if(strcmp(phase_key, "START") == 0) return "Preparing device…";
    if(strcmp(phase_key, "IMAGE_IMPORT_BEGIN") == 0) return "Preparing system software…";
    if(strcmp(phase_key, "VERIFY_IMAGES_BEGIN") == 0) return "Verifying system software…";
    if(strcmp(phase_key, "COMPOSE_UP_BEGIN") == 0) return "Starting device services…";
    if(strcmp(phase_key, "CONTAINERS_WAIT_BEGIN") == 0) return "Final system checks…";
    if(strcmp(phase_key, "DONE") == 0) return "Starting…";
    return "Preparing device…";
}

static void recompute_overall(void) {
    /* Simple, stable overall:
       - Images: 65% weighted by images_done/total_images + (layers done of current image)
       - Containers: remaining 35% comes via CONTAINER_RUNNING events (we don’t know total here; keep it minimal)
       If you want container weighting too, add totals from your TOTAL line (containers=) later.
    */
    float img_part = 0.0f;

    if(g_total_images > 0) {
        img_part = (float)g_images_done / (float)g_total_images;
        if(g_total_layers > 0 && g_current_image[0]) {
            float partial = (float)g_done_layers / (float)g_total_layers;
            if(partial < 0) partial = 0;
            if(partial > 1) partial = 1;
            img_part = ((float)g_images_done + partial) / (float)g_total_images;
            if(img_part > 1) img_part = 1;
        }
    }

    int pct = (int)(img_part * 65.0f);
    if(pct < 0) pct = 0;
    if(pct > 100) pct = 100;
    g_overall_pct = pct;

    lv_async_call(ui_async_overall, (void*)(intptr_t)g_overall_pct);
}

/* Handle one BOOTPROG payload line (without the "BOOTPROG " prefix) */
static void handle_bootprog(const char *payload) {
    if(!payload) return;

    /* split event and rest */
    char event[64] = {0};
    const char *sp = strchr(payload, ' ');
    if(sp) {
        size_t n = (size_t)(sp - payload);
        if(n > sizeof(event) - 1) n = sizeof(event) - 1;
        memcpy(event, payload, n);
        event[n] = 0;
        sp++; /* rest */
    } else {
        snprintf(event, sizeof(event), "%s", payload);
        sp = "";
    }

    if(strcmp(event, "TOTAL") == 0) {
        g_total_images = (int)parse_ll_key(sp, "images=", 0);
        recompute_overall();
        return;
    }

    if(strcmp(event, "PHASE") == 0) {
        char phase[96];
        snprintf(phase, sizeof(phase), "%s", sp);
        /* trim */
        for(int i = (int)strlen(phase) - 1; i >= 0; i--) {
            if(phase[i] == '\n' || phase[i] == '\r' || phase[i] == ' ') phase[i] = 0;
            else break;
        }
        lv_async_call(ui_async_phase, xstrdup0(phase_text(phase)));
        return;
    }

    if(strcmp(event, "IMAGE_IMPORT_START") == 0) {
        snprintf(g_current_image, sizeof(g_current_image), "%s", sp);
        /* trim */
        char *e = g_current_image + strlen(g_current_image) - 1;
        while(e >= g_current_image && (*e == '\n' || *e == '\r' || *e == ' ')) *e-- = 0;

        g_total_layers = 0;
        g_done_layers  = 0;

        /* show immediately even if plan isn’t here yet */
        char pack[256];
        snprintf(pack, sizeof(pack), "%s|%d|%d", g_current_image, g_done_layers, g_total_layers);
        lv_async_call(ui_async_import, xstrdup0(pack));
        recompute_overall();
        return;
    }

    if(strcmp(event, "IMAGE_PLAN") == 0) {
        /* Format: <tag> total_layers=.. total_bytes=.. */
        char tag[128] = {0};
        sscanf(sp, "%127s", tag);
        if(tag[0]) snprintf(g_current_image, sizeof(g_current_image), "%s", tag);

        g_total_layers = (int)parse_ll_key(sp, "total_layers=", g_total_layers);
        /* done stays same */
        char pack[256];
        snprintf(pack, sizeof(pack), "%s|%d|%d", g_current_image, g_done_layers, g_total_layers);
        lv_async_call(ui_async_import, xstrdup0(pack));
        recompute_overall();
        return;
    }

    if(strcmp(event, "BLOB_DONE") == 0) {
        /* CRITICAL FIX:
           Your UI might start after IMAGE_IMPORT_START, so g_current_image can be empty.
           But BLOB_DONE always includes the tag as the first token after event. Use it.
        */
        char tag[128] = {0};
        sscanf(sp, "%127s", tag);
        if(tag[0]) snprintf(g_current_image, sizeof(g_current_image), "%s", tag);

        g_done_layers  = (int)parse_ll_key(sp, "done_layers=", g_done_layers);
        g_total_layers = (int)parse_ll_key(sp, "total_layers=", g_total_layers);

        char pack[256];
        snprintf(pack, sizeof(pack), "%s|%d|%d", g_current_image, g_done_layers, g_total_layers);
        lv_async_call(ui_async_import, xstrdup0(pack));
        recompute_overall();
        return;
    }

    if(strcmp(event, "IMAGE_IMPORT_DONE") == 0 || strcmp(event, "IMAGE_IMPORT_EXISTS") == 0) {
        g_images_done++;
        g_current_image[0] = 0;
        g_total_layers = 0;
        g_done_layers = 0;

        /* hide micro progress, keep overall moving */
        lv_async_call(ui_async_import, xstrdup0("|0|0"));
        recompute_overall();
        return;
    }

    if(strcmp(event, "CONTAINER_WAIT") == 0) {
        /* "CONTAINER_WAIT <name> <timeout>" */
        char name[64] = {0};
        sscanf(sp, "%63s", name);
        if(name[0]) lv_async_call(ui_async_active, xstrdup0(name));
        return;
    }

    if(strcmp(event, "CONTAINER_RUNNING") == 0) {
        char name[64] = {0};
        sscanf(sp, "%63s", name);
        if(name[0]) lv_async_call(ui_async_ok, xstrdup0(name));
        return;
    }

    if(strcmp(event, "READY") == 0 || strcmp(event, "DONE") == 0) {
        lv_async_call(ui_async_phase, xstrdup0("Starting…"));
        lv_async_call(ui_async_overall, (void*)(intptr_t)100);
        return;
    }
}

/* feed one line (full line from file) */
static void handle_line(const char *line) {
    if(!line) return;
    const char *p = strstr(line, "BOOTPROG ");
    if(!p) return;
    p += strlen("BOOTPROG ");
    handle_bootprog(p);
}

static void catchup(FILE *fp) {
    if(!fp) return;

    if(fseeko(fp, 0, SEEK_END) != 0) return;
    off_t end = ftello(fp);
    off_t start = end - (off_t)CATCHUP_BYTES;
    if(start < 0) start = 0;

    if(fseeko(fp, start, SEEK_SET) != 0) return;

    /* If we started mid-line, drop the first partial line */
    if(start > 0) {
        char drop[512];
        fgets(drop, sizeof(drop), fp);
    }

    char buf[1024];
    while(fgets(buf, sizeof(buf), fp)) {
        handle_line(buf);
    }
}

static void *tracker_thread(void *arg) {
    (void)arg;

    FILE *fp = fopen(PODKEEPER_LOG, "r");
    if(!fp) {
        lv_async_call(ui_async_phase, xstrdup0("Waiting for podkeeper…"));
        return NULL;
    }

    /* Catch up from tail so late-start UI still gets current image/layers */
    catchup(fp);

    /* Follow */
    while(1) {
        char buf[1024];
        if(fgets(buf, sizeof(buf), fp)) {
            handle_line(buf);
        } else {
            clearerr(fp);
            usleep(200 * 1000);
        }
    }

    fclose(fp);
    return NULL;
}

void boot_tracker_start(void) {
    pthread_create(&g_thr, NULL, tracker_thread, NULL);
    pthread_detach(g_thr);
}
