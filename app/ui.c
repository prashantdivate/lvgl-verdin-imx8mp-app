#include "ui.h"
#include "lvgl/lvgl.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

//#define CMD_RESTART_CHROMIUM "sudo systemctl restart chromium"
#define CMD_REBOOT_DEVICE    "sh -c '(systemctl reboot || reboot) >/tmp/menu-reboot.log 2>&1 &'"
#define CMD_SSH_ON           "systemctl enable --now sshd.socket && sync"
#define CMD_SSH_OFF          "systemctl disable --now sshd.socket && sync"
#define CMD_CLOSE_MENU       "sh -c '(if [ -d /apps/mad-lcp-containers ]; then (cd /apps/mad-lcp-containers && podman-compose -f docker-compose.yml up -d >/tmp/menu-compose.log 2>&1 &); fi; sleep 0.2; pkill -x bootsplash 2>/dev/null; pkill -x lvglsim 2>/dev/null; pkill -x menu 2>/dev/null; true) >/tmp/menu-close.log 2>&1 &'"

#define COL_BG          lv_color_hex(0xF1F3F4)
#define COL_BG_2        lv_color_hex(0xE8EAED)
#define COL_PANEL       lv_color_hex(0xFFFFFF)
#define COL_PANEL_2     lv_color_hex(0xF8F9FA)
#define COL_TEXT        lv_color_hex(0x202124)
#define COL_MUTED       lv_color_hex(0x5F6368)
#define COL_BORDER      lv_color_hex(0xDADCE0)
#define COL_ACCENT      lv_color_hex(0x4285F4)
#define COL_ACCENT_DIM  lv_color_hex(0xE8F0FE)
#define COL_SUCCESS     lv_color_hex(0x188038)
#define COL_SUCCESS_BG  lv_color_hex(0xE6F4EA)
#define COL_DANGER_BG   lv_color_hex(0xFCE8E6)
#define COL_WARN        lv_color_hex(0xF29900)
#define COL_DANGER      lv_color_hex(0xD93025)
#define COL_BTN         lv_color_hex(0xFFFFFF)

#define CONTENT_W       760
#define PANEL_W         360
#define PANEL_H         460
#define BUTTON_W        332
#define BUTTON_H        54
#define SSH_ROW_H       60
#define SLIDER_ROW_H    82
#define INFO_BUF_SIZE   1536
#define MIN_BRIGHTNESS_PERCENT 20

static lv_obj_t *ssh_switch;
static lv_obj_t *ssh_state_label;
static char device_info_cache[INFO_BUF_SIZE];
static lv_obj_t *action_panel;
static lv_obj_t *info_panel;
static lv_obj_t *info_title;
static lv_obj_t *info_body;
static lv_obj_t *info_body_area;
static lv_obj_t *reboot_confirm_button;

typedef enum {
    CONTROL_BRIGHTNESS,
    CONTROL_VOLUME,
    CONTROL_MICROPHONE
} control_type_t;

typedef enum {
    ACTION_NEUTRAL,
    ACTION_SUCCESS,
    ACTION_DANGER
} action_tone_t;

typedef struct {
    control_type_t type;
    lv_obj_t *slider;
    lv_obj_t *value_label;
    int last_value;
    int card;
    char mixer_control[32];
} control_ctx_t;

static control_ctx_t brightness_ctx = { .type = CONTROL_BRIGHTNESS, .last_value = -1, .card = -1 };
static control_ctx_t volume_ctx = { .type = CONTROL_VOLUME, .last_value = -1, .card = -1 };
static control_ctx_t microphone_ctx = { .type = CONTROL_MICROPHONE, .last_value = -1, .card = -1 };
static int brightness_actual_percent;


static void set_status(const char *txt)
{
    LV_UNUSED(txt);
}

static void append_text(char *buf, size_t buf_size, const char *txt)
{
    size_t used;

    if(!buf || !txt || buf_size == 0) {
        return;
    }

    used = strlen(buf);
    if(used < buf_size - 1) {
        snprintf(buf + used, buf_size - used, "%s", txt);
    }
}

static void append_line(char *buf, size_t buf_size, const char *fmt, const char *a, const char *b)
{
    char line[256];
    snprintf(line, sizeof(line), fmt, a ? a : "", b ? b : "");
    append_text(buf, buf_size, line);
}

static int run_cmd(const char *cmd, const char *status)
{
    set_status(status);
    lv_refr_now(NULL);

    if(cmd && cmd[0]) {
        return system(cmd);
    }

    return -1;
}

static int clamp_percent(int value)
{
    if(value < 0) return 0;
    if(value > 100) return 100;
    return value;
}

