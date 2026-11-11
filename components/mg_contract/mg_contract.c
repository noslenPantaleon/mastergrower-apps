/**
 * @file mg_contract.c
 */
#include "mg_contract.h"

#include <stdio.h>
#include <string.h>
#include "mg_time.h"

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static const char *const k_kind[] = { "cam", "ctrl", "disp" };
static const char *const k_estado[] = { "arrancando", "operativo", "autonomo", "error", "fuera_de_linea" };
static const char *const k_sev[] = { "baja", "media", "alta" };
static const char *const k_evt[] = { "luz_apagada_anticipada", "anomalia_planta", "sd_llena", "error_camara",
                                     "nodo_sin_conexion" };
static const char *const k_trigger[] = { "timer", "evento_sensor", "comando_remoto", "post_actuacion",
                                         "calendario" };
static const char *const k_accion[] = { "desconocida", "capture_now", "refresh_config", "reboot", "start_stream",
                                        "stop_stream", "set_actuador", "set_modo" };
static const char *const k_ack[] = { "accepted", "rejected", "done" };

#define ENUM_STR(table, v) (((unsigned)(v) < ARRAY_LEN(table)) ? (table)[(v)] : "?")

const char *mg_estado_nodo_str(mg_estado_nodo_t v) { return ENUM_STR(k_estado, v); }
const char *mg_severidad_str(mg_severidad_t v) { return ENUM_STR(k_sev, v); }
const char *mg_tipo_evento_str(mg_tipo_evento_t v) { return ENUM_STR(k_evt, v); }
const char *mg_trigger_type_str(mg_trigger_type_t v) { return ENUM_STR(k_trigger, v); }
const char *mg_accion_str(mg_accion_t v) { return ENUM_STR(k_accion, v); }
const char *mg_ack_result_str(mg_ack_result_t v) { return ENUM_STR(k_ack, v); }

mg_accion_t mg_accion_from_str(const char *s)
{
    if (s) {
        for (size_t i = 1; i < ARRAY_LEN(k_accion); i++) {
            if (strcmp(s, k_accion[i]) == 0) {
                return (mg_accion_t)i;
            }
        }
    }
    return MG_ACCION_DESCONOCIDA;
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
    cJSON_AddStringToObject(o, "estado_nodo", mg_estado_nodo_str(st->estado_nodo));
    add_str_or_null(o, "camera_type", st->camera_type);
    add_str_or_null(o, "firmware", st->firmware);
    add_str_or_null(o, "ultima_captura", st->ultima_captura);
    if (st->sd_libre_pct >= 0) {
        cJSON_AddNumberToObject(o, "sd_libre_pct", st->sd_libre_pct);
    } else {
        cJSON_AddNullToObject(o, "sd_libre_pct");
    }
    if (st->rssi != 0) {
        cJSON_AddNumberToObject(o, "rssi", st->rssi);
    } else {
        cJSON_AddNullToObject(o, "rssi");
    }
    add_str_or_null(o, "zona_id", st->zona_id);
    return mg_msg_to_string(o);
}

char *mg_msg_cam_event(const char *node_id, mg_tipo_evento_t tipo, mg_severidad_t sev,
                       const char *captura_id, const char *detalle)
{
    cJSON *o = mg_msg_new(node_id);
    if (!o) {
        return NULL;
    }
    cJSON_AddStringToObject(o, "tipo_evento", mg_tipo_evento_str(tipo));
    cJSON_AddStringToObject(o, "severidad", mg_severidad_str(sev));
    add_str_or_null(o, "captura_id", captura_id);
    if (detalle) {
        cJSON_AddStringToObject(o, "detalle", detalle);
    }
    return mg_msg_to_string(o);
}

char *mg_msg_cmd_ack(const char *node_id, const char *cmd_id, mg_ack_result_t res, const char *motivo)
{
    cJSON *o = mg_msg_new(node_id);
    if (!o) {
        return NULL;
    }
    cJSON_AddStringToObject(o, "cmd_id", cmd_id ? cmd_id : "");
    cJSON_AddStringToObject(o, "resultado", mg_ack_result_str(res));
    if (motivo) {
        cJSON_AddStringToObject(o, "motivo", motivo);
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
    const char *accion = get_str(root, "accion");

    if (!cJSON_IsNumber(ver)) {
        err = ESP_ERR_INVALID_ARG;
    } else if (ver->valueint != MG_SCHEMA_VERSION) {
        err = ESP_ERR_NOT_SUPPORTED;
    } else if (!cmd_id || cmd_id[0] == '\0' || strlen(cmd_id) > MG_CMD_ID_MAX_LEN || !accion) {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK) {
        strlcpy(out->accion_raw, accion, sizeof(out->accion_raw));
        out->accion = mg_accion_from_str(accion);

        const char *emisor = get_str(root, "node_id");
        if (emisor) {
            strlcpy(out->emisor, emisor, sizeof(out->emisor));
        }
        const char *ts = get_str(root, "ts");
        if (!ts || mg_time_parse_iso(ts, &out->ts) != ESP_OK) {
            out->ts = 0;
        }
        cJSON *params = cJSON_DetachItemFromObjectCaseSensitive(root, "parametros");
        if (cJSON_IsObject(params)) {
            out->parametros = params;
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
        cJSON_Delete(cmd->parametros);
        cmd->parametros = NULL;
    }
}
