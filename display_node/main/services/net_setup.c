/**
 * @file net_setup.c
 * @brief Ver net_setup.h.
 */
#include "net_setup.h"

#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "net_setup";

typedef enum { JOB_SCAN, JOB_SAVE, JOB_RESET } job_kind_t;

typedef struct {
    job_kind_t kind;
    mg_settings_t settings;
} job_t;

static struct {
    QueueHandle_t q;
    mg_wifi_ap_t aps[NET_SETUP_MAX_APS];
    volatile int result;   /**< -2 nada, -1 buscando, ≥0 listo */
    mg_settings_t current;
} s_ns = { .result = -2 };

static void restart_soon(void)
{
    vTaskDelay(pdMS_TO_TICKS(800));  // que se vea el mensaje
    esp_restart();
}

static void worker(void *arg)
{
    for (;;) {
        job_t j;
        xQueueReceive(s_ns.q, &j, portMAX_DELAY);
        switch (j.kind) {
        case JOB_SCAN: {
            int n = mg_wifi_scan(s_ns.aps, NET_SETUP_MAX_APS);
            s_ns.result = n < 0 ? 0 : n;
            ESP_LOGI(TAG, "Búsqueda de redes: %d", n);
            break;
        }
        case JOB_SAVE:
            if (mg_settings_save(&j.settings) == ESP_OK) {
                ESP_LOGW(TAG, "Red y servidor guardados: reinicio");
                restart_soon();
            }
            break;
        case JOB_RESET:
            mg_settings_erase();
            ESP_LOGW(TAG, "Red y servidor borrados (valores de fábrica): reinicio");
            restart_soon();
            break;
        }
    }
}

esp_err_t net_setup_start(const mg_settings_t *current)
{
    s_ns.current = *current;
    s_ns.q = xQueueCreate(2, sizeof(job_t));
    if (!s_ns.q) {
        return ESP_ERR_NO_MEM;
    }
    /* xTaskCreate reserva la pila en RAM interna: esta tarea escribe la flash (NVS) */
    return xTaskCreate(worker, "net_setup", 4096, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void net_setup_scan(void)
{
    job_t j = { .kind = JOB_SCAN };
    s_ns.result = -1;
    if (xQueueSend(s_ns.q, &j, 0) != pdTRUE) {
        s_ns.result = 0;
    }
}

int net_setup_scan_result(mg_wifi_ap_t *out, int max)
{
    int r = s_ns.result;
    if (r >= 0) {
        int n = r < max ? r : max;
        memcpy(out, s_ns.aps, n * sizeof(mg_wifi_ap_t));
        s_ns.result = -2;
        return n;
    }
    return r;
}

void net_setup_save_and_restart(const mg_settings_t *s)
{
    job_t j = { .kind = JOB_SAVE, .settings = *s };
    xQueueSend(s_ns.q, &j, 0);
}

void net_setup_reset_and_restart(void)
{
    job_t j = { .kind = JOB_RESET };
    xQueueSend(s_ns.q, &j, 0);
}

void net_setup_current(mg_settings_t *out)
{
    *out = s_ns.current;
}
