/**
 * @file app_main.c
 * @brief Nodo de cámara — arranque: config (NVS, C6), microSD, cámara, pipeline, WiFi,
 *        net_task (MQTT), config_sync, upload_task y, en desarrollo, el servidor de depuración.
 *
 * Las fases siguientes agregan aquí el arranque de las tareas del pipeline
 * (trigger_manager, capture_task, analysis_task, upload_task, ...).
 */
#include <inttypes.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "app_state.h"
#include "config_store.h"
#include "default_params.h"
#include "hal_camera.h"
#include "hal_storage.h"
#include "storage_queue.h"
#include "mg_time.h"
#include "mg_wifi.h"
#include "processes/analysis_task.h"
#include "processes/capture_task.h"
#include "processes/config_sync_task.h"
#include "processes/net_task.h"
#include "processes/pipeline.h"
#include "processes/trigger_manager.h"
#include "processes/upload_task.h"
#include "system_config.h"

#if CONFIG_MG_DEBUG_HTTP
#include "debug/debug_http.h"
#endif

static const char *TAG = "app_main";

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS sin espacio o de otra versión: se borra");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static void log_memory(const char *when)
{
    ESP_LOGI(TAG, "[mem %s] interna libre=%u (mín %u) · PSRAM libre=%u (mín %u)", when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
}

/** Valores por defecto del firmware: último nivel de override → global → default (Contrato §7). */
static void init_config(void)
{
    node_config_t def = {
        .capture_interval_min = DEF_CAPTURE_INTERVAL_MIN,
        .utc_offset_min = DEF_UTC_OFFSET_MIN,
        .jpeg_quality = DEF_JPEG_QUALITY,
        .hsv_suspicion_threshold = DEF_HSV_SUSPICION_THRESHOLD,
        .min_trigger_interval_s = DEF_MIN_TRIGGER_INTERVAL_S,
        .stream_fps_max = DEF_STREAM_FPS_MAX,
        .stream_ttl_max_s = DEF_STREAM_TTL_MAX_S,
    };
    strlcpy(def.light_start, DEF_LIGHT_WINDOW_START, sizeof(def.light_start));
    strlcpy(def.light_end, DEF_LIGHT_WINDOW_END, sizeof(def.light_end));
    strlcpy(def.analysis_mode, DEF_ANALYSIS_MODE, sizeof(def.analysis_mode));
    strlcpy(def.resolution, DEF_RESOLUTION, sizeof(def.resolution));
    strlcpy(def.resolution_full, DEF_RESOLUTION_FULL, sizeof(def.resolution_full));
    strlcpy(def.stream_resolution_max, DEF_STREAM_RES_MAX, sizeof(def.stream_resolution_max));
    ESP_ERROR_CHECK(config_store_init(&def));
}

static esp_err_t init_camera(void)
{
    node_config_t c;
    config_store_get(&c);  // la última config guardada (o los defaults)
    hal_camera_config_t cfg = HAL_CAMERA_CONFIG_DEFAULT();
    if (!hal_camera_res_from_name(c.resolution, &cfg.resolution)) {
        ESP_LOGW(TAG, "Resolución \"%s\" inválida, se usa SVGA", c.resolution);
        cfg.resolution = HAL_CAM_RES_SVGA;
    }
    cfg.jpeg_quality = c.jpeg_quality;

    esp_err_t err = hal_camera_init(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    /* La exposición automática necesita algunos cuadros para estabilizarse. */
    return hal_camera_discard_frames(3);
}

/** Captura N cuadros y registra tiempos y tamaños: base para dimensionar el pipeline. */
static void camera_selftest(int n)
{
    if (n <= 0) {
        return;
    }
    int64_t t_min = INT64_MAX, t_max = 0, t_sum = 0;
    size_t sz_min = SIZE_MAX, sz_max = 0;
    int ok = 0;
    uint16_t w = 0, h = 0;

    for (int i = 0; i < n; i++) {
        hal_frame_t f;
        int64_t t0 = esp_timer_get_time();
        if (hal_camera_capture(&f, pdMS_TO_TICKS(3000)) != ESP_OK) {
            continue;
        }
        int64_t dt = esp_timer_get_time() - t0;
        w = f.width;
        h = f.height;
        sz_min = f.len < sz_min ? f.len : sz_min;
        sz_max = f.len > sz_max ? f.len : sz_max;
        hal_camera_release(&f);

        t_min = dt < t_min ? dt : t_min;
        t_max = dt > t_max ? dt : t_max;
        t_sum += dt;
        ok++;
    }

    if (ok == 0) {
        ESP_LOGE(TAG, "Autoprueba: 0/%d capturas correctas", n);
        return;
    }
    ESP_LOGI(TAG, "Autoprueba %dx%u: %d/%d OK · tiempo min/prom/max = %" PRId64 "/%" PRId64 "/%" PRId64
             " ms · tamaño %u–%u bytes", w, h, ok, n, t_min / 1000, t_sum / ok / 1000, t_max / 1000,
             (unsigned)sz_min, (unsigned)sz_max);
}

/* ------------------------------------------------------------------ comandos (C2) */

static bool s_pipeline_ok;

/**
 * capture_now → disparo remote_command en el trigger_manager. El ack "done" lo envía
 * capture_task cuando la captura termina (respuesta asíncrona).
 */
static esp_err_t cmd_capture_now(const mg_cmd_t *cmd, char *reason, size_t reason_len)
{
    if (!s_pipeline_ok) {
        snprintf(reason, reason_len, "camera_error");
        return ESP_ERR_INVALID_STATE;
    }
    trigger_req_t req = { .type = MG_TRIGGER_REMOTE_COMMAND };
    strlcpy(req.cmd_id, cmd->cmd_id, sizeof(req.cmd_id));

    const char *why = NULL;
    esp_err_t err = trigger_request(&req, &why);
    if (err != ESP_OK) {
        snprintf(reason, reason_len, "%s", why ? why : esp_err_to_name(err));
        return err;
    }
    return ESP_ERR_NOT_FINISHED;
}

/** microSD + cola de pendientes. Sin tarjeta el nodo sigue, sin buffer local. */
static void init_storage(void)
{
    if (hal_storage_init() != ESP_OK) {
        return;
    }
    esp_err_t err = storage_queue_init(HAL_STORAGE_ROOT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage_queue no disponible: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Buffer local: %u capturas pendientes, %d%% libre", (unsigned)storage_queue_count(),
             hal_storage_free_pct());
}

static esp_err_t start_pipeline(void)
{
    node_config_t c;
    config_store_get(&c);
    trigger_config_t tcfg;
    config_sync_to_trigger(&c, &tcfg);

    esp_err_t err = pipeline_init();
    if (err == ESP_OK) {
        err = analysis_task_start();
    }
    if (err == ESP_OK) {
        err = capture_task_start();
    }
    if (err == ESP_OK) {
        err = trigger_manager_start(&tcfg);
    }
    return err;
}

static void reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));  // margen para que salga el ack
    esp_restart();
}

