/**
 * @file config_store.c
 * @brief Ver config_store.h. En NVS se guarda la config ya normalizada ({"version","config"}),
 *        el mismo formato que entrega la API, así la carga reusa el mismo parser.
 */
#include "config_store.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "config_store";

#define NVS_NAMESPACE "mg_cfg"
#define NVS_KEY       "effective"

static const char *const k_resolutions[] = { "QVGA", "VGA", "SVGA", "XGA", "HD", "SXGA", "UXGA", "QXGA", NULL };
static const char *const k_analysis[] = { "hsv", "hybrid", "model", NULL };
static const char *const k_stream_res[] = { "qvga", "vga", NULL };

static struct {
    SemaphoreHandle_t lock;
    node_config_t defaults;
    node_config_t cur;
} s_cs;

/* ---------------------------------------------------------------- validación por campo */

static int s_invalid;  // campos inválidos en el último parseo (solo se usa bajo s_cs.lock)

static void warn(const char *key)
{
    s_invalid++;
    ESP_LOGW(TAG, "%s inválido o ausente: se usa el valor por defecto", key);
}

static long get_int(const cJSON *o, const char *key, long min, long max, long def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v) || v->valuedouble != (double)(long)v->valuedouble ||
        v->valuedouble < min || v->valuedouble > max) {
        warn(key);
        return def;
    }
    return (long)v->valuedouble;
}

static float get_float(const cJSON *o, const char *key, float min, float max, float def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v) || v->valuedouble < min || v->valuedouble > max) {
        warn(key);
        return def;
    }
    return (float)v->valuedouble;
}

static void get_enum(const cJSON *o, const char *key, const char *const *allowed, const char *def,
                     char *out, size_t out_len)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsString(v)) {
        for (const char *const *a = allowed; *a; a++) {
            if (strcmp(v->valuestring, *a) == 0) {
                strlcpy(out, *a, out_len);
                return;
            }
        }
    }
    warn(key);
    strlcpy(out, def, out_len);
}

static bool valid_hhmm(const char *s)
{
    return s && strlen(s) == 5 && s[2] == ':' && s[0] >= '0' && s[0] <= '2' && s[1] >= '0' &&
           s[1] <= '9' && s[3] >= '0' && s[3] <= '5' && s[4] >= '0' && s[4] <= '9' &&
           (s[0] < '2' || s[1] <= '3');
}

static void get_light_window(const cJSON *o, const node_config_t *def, node_config_t *out)
{
    const cJSON *w = cJSON_GetObjectItemCaseSensitive(o, "light_window");
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(w, "start");
    const cJSON *b = cJSON_GetObjectItemCaseSensitive(w, "end");
    if (cJSON_IsString(a) && cJSON_IsString(b) && valid_hhmm(a->valuestring) && valid_hhmm(b->valuestring)) {
        strlcpy(out->light_start, a->valuestring, sizeof(out->light_start));
        strlcpy(out->light_end, b->valuestring, sizeof(out->light_end));
        return;
    }
    warn("light_window");
    strlcpy(out->light_start, def->light_start, sizeof(out->light_start));
    strlcpy(out->light_end, def->light_end, sizeof(out->light_end));
}

/** Arma una config completa desde el objeto "config" (cada campo inválido → default). */
static void parse_config(const cJSON *c, const node_config_t *d, node_config_t *out)
{
    s_invalid = 0;
    out->capture_interval_min = (uint16_t)get_int(c, "capture_interval_min", 1, 1440, d->capture_interval_min);
    get_light_window(c, d, out);
    out->utc_offset_min = (int16_t)get_int(c, "utc_offset_min", -720, 840, d->utc_offset_min);
    get_enum(c, "analysis_mode", k_analysis, d->analysis_mode, out->analysis_mode, sizeof(out->analysis_mode));
    get_enum(c, "resolution", k_resolutions, d->resolution, out->resolution, sizeof(out->resolution));
    get_enum(c, "resolution_full", k_resolutions, d->resolution_full, out->resolution_full,
             sizeof(out->resolution_full));
    out->jpeg_quality = (uint8_t)get_int(c, "jpeg_quality", 4, 63, d->jpeg_quality);
    out->hsv_suspicion_threshold = get_float(c, "hsv_suspicion_threshold", 0.0f, 1.0f, d->hsv_suspicion_threshold);
    out->min_trigger_interval_s = (uint16_t)get_int(c, "min_trigger_interval_s", 0, 3600, d->min_trigger_interval_s);
    get_enum(c, "stream_resolution_max", k_stream_res, d->stream_resolution_max, out->stream_resolution_max,
             sizeof(out->stream_resolution_max));
    out->stream_fps_max = (uint8_t)get_int(c, "stream_fps_max", 1, 30, d->stream_fps_max);
    out->stream_ttl_max_s = (uint16_t)get_int(c, "stream_ttl_max_s", 10, 3600, d->stream_ttl_max_s);
}

/** {"version": N, "config": {...}} → node_config_t. */
static esp_err_t parse_doc(const char *json, size_t len, const node_config_t *def, node_config_t *out)
{
    cJSON *doc = cJSON_ParseWithLength(json, len);
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(doc, "version");
    const cJSON *cfg = cJSON_GetObjectItemCaseSensitive(doc, "config");
    if (!cJSON_IsNumber(ver) || ver->valuedouble < 0 || !cJSON_IsObject(cfg)) {
        cJSON_Delete(doc);
        return ESP_ERR_INVALID_ARG;
    }
    parse_config(cfg, def, out);
    out->version = (uint32_t)ver->valuedouble;
    cJSON_Delete(doc);
    return ESP_OK;
}

