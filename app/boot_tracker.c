#include "boot_tracker.h"
#include "ui.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

/* Podkeeper log path on device */
#ifndef PODKEEPER_LOG_PATH
#define PODKEEPER_LOG_PATH "/var/log/podkeeper.log"
#endif

static pthread_t g_thread;
static bool g_started = false;

/* ---------- small helpers ---------- */

static bool file_exists_and_readable(const char *path) {
    return access(path, R_OK) == 0;
}

/* Extract int key=value from a line, returns true if found */
static bool parse_int_kv(const char *s, const char *key, int *out) {
    const char *p = strstr(s, key);
    if(!p) return false;
    p += strlen(key);
    if(*p != '=') return false;
    p++;
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if(end == p) return false;
    *out = (int)v;
    return true;
}

/* Extract first token after event name: e.g. "IMAGE_PLAN foo:tag total_layers=.." -> foo:tag */
static void parse_first_arg(const char *rest, char *dst, size_t dst_sz) {
    if(dst_sz == 0) return;
    dst[0] = '\0';
    while(*rest == ' ') rest++;
    size_t i = 0;
    while(*rest && *rest != ' ' && i + 1 < dst_sz) {
        dst[i++] = *rest++;
    }
    dst[i] = '\0';
}

/* Map PHASE to human-friendly UI subtitle */
static const char *phase_to_subtitle(const char *phase) {
    if(strcmp(phase, "START") == 0) return "Preparing device…";
    if(strcmp(phase, "IMAGE_IMPORT_BEGIN") == 0) return "Preparing system software…";
    if(strcmp(phase, "VERIFY_IMAGES_BEGIN") == 0) return "Verifying system software…";
    if(strcmp(phase, "VERIFY_IMAGES_DONE") == 0) return "Verifying system software…";
    if(strcmp(phase, "COMPOSE_UP_BEGIN") == 0) return "Starting device services…";
    if(strcmp(phase, "CONTAINERS_WAIT_BEGIN") == 0) return "Final system checks…";
    if(strcmp(phase, "DOWN_BEGIN") == 0) return "Stopping services…";
    if(strcmp(phase, "DOWN_DONE") == 0) return "Stopped";
    return "Preparing device…";
}

static void handle_bootprog_line(const char *line) {
    /* Expected format:
       BOOTPROG <EVENT> <REST...>
     */
    const char *pfx = "BOOTPROG ";
    if(strncmp(line, pfx, strlen(pfx)) != 0) return;

    const char *payload = line + strlen(pfx);

    char event[64];
    event[0] = '\0';

    /* split first token */
    const char *sp = strchr(payload, ' ');
    if(sp) {
        size_t n = (size_t)(sp - payload);
        if(n >= sizeof(event)) n = sizeof(event) - 1;
        memcpy(event, payload, n);
        event[n] = '\0';
    } else {
        /* payload contains only event */
        strncpy(event, payload, sizeof(event) - 1);
        event[sizeof(event) - 1] = '\0';
        sp = payload + strlen(payload);
    }

    const char *rest = (*sp == ' ') ? (sp + 1) : sp;

    if(strcmp(event, "PHASE") == 0) {
        char phase[128];
        parse_first_arg(rest, phase, sizeof(phase));
        ui_set_phase_text(phase_to_subtitle(phase));
        return;
    }

    if(strcmp(event, "TOTAL") == 0) {
        /* optional, we can ignore or use later */
        return;
    }

    if(strcmp(event, "IMAGE_IMPORT_START") == 0) {
        char tag[256];
        parse_first_arg(rest, tag, sizeof(tag));
        ui_set_import_status(tag, 0, 0); /* show image tag immediately; total unknown */
        return;
    }

    if(strcmp(event, "IMAGE_PLAN") == 0) {
        char tag[256];
        parse_first_arg(rest, tag, sizeof(tag));
        int tl = 0, tb = 0;
        (void)tb;
        parse_int_kv(rest, "total_layers", &tl);
        parse_int_kv(rest, "total_bytes", &tb);
        ui_set_import_status(tag, 0, tl);
        return;
    }

    if(strcmp(event, "BLOB_DONE") == 0) {
        /* line contains done_layers, total_layers and current image tag at start */
        char tag[256];
        parse_first_arg(rest, tag, sizeof(tag));

        int dl = 0, tl = 0;
        parse_int_kv(rest, "done_layers", &dl);
        parse_int_kv(rest, "total_layers", &tl);

        /* Sometimes podkeeper lines are slightly truncated (you have total_byt6 etc),
           but done_layers/total_layers are present — so we rely on those. */
        ui_set_import_status(tag[0] ? tag : NULL, dl, tl);
        return;
    }

    if(strcmp(event, "IMAGE_IMPORT_DONE") == 0) {
        /* keep last import status; next IMAGE_IMPORT_START will overwrite */
        return;
    }

    if(strcmp(event, "CONTAINER_WAIT") == 0) {
        char name[128];
        parse_first_arg(rest, name, sizeof(name));
        if(name[0]) ui_set_container_active(name);
        return;
    }

    if(strcmp(event, "CONTAINER_RUNNING") == 0) {
        char name[128];
        parse_first_arg(rest, name, sizeof(name));
        if(name[0]) ui_set_container_ok(name);
        return;
    }

    if(strcmp(event, "READY") == 0 || strcmp(event, "DONE") == 0) {
        ui_set_phase_text("Launching user interface…");
        ui_set_overall_percent(100);
        return;
    }

    if(strcmp(event, "FATAL") == 0) {
        ui_set_phase_text("Startup issue detected. Please contact support.");
        return;
    }
}