static bool command_succeeds(const char *cmd)
{
    return system(cmd) == 0;
}

static bool is_ssh_enabled(void)
{
    return command_succeeds("systemctl is-active --quiet sshd.socket 2>/dev/null") ||
           command_succeeds("systemctl is-active --quiet ssh.socket 2>/dev/null") ||
           command_succeeds("systemctl is-active --quiet sshd.service 2>/dev/null") ||
           command_succeeds("systemctl is-active --quiet ssh.service 2>/dev/null");
}

static bool is_virtual_interface(const char *name)
{
    if(!name) {
        return true;
    }

    return strncmp(name, "docker", 6) == 0 ||
           strncmp(name, "podman", 6) == 0 ||
           strncmp(name, "cni", 3) == 0 ||
           strncmp(name, "br-", 3) == 0 ||
           strncmp(name, "veth", 4) == 0 ||
           strncmp(name, "virbr", 5) == 0 ||
           strncmp(name, "tun", 3) == 0 ||
           strncmp(name, "tap", 3) == 0;
}

static bool is_candidate_network_interface(struct ifaddrs *ifa)
{
    if(!ifa || !ifa->ifa_addr) {
        return false;
    }

    if((ifa->ifa_flags & IFF_LOOPBACK) || !(ifa->ifa_flags & IFF_UP)) {
        return false;
    }

    return !is_virtual_interface(ifa->ifa_name);
}

static bool is_network_connected(void)
{
    struct ifaddrs *ifaddr = NULL;
    bool connected = false;

    if(getifaddrs(&ifaddr) == -1) {
        return false;
    }

    for(struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if(!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        if(!is_candidate_network_interface(ifa)) {
            continue;
        }

        connected = true;
        break;
    }

    freeifaddrs(ifaddr);
    return connected;
}

static void sync_ssh_switch_state(void)
{
    bool enabled = is_ssh_enabled();

    if(ssh_switch) {
        if(enabled) {
            lv_obj_add_state(ssh_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(ssh_switch, LV_STATE_CHECKED);
        }
    }

    if(ssh_state_label) {
        lv_label_set_text(ssh_state_label, enabled ? "Enabled" : "Disabled");
        lv_obj_set_style_text_color(ssh_state_label, enabled ? COL_ACCENT : COL_WARN, 0);
    }
}

static void get_network_info(char *buf, size_t buf_size)
{
    struct ifaddrs *ifaddr = NULL;
    char iface[IF_NAMESIZE] = "N/A";
    char ip[INET_ADDRSTRLEN] = "Not assigned";
    char mac[18] = "Not available";
    bool found_iface = false;
    bool found_ip = false;

    if(!buf || buf_size == 0) {
        return;
    }

    if(getifaddrs(&ifaddr) == -1) {
        snprintf(buf, buf_size, "Network\nIP: Not available\nMAC: Not available");
        return;
    }

    for(struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if(!is_candidate_network_interface(ifa)) {
            continue;
        }

        if(!found_iface) {
            snprintf(iface, sizeof(iface), "%s", ifa->ifa_name);
            found_iface = true;
        }

        if(ifa->ifa_addr->sa_family == AF_INET && !found_ip) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
            snprintf(iface, sizeof(iface), "%s", ifa->ifa_name);
            found_ip = true;
            break;
        }
    }

    for(struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if(!ifa->ifa_addr || strcmp(ifa->ifa_name, iface) != 0) {
            continue;
        }

        if(ifa->ifa_addr->sa_family == AF_PACKET) {
            struct sockaddr_ll *s = (struct sockaddr_ll *)ifa->ifa_addr;
            if(s->sll_halen >= 6) {
                snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                         s->sll_addr[0], s->sll_addr[1], s->sll_addr[2],
                         s->sll_addr[3], s->sll_addr[4], s->sll_addr[5]);
                break;
            }
        }
    }

    freeifaddrs(ifaddr);

    snprintf(buf, buf_size, "Network: %s\nIP: %s\nMAC: %s", iface, ip, mac);
}

static bool read_os_release_value(const char *key, char *value, size_t value_size)
{
    FILE *fp = fopen("/etc/os-release", "r");
    char line[256];
    size_t key_len = strlen(key);

    if(!fp || !value || value_size == 0) {
        if(fp) fclose(fp);
        return false;
    }

    while(fgets(line, sizeof(line), fp) != NULL) {
        char *start;
        char *end;

        if(strncmp(line, key, key_len) != 0 || line[key_len] != '=') {
            continue;
        }

        start = line + key_len + 1;
        while(*start == '"' || *start == '\'') {
            start++;
        }

        end = start + strlen(start);
        while(end > start && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == '"' || end[-1] == '\'')) {
            end--;
        }
        *end = '\0';

        snprintf(value, value_size, "%s", start);
        fclose(fp);
        return true;
    }

    fclose(fp);
    return false;
}