/* ---------------------------------------------------------------- serialización y NVS */

static char *to_json(const node_config_t *c)
{
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddNumberToObject(doc, "version", c->version);
    cJSON *o = cJSON_AddObjectToObject(doc, "config");
    cJSON_AddNumberToObject(o, "capture_interval_min", c->capture_interval_min);
    cJSON *w = cJSON_AddObjectToObject(o, "light_window");
    cJSON_AddStringToObject(w, "start", c->light_start);
    cJSON_AddStringToObject(w, "end", c->light_end);
    cJSON_AddNumberToObject(o, "utc_offset_min", c->utc_offset_min);
    cJSON_AddStringToObject(o, "analysis_mode", c->analysis_mode);
    cJSON_AddStringToObject(o, "resolution", c->resolution);
    cJSON_AddStringToObject(o, "resolution_full", c->resolution_full);
    cJSON_AddNumberToObject(o, "jpeg_quality", c->jpeg_quality);
    cJSON_AddNumberToObject(o, "hsv_suspicion_threshold", c->hsv_suspicion_threshold);
    cJSON_AddNumberToObject(o, "min_trigger_interval_s", c->min_trigger_interval_s);
    cJSON_AddStringToObject(o, "stream_resolution_max", c->stream_resolution_max);
    cJSON_AddNumberToObject(o, "stream_fps_max", c->stream_fps_max);
    cJSON_AddNumberToObject(o, "stream_ttl_max_s", c->stream_ttl_max_s);
    char *s = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    return s;
}

static esp_err_t nvs_save(const node_config_t *c)
{
    char *json = to_json(c);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, NVS_KEY, json);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    free(json);
    return err;
}

static esp_err_t nvs_load(const node_config_t *def, node_config_t *out)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;  // ESP_ERR_NVS_NOT_FOUND la primera vez
    }
    size_t len = 0;
    err = nvs_get_str(h, NVS_KEY, NULL, &len);
    char *json = err == ESP_OK ? malloc(len) : NULL;
    if (err == ESP_OK && !json) {
        err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        err = nvs_get_str(h, NVS_KEY, json, &len);
    }
    nvs_close(h);
    if (err == ESP_OK) {
        err = parse_doc(json, strlen(json), def, out);
    }
    free(json);
    return err;
}

/** Igualdad por contenido (memcmp no sirve: el padding de la struct no está definido). */
static bool same_config(const node_config_t *a, const node_config_t *b)
{
    char *ja = to_json(a), *jb = to_json(b);
    bool eq = ja && jb && strcmp(ja, jb) == 0;
    free(ja);
    free(jb);
    return eq;
}

/* ---------------------------------------------------------------- API */

esp_err_t config_store_init(const node_config_t *defaults)
{
    if (!defaults) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_cs.lock) {
        s_cs.lock = xSemaphoreCreateMutex();
        if (!s_cs.lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    xSemaphoreTake(s_cs.lock, portMAX_DELAY);
    s_cs.defaults = *defaults;
    s_cs.defaults.version = 0;
    s_cs.cur = s_cs.defaults;

    node_config_t loaded;
    esp_err_t err = nvs_load(&s_cs.defaults, &loaded);
    if (err == ESP_OK) {
        s_cs.cur = loaded;
        ESP_LOGI(TAG, "Config v%" PRIu32 " cargada de NVS", loaded.version);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Sin config guardada: valores por defecto del firmware");
    } else {
        ESP_LOGW(TAG, "Config de NVS ilegible (%s): valores por defecto", esp_err_to_name(err));
    }
    xSemaphoreGive(s_cs.lock);
    return ESP_OK;
}

void config_store_get(node_config_t *out)
{
    xSemaphoreTake(s_cs.lock, portMAX_DELAY);
    *out = s_cs.cur;
    xSemaphoreGive(s_cs.lock);
}

uint32_t config_store_version(void)
{
    xSemaphoreTake(s_cs.lock, portMAX_DELAY);
    uint32_t v = s_cs.cur.version;
    xSemaphoreGive(s_cs.lock);
    return v;
}

esp_err_t config_store_apply_json(const char *body, size_t len, bool *changed)
{
    if (changed) {
        *changed = false;
    }
    if (!body || !s_cs.lock) {
        return ESP_ERR_INVALID_ARG;
    }
    node_config_t next;
    xSemaphoreTake(s_cs.lock, portMAX_DELAY);
    esp_err_t err = parse_doc(body, len, &s_cs.defaults, &next);
    int invalid = s_invalid;
    bool diff = err == ESP_OK && !same_config(&next, &s_cs.cur);
    if (diff) {
        s_cs.cur = next;
    }
    xSemaphoreGive(s_cs.lock);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Respuesta de config sin version/config");
        return err;
    }
    if (!diff) {
        return ESP_OK;
    }
    if (invalid) {
        ESP_LOGW(TAG, "Config v%" PRIu32 ": %d campos con valor por defecto", next.version, invalid);
    }
    err = nvs_save(&next);
    if (err != ESP_OK) {
        /* Igual se usa en RAM; al reiniciar sin red volvería la anterior. */
        ESP_LOGE(TAG, "No se pudo guardar la config en NVS: %s", esp_err_to_name(err));
    }
    if (changed) {
        *changed = true;
    }
    return ESP_OK;
}

char *config_store_to_json(void)
{
    node_config_t c;
    config_store_get(&c);
    return to_json(&c);
}
