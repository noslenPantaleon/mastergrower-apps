/**
 * @file thumb_service.c
 * @brief Ver thumb_service.h.
 */
#include "thumb_service.h"

#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "jpeg_decoder.h"
#include "mg_api.h"
#include "mg_time.h"
#include "mg_wifi.h"
#include "system_config.h"

static const char *TAG = "thumbs";

#define QUEUE_LEN        16
#define UPLOAD_WAIT_MS   3000    /**< la cámara sube la captura ~1 s después de tomarla */
#define RETRY_MS         15000
#define MAX_RETRIES      4
#define RECHECK_MS       (5 * 60 * 1000)  /**< revisión periódica de todas las cámaras */
#define LIST_MAX_BODY    4096
#define THUMB_MAX_BODY   (64 * 1024)

typedef struct {
    char node_id[SS_ID_LEN];
    time_t last_capture;      /**< el que vio el modelo la última vez */
    char capture_id[24];      /**< el último que se bajó */
    thumb_t pending;          /**< pixels != NULL: lista para la interfaz */
} slot_t;

static struct {
    QueueHandle_t q;
    SemaphoreHandle_t lock;
    slot_t slots[SS_MAX_NODES];
} s_t;

static slot_t *slot_for(const char *node_id)
{
    slot_t *free_slot = NULL;
    for (int i = 0; i < SS_MAX_NODES; i++) {
        if (strcmp(s_t.slots[i].node_id, node_id) == 0) {
            return &s_t.slots[i];
        }
        if (!free_slot && !s_t.slots[i].node_id[0]) {
            free_slot = &s_t.slots[i];
        }
    }
    if (free_slot) {
        strlcpy(free_slot->node_id, node_id, sizeof(free_slot->node_id));
    }
    return free_slot;
}

/* ---------------------------------------------------------------- avisos del modelo */

/** Corre en la tarea que cambió el modelo (net_task): solo compara y encola. */
static void on_change(ss_change_t change, const char *node_id)
{
    if (change != SS_CHANGE_NODE || !node_id) {
        return;
    }
    ss_node_t n;
    if (!state_store_node(node_id, &n) || n.kind != MG_NODE_CAM || !n.last_capture) {
        return;
    }
    bool changed = false;
    xSemaphoreTake(s_t.lock, portMAX_DELAY);
    slot_t *s = slot_for(node_id);
    if (s && s->last_capture != n.last_capture) {
        s->last_capture = n.last_capture;
        changed = true;
    }
    xSemaphoreGive(s_t.lock);
    if (changed) {
        char id[SS_ID_LEN];
        strlcpy(id, node_id, sizeof(id));
        xQueueSend(s_t.q, id, 0);
    }
}

/* ---------------------------------------------------------------- API */

static bool parse_latest(const char *body, size_t len, thumb_info_t *info)
{
    cJSON *root = cJSON_ParseWithLength(body, len);
    const cJSON *item = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "items"), 0);
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "capture_id");
    bool ok = cJSON_IsString(id);
    if (ok) {
        memset(info, 0, sizeof(*info));
        strlcpy(info->capture_id, id->valuestring, sizeof(info->capture_id));
        const cJSON *ts = cJSON_GetObjectItemCaseSensitive(item, "timestamp");
        if (cJSON_IsString(ts)) {
            mg_time_parse_iso(ts->valuestring, &info->ts);
        }
        const cJSON *tt = cJSON_GetObjectItemCaseSensitive(item, "trigger_type");
        if (cJSON_IsString(tt)) {
            strlcpy(info->trigger_type, tt->valuestring, sizeof(info->trigger_type));
        }
        const cJSON *la = cJSON_GetObjectItemCaseSensitive(item, "local_analysis");
        if (cJSON_IsObject(la)) {
            info->has_analysis = true;
            info->suspicious = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(la, "suspicious"));
            info->dark_scene = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(la, "dark_scene"));
            info->low_plant = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(la, "low_plant_coverage"));
            const cJSON *sc = cJSON_GetObjectItemCaseSensitive(la, "suspicion_score");
            info->score = cJSON_IsNumber(sc) ? (float)sc->valuedouble : 0;
        }
    }
    cJSON_Delete(root);
    return ok;
}

/** JPEG → RGB565 (orden nativo de LVGL) en PSRAM. */
static esp_err_t decode(const uint8_t *jpg, size_t len, thumb_t *out)
{
    esp_jpeg_image_cfg_t cfg = {
        .indata = (uint8_t *)jpg,
        .indata_size = len,
        .out_format = JPEG_IMAGE_FORMAT_RGB888,
        .out_scale = JPEG_IMAGE_SCALE_0,
    };
    esp_jpeg_image_output_t info;
    esp_err_t err = esp_jpeg_get_image_info(&cfg, &info);
    if (err != ESP_OK) {
        return err;
    }
    size_t px = (size_t)info.width * info.height;
    uint8_t *rgb = heap_caps_malloc(px * 3, MALLOC_CAP_SPIRAM);
    uint16_t *out565 = heap_caps_malloc(px * 2, MALLOC_CAP_SPIRAM);
    if (!rgb || !out565) {
        heap_caps_free(rgb);
        heap_caps_free(out565);
        return ESP_ERR_NO_MEM;
    }
    cfg.outbuf = rgb;
    cfg.outbuf_size = px * 3;
    esp_jpeg_image_output_t img;
    err = esp_jpeg_decode(&cfg, &img);
    if (err == ESP_OK) {
        for (size_t i = 0; i < px; i++) {
            const uint8_t *p = &rgb[i * 3];
            out565[i] = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
        }
        out->pixels = out565;
        out->w = img.width;
        out->h = img.height;
    } else {
        heap_caps_free(out565);
    }
    heap_caps_free(rgb);
    return err;
}