/* Tail file from end and process new lines, with periodic reopen support */
static void *tracker_thread(void *arg) {
    (void)arg;

    ui_set_phase_text("Waiting for podkeeper…");

    FILE *fp = NULL;
    struct stat st_prev;
    memset(&st_prev, 0, sizeof(st_prev));
    bool have_prev_stat = false;

    char buf[1024];

    for(;;) {
        /* Ensure file exists */
        if(!fp) {
            if(!file_exists_and_readable(PODKEEPER_LOG_PATH)) {
                ui_set_phase_text("Waiting for podkeeper…");
                sleep(1);
                continue;
            }

            fp = fopen(PODKEEPER_LOG_PATH, "r");
            if(!fp) {
                ui_set_phase_text("Waiting for podkeeper…");
                sleep(1);
                continue;
            }

            /* Stat and remember inode; seek end to act like `tail -n 0 -f` */
            struct stat st_now;
            if(stat(PODKEEPER_LOG_PATH, &st_now) == 0) {
                st_prev = st_now;
                have_prev_stat = true;
            }
            fseeko(fp, 0, SEEK_END);

            ui_set_phase_text("Preparing system software…");
        }

        /* Detect log rotation/recreate (inode change) */
        if(have_prev_stat) {
            struct stat st_now;
            if(stat(PODKEEPER_LOG_PATH, &st_now) == 0) {
                if(st_now.st_ino != st_prev.st_ino) {
                    fclose(fp);
                    fp = NULL;
                    have_prev_stat = false;
                    ui_set_phase_text("Waiting for podkeeper…");
                    continue;
                }
            }
        }

        /* Non-blocking-ish follow: try read; if none, sleep and continue */
        if(fgets(buf, sizeof(buf), fp)) {
            /* strip newline */
            size_t n = strlen(buf);
            while(n && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';

            /* Ignore random non-BOOTPROG lines */
            if(strncmp(buf, "BOOTPROG ", 9) == 0) {
                handle_bootprog_line(buf);
            }
        } else {
            /* No new data yet */
            clearerr(fp);
            usleep(200 * 1000);
        }
    }

    return NULL;
}

void boot_tracker_start(void) {
    if(g_started) return;
    g_started = true;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    int rc = pthread_create(&g_thread, &attr, tracker_thread, NULL);
    pthread_attr_destroy(&attr);

    if(rc != 0) {
        /* If thread creation fails, show an error but keep UI alive */
        ui_set_phase_text("Failed to start tracker thread");
    }
}
