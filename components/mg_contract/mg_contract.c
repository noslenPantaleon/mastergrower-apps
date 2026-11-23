/**
 * @file mg_contract.c
 */
#include "mg_contract.h"

#include <stdio.h>
#include <string.h>
#include "mg_time.h"

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static const char *const k_kind[] = { "cam", "ctrl", "disp" };
static const char *const k_state[] = { "booting", "operational", "autonomous", "error", "offline" };
static const char *const k_sev[] = { "low", "medium", "high" };
static const char *const k_evt[] = { "early_lights_off", "plant_anomaly", "sd_full", "camera_error",
                                     "node_disconnected" };
static const char *const k_trigger[] = { "timer", "sensor_event", "remote_command", "post_actuation",
                                         "schedule" };
static const char *const k_action[] = { "unknown", "capture_now", "refresh_config", "reboot", "start_stream",
                                        "stop_stream", "set_actuator", "set_mode", "ota_update" };
_Static_assert(sizeof(k_action) / sizeof(k_action[0]) == MG_ACTION_COUNT, "k_action y mg_action_t desalineados");
static const char *const k_ack[] = { "accepted", "rejected", "done" };

#define ENUM_STR(table, v) (((unsigned)(v) < ARRAY_LEN(table)) ? (table)[(v)] : "?")

const char *mg_node_state_str(mg_node_state_t v) { return ENUM_STR(k_state, v); }
const char *mg_severity_str(mg_severity_t v) { return ENUM_STR(k_sev, v); }
const char *mg_event_type_str(mg_event_type_t v) { return ENUM_STR(k_evt, v); }
const char *mg_trigger_type_str(mg_trigger_type_t v) { return ENUM_STR(k_trigger, v); }
const char *mg_action_str(mg_action_t v) { return ENUM_STR(k_action, v); }
const char *mg_ack_result_str(mg_ack_result_t v) { return ENUM_STR(k_ack, v); }

mg_action_t mg_action_from_str(const char *s)
{
    if (s) {
        for (size_t i = 1; i < ARRAY_LEN(k_action); i++) {
            if (strcmp(s, k_action[i]) == 0) {
                return (mg_action_t)i;
            }
        }
    }
    return MG_ACTION_UNKNOWN;
}

esp_err_t mg_topic(char *buf, size_t len, mg_node_kind_t kind, const char *node_id, const char *channel)
{
    if (!buf || !node_id || !channel || (unsigned)kind >= ARRAY_LEN(k_kind)) {
        return ESP_ERR_INVALID_ARG;
    }
    int n = snprintf(buf, len, "growlab/%s/%s/%s", k_kind[kind], node_id, channel);
    return (n > 0 && (size_t)n < len) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

cJSON *mg_msg_new(const char *node_id)
{
    cJSON *o = cJSON_CreateObject();
    if (!o) {
        return NULL;
    }
    char ts[MG_TIME_ISO_LEN];
    mg_time_now_iso(ts, sizeof(ts));
    cJSON_AddNumberToObject(o, "schema_version", MG_SCHEMA_VERSION);
    cJSON_AddStringToObject(o, "node_id", node_id);
    cJSON_AddStringToObject(o, "ts", ts);
    return o;
}

char *mg_msg_to_string(cJSON *msg)
{
    if (!msg) {
        return NULL;
    }
    char *s = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    return s;
}

static void add_str_or_null(cJSON *o, const char *k, const char *v)
{
    if (v) {
        cJSON_AddStringToObject(o, k, v);
    } else {
        cJSON_AddNullToObject(o, k);
    }
}

char *mg_msg_cam_status(const char *node_id, const mg_cam_status_t *st)
{
    cJSON *o = mg_msg_new(node_id);
    if (!o || !st) {
        cJSON_Delete(o);
        return NULL;
    }
    cJSON_AddStringToObject(o, "node_state", mg_node_state_str(st->node_state));
    add_str_or_null(o, "camera_type", st->camera_type);
    add_str_or_null(o, "firmware", st->firmware);
    add_str_or_null(o, "last_capture", st->last_capture);
    if (st->sd_free_pct >= 0) {
        cJSON_AddNumberToObject(o, "sd_free_pct", st->sd_free_pct);
    } else {
        cJSON_AddNullToObject(o, "sd_free_pct");
    }
    if (st->rssi != 0) {
        cJSON_AddNumberToObject(o, "rssi", st->rssi);
    } else {
        cJSON_AddNullToObject(o, "rssi");
    }
    add_str_or_null(o, "zone_id", st->zone_id);
    if (st->has_config_version) {
        cJSON_AddNumberToObject(o, "config_version", st->config_version);
    }
    return mg_msg_to_string(o);
}

char *mg_msg_cam_event(const char *node_id, mg_event_type_t type, mg_severity_t sev,
                       const char *capture_id, const char *detail)
{
    cJSON *o = mg_msg_new(node_id);
    if (!o) {
        return NULL;
    }
    cJSON_AddStringToObject(o, "event_type", mg_event_type_str(type));
    cJSON_AddStringToObject(o, "severity", mg_severity_str(sev));
    add_str_or_null(o, "capture_id", capture_id);
    if (detail) {
        cJSON_AddStringToObject(o, "detail", detail);
    }
    return mg_msg_to_string(o);
}

char *mg_msg_cmd_ack(const char *node_id, const char *cmd_id, mg_ack_result_t res, const char *reason)
{
    cJSON *o = mg_msg_new(node_id);
    if (!o) {
        return NULL;
    }
    cJSON_AddStringToObject(o, "cmd_id", cmd_id ? cmd_id : "");
    cJSON_AddStringToObject(o, "result", mg_ack_result_str(res));
    if (reason) {
        cJSON_AddStringToObject(o, "reason", reason);
    }
    return mg_msg_to_string(o);
}

static const char *get_str(const cJSON *o, const char *k)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(it) ? it->valuestring : NULL;
}

esp_err_t mg_cmd_parse(const char *json, size_t len, mg_cmd_t *out)
{
    if (!json || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const char *cmd_id = get_str(root, "cmd_id");
    if (cmd_id) {
        strlcpy(out->cmd_id, cmd_id, sizeof(out->cmd_id));
    }

    esp_err_t err = ESP_OK;
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
    const char *action = get_str(root, "action");

    if (!cJSON_IsNumber(ver)) {
        err = ESP_ERR_INVALID_ARG;
    } else if (ver->valueint != MG_SCHEMA_VERSION) {
        err = ESP_ERR_NOT_SUPPORTED;
    } else if (!cmd_id || cmd_id[0] == '\0' || strlen(cmd_id) > MG_CMD_ID_MAX_LEN || !action) {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK) {
        strlcpy(out->action_raw, action, sizeof(out->action_raw));
        out->action = mg_action_from_str(action);

        const char *sender = get_str(root, "node_id");
        if (sender) {
            strlcpy(out->sender, sender, sizeof(out->sender));
        }
        const char *ts = get_str(root, "ts");
        if (!ts || mg_time_parse_iso(ts, &out->ts) != ESP_OK) {
            out->ts = 0;
        }
        cJSON *params = cJSON_DetachItemFromObjectCaseSensitive(root, "params");
        if (cJSON_IsObject(params)) {
            out->params = params;
        } else {
            cJSON_Delete(params);
        }
    }
    cJSON_Delete(root);
    return err;
}

void mg_cmd_free(mg_cmd_t *cmd)
{
    if (cmd) {
        cJSON_Delete(cmd->params);
        cmd->params = NULL;
    }
}
