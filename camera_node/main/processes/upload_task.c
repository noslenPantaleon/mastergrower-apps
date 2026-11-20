/**
 * @file upload_task.c
 * @brief Ver upload_task.h.
 */
#include "upload_task.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mg_api.h"
#include "mg_wifi.h"
#include "storage_queue.h"
#include "system_config.h"

static const char *TAG = "upload_task";

#define UPLOAD_Q_LEN        3        /* jobs en RAM (sin SD) o avisos (NULL) */
#define UPLOAD_IDLE_MS      60000    /* revisa la SD aunque nadie avise */
#define WIFI_WAIT_MS        10000
#define BACKOFF_MIN_S       2
#define BACKOFF_MAX_S       300
#define BACKOFF_AUTH_S      BACKOFF_MAX_S
#define RAM_RETRIES         3
#define RESP_MAX            2048  /* la respuesta trae local_analysis: cortada no se puede parsear */

typedef enum {
    UP_OK,          /* 201 / 200: la API ya la tiene */
    UP_DISCARD,   /* 4xx de datos: no tiene arreglo */
    UP_RETRY,  /* red, 5xx, 404, 429 */
    UP_AUTH,        /* 401 / 403: config del token o node_id */
} up_result_t;

static QueueHandle_t s_q;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static upload_stats_t s_stats;
static uint32_t s_boot_tag;  /* para las claves de idempotencia de jobs en RAM */

#define STATS(expr) do { portENTER_CRITICAL(&s_mux); expr; portEXIT_CRITICAL(&s_mux); } while (0)

void upload_task_get_stats(upload_stats_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_stats;
    portEXIT_CRITICAL(&s_mux);
}

static up_result_t classify(esp_err_t err, int status)
{
    if (err != ESP_OK || status == 0) {
        return UP_RETRY;
    }
    switch (status) {
    case 200:
    case 201:
        return UP_OK;
    case 400:
    case 413:
    case 415:
    case 422:
        return UP_DISCARD;
    case 401:
    case 403:
        return UP_AUTH;
    default:
        return UP_RETRY;  /* 404 (URL mal), 429, 5xx */
    }
}

/** Saca un campo string de la respuesta JSON de la API (capture_id o mensaje de error). */
static void json_field(const char *body, const char *k, char *out, size_t len)
{
    out[0] = '\0';
    cJSON *o = body ? cJSON_Parse(body) : NULL;
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (cJSON_IsString(v)) {
        strlcpy(out, v->valuestring, len);
    }
    cJSON_Delete(o);
}

static up_result_t post(const uint8_t *jpeg, size_t jpeg_len, const char *meta, const char *key,
                        const char *label)
{
    const mg_api_part_t parts[] = {
        { .name = "metadata", .content_type = "application/json", .data = meta, .len = strlen(meta) },
        { .name = "image", .filename = "capture.jpg", .content_type = "image/jpeg", .data = jpeg,
          .len = jpeg_len },
    };
    int64_t t0 = esp_timer_get_time();
    mg_api_resp_t resp;
    esp_err_t err = mg_api_post_multipart("/captures", parts, 2, key, RESP_MAX, &resp);
    int ms = (int)((esp_timer_get_time() - t0) / 1000);
    up_result_t r = classify(err, resp.status);
    STATS(s_stats.last_status = resp.status);

    char txt[64];
    switch (r) {
    case UP_OK:
        json_field(resp.body, "capture_id", txt, sizeof(txt));
        ESP_LOGI(TAG, "%s → %s (%d, %u bytes, %d ms)%s", label, txt[0] ? txt : "?", resp.status,
                 (unsigned)jpeg_len, ms, resp.status == 200 ? " ya estaba en la API" : "");
        break;
    case UP_DISCARD:
    case UP_AUTH:
        json_field(resp.body, "message", txt, sizeof(txt));
        ESP_LOGE(TAG, "%s rechazada por la API: %d %s", label, resp.status, txt);
        break;
    case UP_RETRY:
        if (resp.status) {
            ESP_LOGW(TAG, "%s: la API respondió %d", label, resp.status);
        } else {
            ESP_LOGW(TAG, "%s: sin respuesta de %s (%s)", label, mg_api_base_url(), esp_err_to_name(err));
        }
        break;
    }
    mg_api_resp_free(&resp);
    return r;
}

/* ---------------------------------------------------------------- backoff */

static int64_t s_next_try_us;  /* 0 = ya */

static void backoff_reset(void)
{
    s_next_try_us = 0;
    STATS(s_stats.backoff_s = 0);
}

static void backoff_fail(up_result_t r)
{
    uint32_t b;
    portENTER_CRITICAL(&s_mux);
    s_stats.failures++;
    b = r == UP_AUTH ? BACKOFF_AUTH_S
                     : (s_stats.backoff_s ? s_stats.backoff_s * 2 : BACKOFF_MIN_S);
    if (b > BACKOFF_MAX_S) {
        b = BACKOFF_MAX_S;
    }
    s_stats.backoff_s = b;
    portEXIT_CRITICAL(&s_mux);
    /* ±20 % de azar: varios nodos no reintentan todos a la vez tras un corte de la API */
    int64_t j = (int64_t)b * 1000000 * (80 + (int64_t)(esp_random() % 41)) / 100;
    s_next_try_us = esp_timer_get_time() + j;
    ESP_LOGI(TAG, "Próximo intento en ~%" PRIu32 " s", b);
}