static const char *friendly_component_name(const char *repo)
{
    static const char *names[][2] = {
        { "agl-compositor-vivante", "Display service" },
        { "weston-vivante", "Display service" },
        { "chromium-imx8", "Web browser" },
        { "lobby-panel-ems", "Lobby application" },
        { "lobby-panel-ems-db", "Database" },
        { "lobby-panel-ems-asterisk", "Intercom service" },
        { "lobby-panel-ems-nginx", "Web service" }
    };
    const char *base = repo ? strrchr(repo, '/') : NULL;
    base = base ? base + 1 : repo;
    for(size_t i = 0; base && i < sizeof(names) / sizeof(names[0]); i++) {
        if(strcmp(base, names[i][0]) == 0) return names[i][1];
    }
    return "Additional component";
}

static void get_device_info(char *buf, size_t buf_size)
{
    char pretty[160] = "Unknown OS";
    char version[160] = "";
    FILE *fp;
    char line[512];
    int count = 0;

    if(!buf || buf_size == 0) {
        return;
    }

    buf[0] = '\0';
    read_os_release_value("PRETTY_NAME", pretty, sizeof(pretty));
    read_os_release_value("VERSION", version, sizeof(version));

    append_text(buf, buf_size, "Operating System\n");
    append_line(buf, buf_size, "%s\n", pretty, NULL);
    if(version[0]) {
        append_line(buf, buf_size, "%s\n", version, NULL);
    }

    append_text(buf, buf_size, "\nSoftware Components\n");
    fp = popen("podman images --format '{{.Repository}}|{{.Tag}}' 2>/dev/null", "r");
    if(!fp) {
        append_text(buf, buf_size, "Not available\n");
        return;
    }

    while(fgets(line, sizeof(line), fp) != NULL) {
        char *sep = strchr(line, '|');
        char *tag;

        if(!sep) {
            continue;
        }

        *sep = '\0';
        tag = sep + 1;
        tag[strcspn(tag, "\r\n")] = '\0';

        append_line(buf, buf_size, "%s - %s\n", friendly_component_name(line), tag);
        count++;
    }

    pclose(fp);

    if(count == 0) {
        append_text(buf, buf_size, "No images found\n");
    }
}

static int read_int_file(const char *path);
static int get_audio_percent(control_ctx_t *ctx);

static bool get_backlight_path(char *path, size_t path_size)
{
    DIR *dir = opendir("/sys/class/backlight");
    struct dirent *entry;
    char candidate[160];
    char brightness[220];
    char max_brightness[220];

    if(!path || path_size == 0 || !dir) {
        return false;
    }

    while((entry = readdir(dir)) != NULL) {
        if(entry->d_name[0] == '.') {
            continue;
        }

        snprintf(candidate, sizeof(candidate), "/sys/class/backlight/%s", entry->d_name);
        snprintf(brightness, sizeof(brightness), "%s/brightness", candidate);
        snprintf(max_brightness, sizeof(max_brightness), "%s/max_brightness", candidate);

        if(read_int_file(brightness) >= 0 && read_int_file(max_brightness) > 0) {
            snprintf(path, path_size, "%s", candidate);
            closedir(dir);
            return true;
        }
    }

    closedir(dir);
    return false;
}

static int read_int_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    int value = -1;

    if(!fp) {
        return -1;
    }

    if(fscanf(fp, "%d", &value) != 1) {
        value = -1;
    }

    fclose(fp);
    return value;
}

static bool write_int_file(const char *path, int value)
{
    FILE *fp = fopen(path, "w");

    if(!fp) {
        return false;
    }

    fprintf(fp, "%d\n", value);
    fclose(fp);
    return true;
}

static int get_brightness_slider_value(void)
{
    char base[128];
    char path[192];
    int current;
    int max;

    if(!get_backlight_path(base, sizeof(base))) {
        return -1;
    }

    snprintf(path, sizeof(path), "%s/max_brightness", base);
    max = read_int_file(path);
    snprintf(path, sizeof(path), "%s/actual_brightness", base);
    current = read_int_file(path);
    if(current < 0) {
        snprintf(path, sizeof(path), "%s/brightness", base);
        current = read_int_file(path);
    }

    if(max <= 0 || current < 0) {
        return -1;
    }

    int minimum = (MIN_BRIGHTNESS_PERCENT * max + 99) / 100;
    if(current < minimum) {
        snprintf(path, sizeof(path), "%s/brightness", base);
        if(!write_int_file(path, minimum)) return -1;
        current = minimum;
    }
    brightness_actual_percent = (current * 100 + max / 2) / max;
    return max > minimum ? clamp_percent((current - minimum) * 100 / (max - minimum)) : 0;
}

