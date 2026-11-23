/**
 * @file mg_ota.c
 * @brief Ver mg_ota.h.
 */
#include "mg_ota.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mg_api.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "sdkconfig.h"

static const char *TAG = "mg_ota";

#define NVS_NS        "mg_ota"
#define NVS_KEY_CMD   "cmd"
#define CHUNK         4096
#define HEAD_BYTES    (32 + sizeof(esp_app_desc_t))   /**< cabecera + segmento + app_desc */
#define URL_MAX       200
#define CMD_ID_MAX    64

typedef struct {
    char cmd_id[CMD_ID_MAX + 1];
    char url[URL_MAX];
    char version[32];
    char sha256[65];
} job_t;

static struct {
    mg_ota_fail_cb_t on_fail;
    mg_ota_confirmed_cb_t on_confirmed;
    volatile bool busy;
    bool pending;
    volatile bool mqtt_ok, api_ok;
    esp_timer_handle_t rollback_timer;
    char rolled_back[CMD_ID_MAX + 1];  /**< ota_update que terminó en vuelta atrás (se avisa al conectar) */
    TaskHandle_t confirm_task;         /**< solo mientras hay una imagen a prueba */
} s_ota;

/* ---------------------------------------------------------------- confirmación y rollback */

static void rollback(void *arg)
{
    ESP_LOGE(TAG, "El firmware nuevo no se conectó en %d s: vuelvo a la versión anterior",
             CONFIG_MG_OTA_CONFIRM_TIMEOUT_S);
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

/* Confirmar escribe la flash (otadata y NVS): con la caché congelada la PSRAM no se puede leer, así
 * que no puede correr en la tarea que avisa (api_jobs, thumbs y LVGL tienen la pila en PSRAM: se
 * caía con "s_task_stack_is_sane_when_cache_frozen"). Lo hace esta tarea, con pila interna. */
static void do_confirm(void)
{
    s_ota.pending = false;
    esp_timer_stop(s_ota.rollback_timer);
    esp_ota_mark_app_valid_cancel_rollback();
    char cmd_id[CMD_ID_MAX + 1] = "";
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        size_t len = sizeof(cmd_id);
        nvs_get_str(h, NVS_KEY_CMD, cmd_id, &len);
        nvs_erase_key(h, NVS_KEY_CMD);
        nvs_commit(h);
        nvs_close(h);
    }
    const char *ver = esp_app_get_description()->version;
    ESP_LOGI(TAG, "Firmware %s confirmado (rollback cancelado)", ver);
    if (s_ota.on_confirmed) {
        s_ota.on_confirmed(cmd_id, ver);
    }
}

static void confirm_task(void *arg)
{
    while (!(s_ota.mqtt_ok && s_ota.api_ok)) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    do_confirm();
    s_ota.confirm_task = NULL;
    vTaskDelete(NULL);
}

static void try_confirm(void)
{
    if (s_ota.pending && s_ota.confirm_task) {
        xTaskNotifyGive(s_ota.confirm_task);
    }
}

void mg_ota_note_mqtt_ok(void)
{
    s_ota.mqtt_ok = true;
    if (s_ota.rolled_back[0] && s_ota.on_fail) {
        s_ota.on_fail(s_ota.rolled_back, "rolled_back");  // la API se entera de que no quedó
        s_ota.rolled_back[0] = '\0';
    }
    try_confirm();
}

void mg_ota_note_api_ok(void)
{
    s_ota.api_ok = true;
    try_confirm();
}

bool mg_ota_pending(void)
{
    return s_ota.pending;
}

