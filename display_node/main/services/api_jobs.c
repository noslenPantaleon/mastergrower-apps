/**
 * @file api_jobs.c
 * @brief Ver api_jobs.h.
 */
#include "api_jobs.h"

#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "jpeg_util.h"
#include "mg_api.h"
#include "system_config.h"

static const char *TAG = "api_jobs";

#define QUEUE_LEN   12
#define PATH_MAX_LEN 160
#define IMAGE_MAX   (96 * 1024)

typedef enum { JOB_GET, JOB_IMAGE, JOB_SEND } job_kind_t;

typedef struct {
    job_kind_t kind;
    char method[8];
    char path[PATH_MAX_LEN];
    char *json;          /**< malloc o NULL */
    size_t max_body;
    api_cb_t cb;
    void *ctx;
} job_t;

typedef struct {
    api_result_t r;
    api_cb_t cb;
    void *ctx;
} done_t;

static QueueHandle_t s_jobs, s_done;

static void worker(void *arg)
{
    for (;;) {
        job_t j;
        xQueueReceive(s_jobs, &j, portMAX_DELAY);
        done_t d = { .cb = j.cb, .ctx = j.ctx };
        mg_api_resp_t resp;
        esp_err_t err;
        if (j.kind == JOB_SEND) {
            err = mg_api_request_json(j.method, j.path, j.json, j.max_body, &resp);
        } else {
            err = mg_api_get(j.path, NULL, j.kind == JOB_IMAGE ? IMAGE_MAX : j.max_body, &resp);
        }
        free(j.json);
        d.r.status = err == ESP_OK ? resp.status : 0;
        if (j.kind == JOB_IMAGE && d.r.status == 200 && resp.body) {
            if (jpeg_to_rgb565((const uint8_t *)resp.body, resp.body_len, &d.r.pixels, &d.r.w, &d.r.h) != ESP_OK) {
                d.r.status = 0;
            }
            mg_api_resp_free(&resp);
        } else {
            d.r.body = resp.body;  // pasa a ser del resultado
            d.r.len = resp.body_len;
        }
        if (d.r.status == 0 || d.r.status >= 400) {
            ESP_LOGW(TAG, "%s %s → %d", j.kind == JOB_SEND ? j.method : "GET", j.path, d.r.status);
        }
        if (xQueueSend(s_done, &d, pdMS_TO_TICKS(1000)) != pdTRUE) {
            free(d.r.body);
            heap_caps_free(d.r.pixels);
        }
    }
}

esp_err_t api_jobs_start(void)
{
    s_jobs = xQueueCreate(QUEUE_LEN, sizeof(job_t));
    s_done = xQueueCreate(QUEUE_LEN, sizeof(done_t));
    if (!s_jobs || !s_done) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(worker, "api_jobs", SYS_API_JOBS_STACK, NULL,
                                                    SYS_API_JOBS_PRIO, NULL, SYS_CORE_NET, MALLOC_CAP_SPIRAM);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t submit(job_kind_t kind, const char *method, const char *path, const char *json,
                        size_t max_body, api_cb_t cb, void *ctx)
{
    job_t j = { .kind = kind, .max_body = max_body, .cb = cb, .ctx = ctx };
    strlcpy(j.method, method ? method : "GET", sizeof(j.method));
    if (strlcpy(j.path, path, sizeof(j.path)) >= sizeof(j.path)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (json) {
        j.json = strdup(json);
        if (!j.json) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (xQueueSend(s_jobs, &j, 0) != pdTRUE) {
        free(j.json);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t api_get(const char *path, size_t max_body, api_cb_t cb, void *ctx)
{
    return submit(JOB_GET, NULL, path, NULL, max_body, cb, ctx);
}

esp_err_t api_get_image(const char *path, api_cb_t cb, void *ctx)
{
    return submit(JOB_IMAGE, NULL, path, NULL, 0, cb, ctx);
}

esp_err_t api_send(const char *method, const char *path, const char *json, api_cb_t cb, void *ctx)
{
    return submit(JOB_SEND, method, path, json, 4096, cb, ctx);
}

void api_jobs_dispatch(void)
{
    done_t d;
    while (s_done && xQueueReceive(s_done, &d, 0) == pdTRUE) {
        if (d.cb) {
            d.cb(&d.r, d.ctx);
        }
        free(d.r.body);
        heap_caps_free(d.r.pixels);
    }
}
