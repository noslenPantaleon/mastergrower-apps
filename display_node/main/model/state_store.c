/**
 * @file state_store.c
 * @brief Ver state_store.h.
 */
#include "state_store.h"

#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mg_time.h"
#include "sdkconfig.h"

static const char *TAG = "state_store";

#define MAX_LISTENERS 4

static struct {
    SemaphoreHandle_t lock;
    ss_node_t nodes[SS_MAX_NODES];
    int n_nodes;
    ss_event_t events[SS_MAX_EVENTS];  /**< anillo */
    int ev_head, ev_count;
    ss_conn_t conn;
    ss_listener_t listeners[MAX_LISTENERS];
    int n_listeners;
} s_ss;

static void notify(ss_change_t c, const char *node_id)
{
    for (int i = 0; i < s_ss.n_listeners; i++) {
        s_ss.listeners[i](c, node_id);
    }
}

void state_store_init(void)
{
    if (!s_ss.lock) {
        s_ss.lock = xSemaphoreCreateMutex();
    }
}

void state_store_subscribe(ss_listener_t cb)
{
    if (cb && s_ss.n_listeners < MAX_LISTENERS) {
        s_ss.listeners[s_ss.n_listeners++] = cb;
    }
}

static void copy_str(char *dst, size_t len, const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(v)) {
        strlcpy(dst, v->valuestring, len);
    } else if (cJSON_IsNull(v)) {
        dst[0] = '\0';
    }
}

static time_t get_time(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    time_t t = 0;
    if (cJSON_IsString(v)) {
        mg_time_parse_iso(v->valuestring, &t);
    }
    return t;
}

static int get_int(const cJSON *obj, const char *key, int def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}

static ss_node_t *find_or_add(const char *node_id, mg_node_kind_t kind)
{
    for (int i = 0; i < s_ss.n_nodes; i++) {
        if (strcmp(s_ss.nodes[i].node_id, node_id) == 0) {
            return &s_ss.nodes[i];
        }
    }
    if (s_ss.n_nodes >= SS_MAX_NODES) {
        return NULL;
    }
    /* Inserción ordenada por node_id: las pantallas lo muestran en ese orden */
    int pos = 0;
    while (pos < s_ss.n_nodes && strcmp(s_ss.nodes[pos].node_id, node_id) < 0) {
        pos++;
    }
    memmove(&s_ss.nodes[pos + 1], &s_ss.nodes[pos], (s_ss.n_nodes - pos) * sizeof(ss_node_t));
    s_ss.n_nodes++;
    ss_node_t *n = &s_ss.nodes[pos];
    memset(n, 0, sizeof(*n));
    strlcpy(n->node_id, node_id, sizeof(n->node_id));
    n->kind = kind;
    n->sd_free_pct = -1;
    n->config_version = -1;
    return n;
}

bool state_store_apply_status(mg_node_kind_t kind, const char *node_id, const char *json, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return false;
    }
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "node_state");
    if (!cJSON_IsString(state)) {
        cJSON_Delete(root);
        return false;
    }
    xSemaphoreTake(s_ss.lock, portMAX_DELAY);
    ss_node_t *n = find_or_add(node_id, kind);
    bool changed = n != NULL;
    if (n) {
        bool offline = strcmp(state->valuestring, "offline") == 0;
        strlcpy(n->node_state, state->valuestring, sizeof(n->node_state));
        copy_str(n->camera_type, sizeof(n->camera_type), root, "camera_type");
        copy_str(n->firmware, sizeof(n->firmware), root, "firmware");
        copy_str(n->zone_id, sizeof(n->zone_id), root, "zone_id");
        if (!offline) {  // el LWT trae datos de cuando se conectó: no pisan los últimos buenos
            n->status_ts = get_time(root, "ts");
            n->last_capture = get_time(root, "last_capture");
            n->sd_free_pct = get_int(root, "sd_free_pct", -1);
            n->rssi = get_int(root, "rssi", 0);
            n->config_version = get_int(root, "config_version", -1);
        }
        n->rx_us = esp_timer_get_time();
    } else {
        ESP_LOGW(TAG, "Tabla de nodos llena: se ignora %s", node_id);
    }
    xSemaphoreGive(s_ss.lock);
    cJSON_Delete(root);
    if (changed) {
        notify(SS_CHANGE_NODE, node_id);
    }
    return changed;
}