esp_err_t mg_ota_init(mg_ota_fail_cb_t on_fail, mg_ota_confirmed_cb_t on_confirmed)
{
    s_ota.on_fail = on_fail;
    s_ota.on_confirmed = on_confirmed;
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        s_ota.pending = true;
        /* Pila interna (xTaskCreate la reserva en RAM interna): esta tarea escribe la flash */
        if (xTaskCreate(confirm_task, "ota_confirm", 4096, NULL, 2, &s_ota.confirm_task) != pdPASS) {
            ESP_LOGE(TAG, "Sin memoria para la tarea de confirmación: el firmware volverá atrás");
        }
        const esp_timer_create_args_t args = { .callback = rollback, .name = "ota_rollback" };
        esp_err_t err = esp_timer_create(&args, &s_ota.rollback_timer);
        if (err == ESP_OK) {
            err = esp_timer_start_once(s_ota.rollback_timer, (uint64_t)CONFIG_MG_OTA_CONFIRM_TIMEOUT_S * 1000000);
        }
        ESP_LOGW(TAG, "Firmware %s a prueba en %s: se confirma al conectar (o rollback en %d s)",
                 esp_app_get_description()->version, running->label, CONFIG_MG_OTA_CONFIRM_TIMEOUT_S);
        return err;
    }
    /* Imagen ya confirmada pero con un cmd_id guardado: el ota_update instaló una versión que no
     * se pudo confirmar y el bootloader volvió a esta. */
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        size_t len = sizeof(s_ota.rolled_back);
        if (nvs_get_str(h, NVS_KEY_CMD, s_ota.rolled_back, &len) == ESP_OK) {
            ESP_LOGW(TAG, "La actualización %s no se confirmó: se volvió a %s", s_ota.rolled_back,
                     esp_app_get_description()->version);
            nvs_erase_key(h, NVS_KEY_CMD);
            nvs_commit(h);
        } else {
            s_ota.rolled_back[0] = '\0';
        }
        nvs_close(h);
    }
    return ESP_OK;
}

/* ---------------------------------------------------------------- descarga */

static void fail(job_t *j, const char *reason)
{
    ESP_LOGE(TAG, "OTA %s falló: %s", j->version, reason);
    if (s_ota.on_fail) {
        s_ota.on_fail(j->cmd_id, reason);
    }
}

static void hex(const uint8_t *b, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) {
        sprintf(out + 2 * i, "%02x", b[i]);
    }
}

/** @return NULL si todo bien, o el motivo de falla. Si devuelve NULL, el nodo reinicia. */
static const char *run(job_t *j)
{
    char url[URL_MAX];
    if (mg_api_url(j->url, url, sizeof(url)) != ESP_OK) {
        return "invalid_format";
    }
    esp_http_client_config_t cfg = { .url = url, .timeout_ms = 15000, .buffer_size = CHUNK,
                                     .keep_alive_enable = false };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        return "out_of_memory";
    }
    char auth[160];
    snprintf(auth, sizeof(auth), "Bearer %s", mg_api_token());
    esp_http_client_set_header(c, "Authorization", auth);

    const char *why = NULL;
    uint8_t *buf = malloc(CHUNK);
    esp_ota_handle_t ota = 0;
    bool ota_open = false;
    size_t total = 0, have = 0;
    psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
    int64_t len = -1;

    if (!buf || psa_crypto_init() != PSA_SUCCESS || psa_hash_setup(&sha, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        why = "out_of_memory";
    } else if (esp_http_client_open(c, 0) != ESP_OK || (len = esp_http_client_fetch_headers(c)) < 0 ||
               esp_http_client_get_status_code(c) != 200) {
        ESP_LOGE(TAG, "GET %s → %d", url, esp_http_client_get_status_code(c));
        why = "download_failed";
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!why && !part) {
        why = "no_partition";
    }
    ESP_LOGI(TAG, "Descargando %s (%lld bytes) → %s", j->version, (long long)len, part ? part->label : "?");
    int64_t t0 = esp_timer_get_time(), last_log = t0;

    while (!why) {
        int r = esp_http_client_read(c, (char *)buf + have, CHUNK - have);
        if (r < 0) {
            why = "download_failed";
            break;
        }
        have += r;
        bool eof = r == 0 || esp_http_client_is_complete_data_received(c);
        if (!ota_open) {
            if (have < HEAD_BYTES && !eof) {
                continue;  // juntar la cabecera antes de decidir
            }
            if (have < HEAD_BYTES) {
                why = "download_failed";
                break;
            }
            const esp_app_desc_t *nd = (const esp_app_desc_t *)(buf + 32);
            const esp_app_desc_t *cur = esp_app_get_description();
            if (nd->magic_word != ESP_APP_DESC_MAGIC_WORD || strcmp(nd->project_name, cur->project_name) != 0) {
                ESP_LOGE(TAG, "La imagen es de \"%.32s\", no de \"%s\"", nd->project_name, cur->project_name);
                why = "wrong_project";
                break;
            }
            if (strcmp(nd->version, cur->version) == 0) {
                why = "same_version";
                break;
            }
            if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
                why = "flash_error";
                break;
            }
            ota_open = true;
        }
        if (have) {
            if (esp_ota_write(ota, buf, have) != ESP_OK) {
                why = "flash_error";
                break;
            }
            psa_hash_update(&sha, buf, have);
            total += have;
            have = 0;
        }
        int64_t now = esp_timer_get_time();
        if (now - last_log > 2000000) {
            last_log = now;
            ESP_LOGI(TAG, "  %u KB", (unsigned)(total / 1024));
        }
        if (eof) {
            break;
        }
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    free(buf);

    if (!why) {
        uint8_t digest[32];
        size_t dlen = 0;
        char got[65];
        psa_hash_finish(&sha, digest, sizeof(digest), &dlen);
        hex(digest, sizeof(digest), got);
        if (strcasecmp(got, j->sha256) != 0) {
            ESP_LOGE(TAG, "SHA-256 distinto: %s (esperado %s)", got, j->sha256);
            why = "bad_checksum";
        }
    } else {
        psa_hash_abort(&sha);
    }
    if (why) {
        if (ota_open) {
            esp_ota_abort(ota);
        }
        return why;
    }
    if (esp_ota_end(ota) != ESP_OK) {
        return "invalid_image";
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        return "flash_error";
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, NVS_KEY_CMD, j->cmd_id);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Firmware %s grabado (%u KB en %.1f s): reinicio", j->version, (unsigned)(total / 1024),
             (esp_timer_get_time() - t0) / 1e6);
    return NULL;
}