static bool set_brightness_slider_value(int percent)
{
    char base[128];
    char path[192];
    int max;
    int value;

    percent = clamp_percent(percent);

    if(!get_backlight_path(base, sizeof(base))) {
        return false;
    }

    snprintf(path, sizeof(path), "%s/max_brightness", base);
    max = read_int_file(path);
    if(max <= 0) {
        return false;
    }

    /* Slider 0..100 spans only the visible hardware levels, never zero. */
    int minimum = (MIN_BRIGHTNESS_PERCENT * max + 99) / 100;
    value = minimum + (percent * (max - minimum) + 50) / 100;
    snprintf(path, sizeof(path), "%s/brightness", base);
    if(!write_int_file(path, value)) return false;
    brightness_actual_percent = (value * 100 + max / 2) / max;
    return true;
}

static void sync_control_from_system(control_ctx_t *ctx)
{
    int value;

    if(!ctx || !ctx->slider) {
        return;
    }

    value = ctx->type == CONTROL_BRIGHTNESS ? get_brightness_slider_value() : get_audio_percent(ctx);
    if(value < 0) {
        lv_obj_add_state(ctx->slider, LV_STATE_DISABLED);
        lv_obj_set_style_opa(lv_obj_get_parent(ctx->slider), 130, 0);
        ctx->last_value = -1;
        lv_label_set_text(ctx->value_label, "Unavailable");
        return;
    }

    value = clamp_percent(value);
    lv_obj_clear_state(ctx->slider, LV_STATE_DISABLED);
    lv_obj_set_style_opa(lv_obj_get_parent(ctx->slider), LV_OPA_COVER, 0);
    lv_slider_set_value(ctx->slider, value, LV_ANIM_OFF);
    ctx->last_value = value;
    lv_label_set_text_fmt(ctx->value_label, "%d%%", ctx->type == CONTROL_BRIGHTNESS ? brightness_actual_percent : value);
}

static int get_usb_audio_card(bool capture)
{
    /* A microphone-only USB card must not become the playback volume target. */
    for(int card = 0; card < 32; card++) {
        char path[80];
        snprintf(path, sizeof(path), "/proc/asound/card%d/usbid", card);
        FILE *usb = fopen(path, "r");
        if(!usb) continue;
        fclose(usb);
        snprintf(path, sizeof(path), "/proc/asound/card%d", card);
        DIR *directory = opendir(path);
        if(!directory) continue;
        bool playback = false;
        struct dirent *entry;
        while((entry = readdir(directory)) != NULL) {
            size_t length = strlen(entry->d_name);
            if(length > 3 && strncmp(entry->d_name, "pcm", 3) == 0 &&
               entry->d_name[length - 1] == (capture ? 'c' : 'p')) {
                playback = true;
                break;
            }
        }
        closedir(directory);
        if(playback) return card;
    }
    return -1;
}

static int get_audio_percent(control_ctx_t *ctx)
{
    const char *playback_controls[] = { "Master", "PCM", "Speaker", "Headphone" };
    const char *capture_controls[] = { "Mic", "Capture", "Microphone", NULL };
    bool capture = ctx->type == CONTROL_MICROPHONE;
    const char **controls = capture ? capture_controls : playback_controls;
    int card = get_usb_audio_card(capture);
    ctx->card = card;
    ctx->mixer_control[0] = '\0';
    if(card < 0) return -1;

    for(size_t i = 0; i < 4 && controls[i]; i++) {
        char command[128], line[256];
        int volume = -1;
        snprintf(command, sizeof(command), "amixer -c %d get '%s' 2>/dev/null", card, controls[i]);
        FILE *fp = popen(command, "r");
        if(!fp) continue;
        while(fgets(line, sizeof(line), fp) != NULL) {
            char *percent = strchr(line, '%');
            if(percent && strstr(line, capture ? "Capture" : "Playback") && volume < 0) {
                char *start = percent;
                while(start > line && *(start - 1) >= '0' && *(start - 1) <= '9') start--;
                volume = strstr(line, "[off]") ? 0 : atoi(start);
            }
        }
        int status = pclose(fp);
        if(status == 0 && volume >= 0) {
            snprintf(ctx->mixer_control, sizeof(ctx->mixer_control), "%s", controls[i]);
            return clamp_percent(volume);
        }
    }
    return -1;
}