bool state_store_apply_event(const char *node_id, const char *json, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return false;
    }
    ss_event_t e = { 0 };
    strlcpy(e.node_id, node_id, sizeof(e.node_id));
    copy_str(e.event_type, sizeof(e.event_type), root, "event_type");
    copy_str(e.severity, sizeof(e.severity), root, "severity");
    e.ts = get_time(root, "ts");
    const cJSON *d = cJSON_GetObjectItemCaseSensitive(root, "detail");
    if (cJSON_IsString(d)) {
        strlcpy(e.detail, d->valuestring, sizeof(e.detail));
    } else if (d && !cJSON_IsNull(d)) {
        char *txt = cJSON_PrintUnformatted(d);
        if (txt) {
            strlcpy(e.detail, txt, sizeof(e.detail));
            cJSON_free(txt);
        }
    }
    cJSON_Delete(root);
    if (!e.event_type[0]) {
        return false;
    }
    xSemaphoreTake(s_ss.lock, portMAX_DELAY);
    s_ss.ev_head = (s_ss.ev_head + SS_MAX_EVENTS - 1) % SS_MAX_EVENTS;
    s_ss.events[s_ss.ev_head] = e;
    if (s_ss.ev_count < SS_MAX_EVENTS) {
        s_ss.ev_count++;
    }
    xSemaphoreGive(s_ss.lock);
    notify(SS_CHANGE_EVENT, node_id);
    return true;
}

void state_store_set_conn(const ss_conn_t *c)
{
    xSemaphoreTake(s_ss.lock, portMAX_DELAY);
    bool changed = memcmp(&s_ss.conn, c, sizeof(*c)) != 0;
    s_ss.conn = *c;
    xSemaphoreGive(s_ss.lock);
    if (changed) {
        notify(SS_CHANGE_CONN, NULL);
    }
}

ss_conn_t state_store_conn(void)
{
    xSemaphoreTake(s_ss.lock, portMAX_DELAY);
    ss_conn_t c = s_ss.conn;
    xSemaphoreGive(s_ss.lock);
    return c;
}

int state_store_nodes(ss_node_t *out, int max)
{
    xSemaphoreTake(s_ss.lock, portMAX_DELAY);
    int n = s_ss.n_nodes < max ? s_ss.n_nodes : max;
    memcpy(out, s_ss.nodes, n * sizeof(ss_node_t));
    xSemaphoreGive(s_ss.lock);
    return n;
}

bool state_store_node(const char *node_id, ss_node_t *out)
{
    bool found = false;
    xSemaphoreTake(s_ss.lock, portMAX_DELAY);
    for (int i = 0; i < s_ss.n_nodes && !found; i++) {
        if (strcmp(s_ss.nodes[i].node_id, node_id) == 0) {
            *out = s_ss.nodes[i];
            found = true;
        }
    }
    xSemaphoreGive(s_ss.lock);
    return found;
}

int state_store_events(ss_event_t *out, int max)
{
    xSemaphoreTake(s_ss.lock, portMAX_DELAY);
    int n = s_ss.ev_count < max ? s_ss.ev_count : max;
    for (int i = 0; i < n; i++) {
        out[i] = s_ss.events[(s_ss.ev_head + i) % SS_MAX_EVENTS];
    }
    xSemaphoreGive(s_ss.lock);
    return n;
}

int64_t state_store_age_s(const ss_node_t *n)
{
    if (mg_time_is_valid() && n->status_ts > 0) {
        int64_t age = (int64_t)time(NULL) - n->status_ts;
        return age < 0 ? 0 : age;
    }
    return (esp_timer_get_time() - n->rx_us) / 1000000;
}

bool state_store_is_stale(const ss_node_t *n)
{
    return strcmp(n->node_state, "offline") != 0 && state_store_age_s(n) > CONFIG_MG_STALE_AFTER_S;
}