/**
 * @return true si terminó (bajó la miniatura o ya estaba al día); false si la subida de la
 *         cámara todavía no llegó (conviene reintentar).
 */
static bool fetch(const char *node_id)
{
    char path[96];
    snprintf(path, sizeof(path), "/captures?node_id=%s&limit=1", node_id);
    mg_api_resp_t resp;
    if (mg_api_get(path, NULL, LIST_MAX_BODY, &resp) != ESP_OK || resp.status != 200) {
        ESP_LOGW(TAG, "%s: GET /captures → %d", node_id, resp.status);
        mg_api_resp_free(&resp);
        return false;
    }
    thumb_info_t info;
    bool have = parse_latest(resp.body, resp.body_len, &info);
    mg_api_resp_free(&resp);
    if (!have) {
        return true;  // la cámara todavía no subió nada nunca
    }

    xSemaphoreTake(s_t.lock, portMAX_DELAY);
    slot_t *s = slot_for(node_id);
    bool same = s && strcmp(s->capture_id, info.capture_id) == 0;
    time_t wanted = s ? s->last_capture : 0;
    xSemaphoreGive(s_t.lock);
    if (same) {
        return info.ts >= wanted;  // misma captura: si es más vieja que la del status, falta subir
    }

    snprintf(path, sizeof(path), "/captures/%s/thumb", info.capture_id);
    if (mg_api_get(path, NULL, THUMB_MAX_BODY, &resp) != ESP_OK || resp.status != 200) {
        ESP_LOGW(TAG, "%s: GET %s → %d", node_id, path, resp.status);
        mg_api_resp_free(&resp);
        return false;
    }
    thumb_t t = { .info = info };
    esp_err_t err = decode((const uint8_t *)resp.body, resp.body_len, &t);
    size_t jpg_len = resp.body_len;
    mg_api_resp_free(&resp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: miniatura inválida (%s)", node_id, esp_err_to_name(err));
        return true;
    }

    xSemaphoreTake(s_t.lock, portMAX_DELAY);
    s = slot_for(node_id);
    if (s) {
        heap_caps_free(s->pending.pixels);  // una anterior que la interfaz no llegó a tomar
        s->pending = t;
        strlcpy(s->capture_id, info.capture_id, sizeof(s->capture_id));
    } else {
        heap_caps_free(t.pixels);
    }
    xSemaphoreGive(s_t.lock);
    ESP_LOGI(TAG, "%s: miniatura %s %ux%u (%u bytes)", node_id, info.capture_id, t.w, t.h,
             (unsigned)jpg_len);
    state_store_notify(SS_CHANGE_THUMB, node_id);
    return info.ts >= wanted;
}

static void thumb_task(void *arg)
{
    TickType_t last_recheck = xTaskGetTickCount();
    for (;;) {
        char id[SS_ID_LEN];
        if (xQueueReceive(s_t.q, id, pdMS_TO_TICKS(RECHECK_MS)) == pdTRUE) {
            mg_wifi_wait_connected(portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(UPLOAD_WAIT_MS));
            for (int i = 0; i < MAX_RETRIES && !fetch(id); i++) {
                vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
            }
        }
        if (xTaskGetTickCount() - last_recheck > pdMS_TO_TICKS(RECHECK_MS)) {
            last_recheck = xTaskGetTickCount();
            static ss_node_t nodes[SS_MAX_NODES];
            int n = state_store_nodes(nodes, SS_MAX_NODES);
            for (int i = 0; i < n; i++) {
                if (nodes[i].kind == MG_NODE_CAM && mg_wifi_is_connected()) {
                    fetch(nodes[i].node_id);
                }
            }
        }
    }
}

esp_err_t thumb_service_start(void)
{
    s_t.q = xQueueCreate(QUEUE_LEN, SS_ID_LEN);
    s_t.lock = xSemaphoreCreateMutex();
    if (!s_t.q || !s_t.lock) {
        return ESP_ERR_NO_MEM;
    }
    state_store_subscribe(on_change);
    BaseType_t ok = xTaskCreatePinnedToCore(thumb_task, "thumbs", SYS_THUMB_STACK, NULL, SYS_THUMB_PRIO,
                                            NULL, SYS_CORE_NET);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool thumb_service_take(const char *node_id, thumb_t *out)
{
    bool got = false;
    xSemaphoreTake(s_t.lock, portMAX_DELAY);
    for (int i = 0; i < SS_MAX_NODES && !got; i++) {
        slot_t *s = &s_t.slots[i];
        if (strcmp(s->node_id, node_id) == 0 && s->pending.pixels) {
            *out = s->pending;
            s->pending.pixels = NULL;
            got = true;
        }
    }
    xSemaphoreGive(s_t.lock);
    return got;
}