static bool set_audio_percent(control_ctx_t *ctx, int percent)
{
    char command[224];
    bool capture = ctx->type == CONTROL_MICROPHONE;
    percent = clamp_percent(percent);
    if(get_usb_audio_card(capture) != ctx->card || !ctx->mixer_control[0]) {
        if(get_audio_percent(ctx) < 0) return false;
    }
    const char *switch_action = capture ? (percent ? "cap" : "nocap") : (percent ? "unmute" : "mute");
    snprintf(command, sizeof(command), "amixer -c %d set '%s' %s %d%% %s >/tmp/menu-volume.log 2>&1",
             ctx->card, ctx->mixer_control, capture ? "capture" : "playback", percent, switch_action);
    return system(command) == 0;
}

static void usb_audio_changed_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    static int previous_cards[] = { -2, -2 };
    control_ctx_t *controls[] = { &volume_ctx, &microphone_ctx };
    for(int i = 0; i < 2; i++) {
        int card = get_usb_audio_card(i == 1);
        if(card != previous_cards[i]) {
            previous_cards[i] = card;
            sync_control_from_system(controls[i]);
        }
    }
}

static bool apply_control_value(control_ctx_t *ctx, int value)
{
    bool ok;

    if(!ctx) {
        return false;
    }

    value = clamp_percent(value);
    if(ctx->last_value == value) {
        return true;
    }

    if(ctx->type == CONTROL_BRIGHTNESS) {
        ok = set_brightness_slider_value(value);
        set_status(ok ? "Brightness updated" : "Brightness control unavailable");
    } else {
        ok = set_audio_percent(ctx, value);
        set_status(ok ? "Volume updated" : "Volume control unavailable");
    }

    if(ok) {
        ctx->last_value = value;
        lv_label_set_text_fmt(ctx->value_label, "%d%%", ctx->type == CONTROL_BRIGHTNESS ? brightness_actual_percent : value);
    } else {
        sync_control_from_system(ctx);
    }

    return ok;
}

static void control_slider_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *slider = lv_event_get_target(e);
    control_ctx_t *ctx = lv_event_get_user_data(e);
    int value = lv_slider_get_value(slider);

    if(code == LV_EVENT_VALUE_CHANGED && ctx && ctx->type == CONTROL_BRIGHTNESS) {
        apply_control_value(ctx, value);
        return;
    }

    if(code == LV_EVENT_VALUE_CHANGED) {
        if(ctx && ctx->value_label) lv_label_set_text_fmt(ctx->value_label, "%d%%", value);
        return;
    }

    if(code != LV_EVENT_RELEASED) {
        return;
    }

    apply_control_value(ctx, value);
}

static lv_obj_t *make_panel(lv_obj_t *parent);

static void close_dialog_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    fprintf(stderr, "menu: back to actions\n");
    lv_obj_add_flag(info_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(action_panel, LV_OBJ_FLAG_HIDDEN);
}

static void confirm_reboot_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    run_cmd(CMD_REBOOT_DEVICE, "Rebooting device...");
}