static void ota_task(void *arg)
{
    job_t *j = arg;
    const char *why = run(j);
    if (why) {
        fail(j, why);
        s_ota.busy = false;
    } else {
        vTaskDelay(pdMS_TO_TICKS(1000));  // que salgan los logs y el status
        esp_restart();
    }
    free(j);
    vTaskDelete(NULL);
}

esp_err_t mg_ota_start(const mg_ota_req_t *req, char *reason, size_t reason_len)
{
    if (!req || !req->cmd_id || !req->url || !req->version || !req->sha256 || strlen(req->sha256) != 64 ||
        strlen(req->url) >= URL_MAX || strlen(req->version) >= 32 || strlen(req->cmd_id) > CMD_ID_MAX) {
        snprintf(reason, reason_len, "invalid_format");
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(req->version, esp_app_get_description()->version) == 0) {
        snprintf(reason, reason_len, "same_version");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_ota.busy || s_ota.pending) {
        snprintf(reason, reason_len, "busy");  // otra en curso, o la actual todavía no se confirmó
        return ESP_ERR_INVALID_STATE;
    }
    job_t *j = calloc(1, sizeof(*j));
    if (!j) {
        snprintf(reason, reason_len, "out_of_memory");
        return ESP_ERR_NO_MEM;
    }
    strlcpy(j->cmd_id, req->cmd_id, sizeof(j->cmd_id));
    strlcpy(j->url, req->url, sizeof(j->url));
    strlcpy(j->version, req->version, sizeof(j->version));
    strlcpy(j->sha256, req->sha256, sizeof(j->sha256));
    s_ota.busy = true;
    /* Pila interna: esta tarea escribe la flash (no puede tener la pila en PSRAM) */
    if (xTaskCreatePinnedToCore(ota_task, "ota", 6144, j, 3, NULL, 0) != pdPASS) {
        s_ota.busy = false;
        free(j);
        snprintf(reason, reason_len, "out_of_memory");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
