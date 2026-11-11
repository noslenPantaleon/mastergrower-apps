/**
 * @file app_main.c
 * @brief Nodo de cámara — arranque: cámara (C1), WiFi, net_task MQTT con status/LWT y
 *        comandos (C2) y, en desarrollo, el servidor HTTP de depuración.
 *
 * Las fases siguientes agregan aquí el arranque de las tareas del pipeline
 * (trigger_manager, capture_task, analysis_task, upload_task, ...).
 */
#include <inttypes.h>
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
#include "default_params.h"
#include "hal_camera.h"
#include "mg_time.h"
#include "mg_wifi.h"
#include "processes/net_task.h"
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

static esp_err_t init_camera(void)
{
    hal_camera_config_t cfg = HAL_CAMERA_CONFIG_DEFAULT();
    if (!hal_camera_res_from_name(DEF_RESOLUCION, &cfg.resolution)) {
        ESP_LOGW(TAG, "Resolución \"%s\" inválida, se usa SVGA", DEF_RESOLUCION);
        cfg.resolution = HAL_CAM_RES_SVGA;
    }
    cfg.jpeg_quality = DEF_CALIDAD_JPEG;

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

/**
 * capture_now: por ahora captura en el momento y registra el resultado.
 * En C3 pasa a encolar un disparo comando_remoto en el trigger_manager (respuesta asíncrona).
 */
static esp_err_t cmd_capture_now(const mg_cmd_t *cmd, char *motivo, size_t motivo_len)
{
    hal_frame_t f;
    esp_err_t err = hal_camera_capture(&f, pdMS_TO_TICKS(3000));
    if (err != ESP_OK) {
        snprintf(motivo, motivo_len, "error_camara");
        net_task_publish_event(MG_EVT_ERROR_CAMARA, MG_SEV_MEDIA, NULL, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "capture_now: %ux%u, %u bytes", f.width, f.height, (unsigned)f.len);
    hal_camera_release(&f);
    app_state_set_ultima_captura(time(NULL));
    return ESP_OK;
}

static void reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));  // margen para que salga el ack
    esp_restart();
}

static esp_err_t cmd_reboot(const mg_cmd_t *cmd, char *motivo, size_t motivo_len)
{
    ESP_LOGW(TAG, "Reinicio pedido por %s", cmd->emisor);
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
        app_state_set_estado(MG_ESTADO_AUTONOMO);
        return err;
    }

    net_task_register_cmd(MG_ACCION_CAPTURE_NOW, cmd_capture_now);
    net_task_register_cmd(MG_ACCION_REBOOT, cmd_reboot);
    ESP_ERROR_CHECK(net_task_start());

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
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    log_memory("arranque");

    esp_err_t err = init_camera();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cámara no disponible: %s", esp_err_to_name(err));
        app_state_set_estado(MG_ESTADO_ERROR);
    } else {
        log_memory("cámara lista");
        camera_selftest(CONFIG_MG_SELFTEST_CAPTURES);
        log_memory("autoprueba");
    }

    /* Si la cámara falló, el status publicado al conectar lleva estado_nodo=error. */
    start_network();
}