static void show_info_dialog(const char *title_txt, const char *body_txt)
{
    fprintf(stderr, "menu: showing %s\n", title_txt);
    /* Reuse the Actions card. A full-screen translucent modal forces an
     * expensive repaint of the software-rendered menu behind it. */
    if(!info_panel) {
        info_panel = make_panel(lv_obj_get_parent(action_panel));
        lv_obj_add_flag(info_panel, LV_OBJ_FLAG_HIDDEN);
        info_title = lv_label_create(info_panel);
        lv_obj_set_width(info_title, LV_PCT(100));
        lv_obj_set_style_text_color(info_title, COL_TEXT, 0);
        lv_obj_set_style_text_font(info_title, &lv_font_montserrat_18, 0);

        lv_obj_t *body_area = lv_obj_create(info_panel);
        info_body_area = body_area;
        lv_obj_set_size(body_area, LV_PCT(100), PANEL_H - 120);
        lv_obj_set_style_bg_opa(body_area, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(body_area, 0, 0);
        lv_obj_set_style_pad_all(body_area, 0, 0);
        lv_obj_set_scrollbar_mode(body_area, LV_SCROLLBAR_MODE_AUTO);
        info_body = lv_label_create(body_area);
        lv_obj_set_width(info_body, LV_PCT(100));
        lv_label_set_long_mode(info_body, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(info_body, COL_MUTED, 0);
        lv_obj_set_style_text_font(info_body, &lv_font_montserrat_14, 0);

        reboot_confirm_button = lv_btn_create(info_panel);
        lv_obj_set_size(reboot_confirm_button, LV_PCT(100), 40);
        lv_obj_set_style_bg_color(reboot_confirm_button, COL_DANGER, 0);
        lv_obj_set_style_shadow_width(reboot_confirm_button, 0, 0);
        lv_obj_add_event_cb(reboot_confirm_button, confirm_reboot_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *confirm_label = lv_label_create(reboot_confirm_button);
        lv_label_set_text(confirm_label, "Restart now");
        lv_obj_center(confirm_label);
        lv_obj_add_flag(reboot_confirm_button, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *back = lv_btn_create(info_panel);
        lv_obj_set_size(back, LV_PCT(100), 40);
        lv_obj_set_style_bg_color(back, COL_ACCENT, 0);
        lv_obj_set_style_shadow_width(back, 0, 0);
        lv_obj_add_event_cb(back, close_dialog_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *label = lv_label_create(back);
        lv_label_set_text(label, LV_SYMBOL_LEFT "  Back");
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_center(label);
    }
    lv_obj_add_flag(reboot_confirm_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(info_body_area, PANEL_H - 120);
    lv_label_set_text(info_title, title_txt);
    lv_label_set_text(info_body, body_txt);
    lv_obj_scroll_to_y(lv_obj_get_parent(info_body), 0, LV_ANIM_OFF);
    lv_obj_add_flag(action_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(info_panel, LV_OBJ_FLAG_HIDDEN);
}

#if 0
static void restart_chromium_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    run_cmd(CMD_RESTART_CHROMIUM, "Restarting Chromium...");
}
#endif

static void reboot_device_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    show_info_dialog("Restart device?", "Applications will close during restart.\n\nSelect Restart now to confirm, or Back to cancel.");
    lv_obj_set_height(info_body_area, PANEL_H - 172);
    lv_obj_remove_flag(reboot_confirm_button, LV_OBJ_FLAG_HIDDEN);
}

static void close_menu_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    run_cmd(CMD_CLOSE_MENU, "Closing menu...");
}

static void network_info_cb(lv_event_t *e)
{
    char info[128];

    LV_UNUSED(e);
    get_network_info(info, sizeof(info));
    set_status(info);
    show_info_dialog("Network Information", info);
}

static void device_info_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    show_info_dialog("Device Information", device_info_cache);
}

static void ssh_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    int rc;

    if(lv_obj_has_state(sw, LV_STATE_CHECKED)) {
        rc = run_cmd(CMD_SSH_ON, "Enabling SSH...");
    } else {
        rc = run_cmd(CMD_SSH_OFF, "Disabling SSH...");
    }

    sync_ssh_switch_state();
    set_status(rc == 0 ? (is_ssh_enabled() ? "SSH Enabled" : "SSH Disabled") : "SSH command failed");
}

static lv_obj_t *make_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, PANEL_W, PANEL_H);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(panel, LV_DIR_NONE);
    lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, COL_BORDER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 22, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_set_style_shadow_opa(panel, 22, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(0x9AA0A6), 0);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_set_style_pad_row(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return panel;
}

static void make_section_title(lv_obj_t *parent, const char *icon, const char *txt)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 32);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(row, LV_DIR_NONE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    lv_obj_t *sym = lv_label_create(row);
    lv_label_set_text(sym, icon);
    lv_obj_set_style_text_color(sym, COL_ACCENT, 0);
    lv_obj_set_style_text_font(sym, &lv_font_montserrat_20, 0);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, txt);
    lv_obj_set_style_text_color(label, COL_TEXT, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
}

