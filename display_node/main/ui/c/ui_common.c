/**
 * @file ui_common.c
 * @brief Ver ui_common.h.
 */
#include "ui_common.h"

#include <stdio.h>
#include <string.h>
#include "mg_time.h"
#include "sdkconfig.h"
#include "theme.h"

void ui_hhmm(time_t t, char *buf, size_t len)
{
    if (!t) {
        snprintf(buf, len, "--:--");
        return;
    }
    t += (time_t)CONFIG_MG_UTC_OFFSET_MIN * 60;
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(buf, len, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

void ui_ddmm_hhmm(time_t t, char *buf, size_t len)
{
    if (!t) {
        snprintf(buf, len, "--/-- --:--");
        return;
    }
    t += (time_t)CONFIG_MG_UTC_OFFSET_MIN * 60;
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(buf, len, "%02d/%02d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min);
}

time_t ui_parse_iso(const char *iso)
{
    time_t t = 0;
    if (iso) {
        mg_time_parse_iso(iso, &t);
    }
    return t;
}

const char *ui_trigger_title(const char *t)
{
    if (!t) {
        return "-";
    }
    if (strcmp(t, "timer") == 0) {
        return "Programada";
    }
    if (strcmp(t, "remote_command") == 0) {
        return "Manual";
    }
    if (strcmp(t, "sensor_event") == 0) {
        return "Por sensor";
    }
    if (strcmp(t, "post_actuation") == 0) {
        return "Tras actuación";
    }
    if (strcmp(t, "schedule") == 0) {
        return "Calendario";
    }
    return t;
}

void ui_age(int64_t s64, char *buf, size_t len)
{
    int s = s64 > 999999999 ? 999999999 : (int)s64;
    if (s < 60) {
        snprintf(buf, len, "hace %d s", s);
    } else if (s < 3600) {
        snprintf(buf, len, "hace %d min", s / 60);
    } else if (s < 86400) {
        snprintf(buf, len, "hace %d h", s / 3600);
    } else {
        snprintf(buf, len, "hace %d d", s / 86400);
    }
}

bool ui_node_online(const ss_node_t *n)
{
    return !state_store_is_stale(n) &&
           (strcmp(n->node_state, "operational") == 0 || strcmp(n->node_state, "autonomous") == 0 ||
            strcmp(n->node_state, "booting") == 0);
}

void ui_node_chip(const ss_node_t *n, const char **text, uint32_t *bg, uint32_t *fg)
{
    if (strcmp(n->node_state, "offline") == 0) {
        *text = "Sin conexión", *bg = TH_BAD_BG, *fg = TH_BAD_FG;
    } else if (state_store_is_stale(n)) {
        *text = "Dato antiguo", *bg = TH_OFF_BG, *fg = TH_OFF_FG;
    } else if (strcmp(n->node_state, "operational") == 0) {
        *text = "En línea", *bg = TH_OK_BG, *fg = TH_OK_FG;
    } else if (strcmp(n->node_state, "autonomous") == 0) {
        *text = "Autónomo", *bg = TH_WARN_BG, *fg = TH_WARN_FG;
    } else if (strcmp(n->node_state, "booting") == 0) {
        *text = "Arrancando", *bg = TH_WARN_BG, *fg = TH_WARN_FG;
    } else {
        *text = "Error", *bg = TH_BAD_BG, *fg = TH_BAD_FG;
    }
}

const char *ui_event_title(const char *t)
{
    if (strcmp(t, "early_lights_off") == 0) {
        return "Luz apagada antes de tiempo";
    }
    if (strcmp(t, "plant_anomaly") == 0) {
        return "Posible anomalía en la planta";
    }
    if (strcmp(t, "sd_full") == 0) {
        return "Tarjeta SD casi llena";
    }
    if (strcmp(t, "camera_error") == 0) {
        return "Error de cámara";
    }
    if (strcmp(t, "node_disconnected") == 0) {
        return "Nodo sin conexión";
    }
    return t;
}

void ui_severity_chip(const char *sev, const char **text, uint32_t *bg, uint32_t *fg)
{
    if (strcmp(sev, "high") == 0) {
        *text = "alta", *bg = TH_BAD_BG, *fg = TH_BAD_FG;
    } else if (strcmp(sev, "medium") == 0) {
        *text = "media", *bg = TH_WARN_BG, *fg = TH_WARN_FG;
    } else {
        *text = "baja", *bg = TH_OFF_BG, *fg = TH_OFF_FG;
    }
}

int ui_active_alarms(void)
{
    static ss_node_t nodes[SS_MAX_NODES];
    static ss_event_t events[SS_MAX_EVENTS];
    int count = 0;
    int n = state_store_nodes(nodes, SS_MAX_NODES);
    for (int i = 0; i < n; i++) {
        if (strcmp(nodes[i].node_state, "offline") == 0 || strcmp(nodes[i].node_state, "error") == 0) {
            count++;
        }
    }
    time_t now = mg_time_is_valid() ? time(NULL) : 0;
    int e = state_store_events(events, SS_MAX_EVENTS);
    for (int i = 0; i < e; i++) {
        if (strcmp(events[i].severity, "high") == 0 && (!now || now - events[i].ts < 86400)) {
            count++;
        }
    }
    return count;
}