static esp_err_t cmd_reboot(const mg_cmd_t *cmd, char *reason, size_t reason_len)
{
    ESP_LOGW(TAG, "Reinicio pedido por %s", cmd->sender);
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 1, NULL);
    return ESP_ERR_NOT_FINISHED;  // solo "accepted": el nodo se reinicia antes de un "done"
}

/* ------------------------------------------------------------------ red */

static void on_wifi_connected(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    char ip[16];
    mg_wifi_get_ip_str(ip, sizeof(ip));
#if CONFIG_MG_DEBUG_HTTP
    ESP_LOGI(TAG, "Vista previa: http://%s:%d/  (RSSI %d dBm)", ip, CONFIG_MG_DEBUG_HTTP_PORT,
             mg_wifi_get_rssi());
#else
    ESP_LOGI(TAG, "IP %s (RSSI %d dBm)", ip, mg_wifi_get_rssi());
#endif
}

static esp_err_t start_network(void)
{
    ESP_ERROR_CHECK(esp_event_handler_register(MG_WIFI_EVENT, MG_WIFI_EVENT_CONNECTED,
                                               on_wifi_connected, NULL));
    const mg_wifi_config_t wcfg = { .hostname = SYS_NODE_ID };
    esp_err_t err = mg_wifi_start(&wcfg);
    if (err != ESP_OK) {
        /* Sin WiFi el nodo igual debería capturar y guardar en SD (modo autónomo, C4). */
        ESP_LOGE(TAG, "WiFi no iniciado (%s): nodo sin red", esp_err_to_name(err));
        app_state_set_state(MG_STATE_AUTONOMOUS);
        return err;
    }

    net_task_register_cmd(MG_ACTION_CAPTURE_NOW, cmd_capture_now);
    net_task_register_cmd(MG_ACTION_REBOOT, cmd_reboot);
    ESP_ERROR_CHECK(net_task_start());
    /* Config remota: al arrancar, con refresh_config y periódica. Sin red queda la de NVS. */
    err = config_sync_task_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_sync no iniciada: %s", esp_err_to_name(err));
    }
    /* Sube lo pendiente de la SD aunque la cámara haya fallado en este arranque. */
    err = upload_task_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "upload_task no iniciada: %s", esp_err_to_name(err));
    }

#if CONFIG_MG_DEBUG_HTTP
    /* httpd escucha en todas las interfaces: puede arrancar antes de tener IP. */
    ESP_ERROR_CHECK(debug_http_start());
#endif
    return ESP_OK;
}

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "MasterGrow nodo de cámara %s · node_id=%s · fw %s · IDF %s", app->project_name,
             SYS_NODE_ID, SYS_FIRMWARE_VERSION, app->idf_ver);
    app_state_init();

    size_t psram = esp_psram_is_initialized() ? esp_psram_get_size() : 0;
    if (psram == 0) {
        ESP_LOGE(TAG, "PSRAM no disponible: la cámara necesita PSRAM para los framebuffers");
        return;
    }
    ESP_LOGI(TAG, "PSRAM: %u KB", (unsigned)(psram / 1024));

    ESP_ERROR_CHECK(init_nvs());
    init_config();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    log_memory("arranque");

    init_storage();
    log_memory("almacenamiento");

    esp_err_t err = init_camera();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cámara no disponible: %s", esp_err_to_name(err));
        app_state_set_state(MG_STATE_ERROR);
    } else {
        log_memory("cámara lista");
        camera_selftest(CONFIG_MG_SELFTEST_CAPTURES);
        log_memory("autoprueba");

        err = start_pipeline();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Pipeline no iniciado: %s", esp_err_to_name(err));
            app_state_set_state(MG_STATE_ERROR);
        } else {
            s_pipeline_ok = true;
        }
    }

    /* Si la cámara falló, el status publicado al conectar lleva node_state=error. */
    start_network();
}