static lv_obj_t *make_action_button(lv_obj_t *parent,
                                    const char *icon,
                                    const char *txt,
                                    const char *hint,
                                    lv_event_cb_t cb,
                                    action_tone_t tone)
{
    lv_color_t bg = COL_BTN;
    lv_color_t pressed = lv_color_hex(0xF1F3F4);
    lv_color_t border = COL_BORDER;
    lv_color_t icon_color = COL_ACCENT;
    lv_obj_t *btn = lv_btn_create(parent);

    if(tone == ACTION_SUCCESS) {
        bg = COL_SUCCESS_BG;
        pressed = lv_color_hex(0xCEEAD6);
        border = COL_SUCCESS;
        icon_color = COL_SUCCESS;
    } else if(tone == ACTION_DANGER) {
        bg = COL_DANGER_BG;
        pressed = lv_color_hex(0xFAD2CF);
        border = COL_DANGER;
        icon_color = COL_DANGER;
    }

    lv_obj_set_size(btn, BUTTON_W, BUTTON_H);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(btn, LV_DIR_NONE);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_color(btn, pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, border, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 27, 0);
    lv_obj_set_style_shadow_width(btn, 10, 0);
    lv_obj_set_style_shadow_opa(btn, 18, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0xBDC1C6), 0);
    lv_obj_set_style_pad_left(btn, 14, 0);
    lv_obj_set_style_pad_right(btn, 12, 0);
    lv_obj_set_style_pad_top(btn, 8, 0);
    lv_obj_set_style_pad_bottom(btn, 8, 0);
    lv_obj_set_style_pad_column(btn, 12, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sym = lv_label_create(btn);
    lv_label_set_text(sym, icon);
    lv_obj_set_style_text_color(sym, icon_color, 0);
    lv_obj_set_style_text_font(sym, &lv_font_montserrat_20, 0);
    lv_obj_add_flag(sym, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *texts = lv_obj_create(btn);
    lv_obj_remove_style_all(texts);
    lv_obj_clear_flag(texts, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(texts, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(texts, LV_DIR_NONE);
    lv_obj_add_flag(texts, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(texts, 220, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(texts, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(texts, 2, 0);

    lv_obj_t *title = lv_label_create(texts);
    lv_label_set_text(title, txt);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_add_flag(title, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *sub = lv_label_create(texts);
    lv_label_set_text(sub, hint);
    lv_obj_set_width(sub, 210);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_color(sub, COL_MUTED, 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_add_flag(sub, LV_OBJ_FLAG_EVENT_BUBBLE);

    return btn;
}

static void make_ssh_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, BUTTON_W, SSH_ROW_H);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(row, LV_DIR_NONE);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xEEF0F3), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, SSH_ROW_H / 2, 0);
    lv_obj_set_style_shadow_width(row, 14, 0);
    lv_obj_set_style_shadow_opa(row, 24, 0);
    lv_obj_set_style_shadow_color(row, lv_color_hex(0xBDC1C6), 0);
    lv_obj_set_style_pad_left(row, 22, 0);
    lv_obj_set_style_pad_right(row, 20, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *text_col = lv_obj_create(row);
    lv_obj_remove_style_all(text_col);
    lv_obj_clear_flag(text_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(text_col, LV_DIR_NONE);
    lv_obj_set_size(text_col, 185, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(text_col, 4, 0);

    lv_obj_t *label = lv_label_create(text_col);
    lv_label_set_text(label, "SSH Access");
    lv_obj_set_style_text_color(label, COL_TEXT, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);

    ssh_state_label = lv_label_create(text_col);
    lv_label_set_text(ssh_state_label, "CHECKING");
    lv_obj_set_style_text_color(ssh_state_label, COL_WARN, 0);
    lv_obj_set_style_text_font(ssh_state_label, &lv_font_montserrat_12, 0);

    ssh_switch = lv_switch_create(row);
    lv_obj_set_size(ssh_switch, 58, 32);
    lv_obj_set_style_bg_color(ssh_switch, lv_color_hex(0xBDC1C6), 0);
    lv_obj_set_style_bg_color(ssh_switch, COL_ACCENT_DIM, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(ssh_switch, COL_ACCENT, LV_PART_KNOB | LV_STATE_CHECKED);
    sync_ssh_switch_state();
    lv_obj_add_event_cb(ssh_switch, ssh_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void make_microphone_icon(lv_obj_t *row, lv_align_t align, bool muted)
{
    lv_obj_t *icon = lv_obj_create(row);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, 24, 26);
    lv_obj_align(icon, align, 0, -1);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    /* Capsule, stem and base: no extra icon font is required. */
    const int parts[][5] = { {9, 1, 6, 12, 3}, {11, 18, 2, 5, 0}, {6, 22, 12, 2, 1} };
    for(int i = 0; i < 3; i++) {
        lv_obj_t *part = lv_obj_create(icon);
        lv_obj_remove_style_all(part);
        lv_obj_set_pos(part, parts[i][0], parts[i][1]);
        lv_obj_set_size(part, parts[i][2], parts[i][3]);
        lv_obj_set_style_bg_color(part, COL_TEXT, 0);
        lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(part, parts[i][4], 0);
    }
    lv_obj_t *arc = lv_arc_create(icon);
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, 18, 18);
    lv_obj_set_pos(arc, 3, 3);
    lv_arc_set_bg_angles(arc, 0, 180);
    lv_obj_set_style_arc_color(arc, COL_TEXT, 0);
    lv_obj_set_style_arc_width(arc, 2, 0);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    if(muted) {
        static const lv_point_precise_t slash[] = { {2, 2}, {22, 24} };
        lv_obj_t *line = lv_line_create(icon);
        lv_line_set_points(line, slash, 2);
        lv_obj_set_style_line_color(line, COL_DANGER, 0);
        lv_obj_set_style_line_width(line, 2, 0);
    }
}

static void make_control_slider(lv_obj_t *parent, const char *name,
                                const char *min_icon, const char *max_icon,
                                control_ctx_t *ctx)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, BUTTON_W, SLIDER_ROW_H);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_base_dir(row, LV_BASE_DIR_LTR, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xEEF0F3), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 14, 0);

    lv_obj_t *title = lv_label_create(row);
    lv_label_set_text(title, name);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    ctx->value_label = lv_label_create(row);
    lv_obj_set_style_text_font(ctx->value_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->value_label, COL_ACCENT, 0);
    lv_obj_align(ctx->value_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    if(ctx->type == CONTROL_MICROPHONE) {
        make_microphone_icon(row, LV_ALIGN_BOTTOM_LEFT, true);
        make_microphone_icon(row, LV_ALIGN_BOTTOM_RIGHT, false);
    } else {
        lv_obj_t *left = lv_label_create(row);
        lv_label_set_text(left, min_icon);
        lv_obj_set_style_text_font(left, &lv_font_montserrat_20, 0);
        lv_obj_align(left, LV_ALIGN_BOTTOM_LEFT, 0, -4);
        lv_obj_t *right = lv_label_create(row);
        lv_label_set_text(right, max_icon);
        lv_obj_set_style_text_font(right, &lv_font_montserrat_20, 0);
        lv_obj_align(right, LV_ALIGN_BOTTOM_RIGHT, 0, -4);
    }

    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_size(slider, 220, 10);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_base_dir(slider, LV_BASE_DIR_LTR, 0);
    lv_slider_set_mode(slider, LV_SLIDER_MODE_NORMAL);
    lv_slider_set_range(slider, 0, 100);
    lv_obj_set_ext_click_area(slider, 12);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xC8CCD2), 0);
    lv_obj_set_style_bg_color(slider, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 5, 0);
    lv_obj_set_style_radius(slider, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_width(slider, 26, LV_PART_KNOB);
    lv_obj_set_style_height(slider, 26, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, 13, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, control_slider_cb, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(slider, control_slider_cb, LV_EVENT_RELEASED, ctx);
    ctx->slider = slider;
    sync_control_from_system(ctx);
}

static void recovery_menu_create(void)
{
    lv_obj_t *scr = lv_scr_act();

    lv_obj_clean(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_grad_color(scr, COL_BG_2, 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *accent = lv_obj_create(scr);
    lv_obj_remove_style_all(accent);
    lv_obj_set_size(accent, LV_PCT(100), 4);
    lv_obj_set_style_bg_color(accent, COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "LCP Service Menu");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "Field maintenance console");
    lv_obj_set_style_text_color(subtitle, COL_MUTED, 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, CONTENT_W, PANEL_H);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 132);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *service_panel = make_panel(content);
    make_section_title(service_panel, LV_SYMBOL_SETTINGS, "Services");
    make_ssh_row(service_panel);
    make_control_slider(service_panel, "Brightness", LV_SYMBOL_EYE_CLOSE, LV_SYMBOL_EYE_OPEN, &brightness_ctx);
    make_control_slider(service_panel, "Speaker", LV_SYMBOL_MUTE, LV_SYMBOL_VOLUME_MAX, &volume_ctx);
    make_control_slider(service_panel, "Microphone", "", "", &microphone_ctx);

    action_panel = make_panel(content);
    make_section_title(action_panel, LV_SYMBOL_BARS, "Actions");
    /* make_action_button(action_panel, LV_SYMBOL_REFRESH, "Restart Chromium", "Disabled", restart_chromium_cb, false); */
    make_action_button(action_panel, LV_SYMBOL_DRIVE, "Device Info", "Show OS and image versions",
                       device_info_cb, ACTION_NEUTRAL);
    make_action_button(action_panel, LV_SYMBOL_WIFI, "Network Info", "Show IP and MAC address",
                       network_info_cb, is_network_connected() ? ACTION_SUCCESS : ACTION_DANGER);
    make_action_button(action_panel, LV_SYMBOL_POWER, "Reboot Device", "Restart device",
                       reboot_device_cb, ACTION_DANGER);
    make_action_button(action_panel, LV_SYMBOL_PLAY, "Close Menu", "Return to application",
                       close_menu_cb, ACTION_NEUTRAL);
}

void app_ui_init(void)
{
    /* Query Podman once per menu launch, never from an interaction callback. */
    get_device_info(device_info_cache, sizeof(device_info_cache));
    recovery_menu_create();
    lv_timer_create(usb_audio_changed_cb, 1000, NULL);
}