static TickType_t backoff_remaining(void)
{
    int64_t d = s_next_try_us - esp_timer_get_time();
    return d > 0 ? pdMS_TO_TICKS(d / 1000) + 1 : 0;
}

/* ---------------------------------------------------------------- fuentes */

/** Sube una captura de la SD. true si hay que seguir con la próxima sin esperar. */
static bool upload_from_sd(void)
{
    sq_entry_t e;
    if (!storage_queue_ready() || storage_queue_peek(&e) != ESP_OK) {
        return false;
    }
    uint8_t *jpeg = NULL;
    size_t len = 0;
    char *meta = NULL;
    esp_err_t err = storage_queue_read(e.id, &jpeg, &len, &meta);
    if (err != ESP_OK) {
        /* Archivos ilegibles: se descartan para no trabar la cola para siempre. */
        ESP_LOGE(TAG, "No se pudo leer %s de la SD (%s): se descarta", e.id, esp_err_to_name(err));
        storage_queue_remove(e.id);
        STATS(s_stats.discarded++);
        return true;
    }

    up_result_t r = post(jpeg, len, meta, e.id, e.id);
    free(jpeg);
    free(meta);

    switch (r) {
    case UP_OK:
        storage_queue_remove(e.id);
        STATS(s_stats.uploaded++);
        backoff_reset();
        return true;
    case UP_DISCARD:
        storage_queue_remove(e.id);
        STATS(s_stats.discarded++);
        return true;
    default:
        backoff_fail(r);
        return false;
    }
}

/** Sin SD: sube desde RAM con pocos reintentos y libera el job pase lo que pase. */
static void upload_from_ram(capture_job_t *job)
{
    char *meta = pipeline_meta_json(job);
    char key[32], label[24];
    snprintf(key, sizeof(key), "ram-%08" PRIx32 "-%" PRIu32, s_boot_tag, job->seq);
    snprintf(label, sizeof(label), "#%" PRIu32 " (RAM)", job->seq);

    up_result_t r = UP_DISCARD;
    for (int i = 0; meta && i < RAM_RETRIES; i++) {
        if (!mg_wifi_is_connected() && mg_wifi_wait_connected(pdMS_TO_TICKS(WIFI_WAIT_MS)) != ESP_OK) {
            r = UP_RETRY;
            continue;
        }
        r = post(job->jpeg, job->jpeg_len, meta, key, label);
        if (r == UP_OK || r == UP_DISCARD) {
            break;
        }
        STATS(s_stats.failures++);
        vTaskDelay(pdMS_TO_TICKS(2000 * (i + 1)));
    }
    if (r == UP_OK) {
        STATS(s_stats.uploaded++);
    } else {
        ESP_LOGW(TAG, "%s se pierde: no hay SD y la subida falló", label);
        STATS(s_stats.discarded++);
    }
    free(meta);
    pipeline_job_free(job);
}

/* ---------------------------------------------------------------- tarea */

static void upload_task(void *arg)
{
    bool has_more = true;  /* al arrancar puede haber pendientes de antes del reinicio */
    for (;;) {
        TickType_t wait_ticks = backoff_remaining();
        if (!wait_ticks) {
            wait_ticks = has_more ? 0 : pdMS_TO_TICKS(UPLOAD_IDLE_MS);
        }
        capture_job_t *job = NULL;
        if (xQueueReceive(s_q, &job, wait_ticks) == pdTRUE && job) {
            upload_from_ram(job);
            continue;
        }
        if (backoff_remaining()) {
            continue;  /* un aviso de captura nueva no adelanta el reintento */
        }
        if (!mg_wifi_is_connected()) {
            if (mg_wifi_wait_connected(pdMS_TO_TICKS(WIFI_WAIT_MS)) != ESP_OK) {
                has_more = true;
                vTaskDelay(pdMS_TO_TICKS(100));  /* por si el WiFi aún no arrancó */
                continue;
            }
        }
        has_more = upload_from_sd();
    }
}

void upload_task_submit(capture_job_t *job)
{
    if (!job) {
        return;
    }
    if (job->sq_id[0] || !s_q) {
        /* En la SD (la tarea la leerá de ahí) o la subida no está activa. */
        pipeline_job_free(job);
        capture_job_t *wakeup = NULL;
        if (s_q) {
            xQueueSend(s_q, &wakeup, 0);  /* si está llena ya hay un aviso pendiente */
        }
        return;
    }
    if (xQueueSend(s_q, &job, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Cola de subida llena: captura #%" PRIu32 " sin SD se pierde", job->seq);
        STATS(s_stats.discarded++);
        pipeline_job_free(job);
    }
}

esp_err_t upload_task_start(void)
{
    if (s_q) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!CONFIG_MG_API_TOKEN[0]) {
        ESP_LOGW(TAG, "CONFIG_MG_API_TOKEN vacío: la API va a responder 401");
    }
    s_boot_tag = esp_random();
    s_q = xQueueCreate(UPLOAD_Q_LEN, sizeof(capture_job_t *));
    if (!s_q) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(upload_task, "upload_task", SYS_UPLOAD_STACK, NULL,
                                            SYS_UPLOAD_PRIO, NULL, SYS_CORE_NET);
    if (ok != pdPASS) {
        vQueueDelete(s_q);
        s_q = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Subida a %s/captures", mg_api_base_url());
    return ESP_OK;
}
