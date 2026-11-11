/**
 * @file mg_wifi.c
 * @brief Implementación de mg_wifi. La reconexión se agenda con un esp_timer para no
 *        bloquear el event loop (nunca se hace vTaskDelay dentro de un handler).
 */
#include "mg_wifi.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

ESP_EVENT_DEFINE_BASE(MG_WIFI_EVENT);

static const char *TAG = "mg_wifi";

#define BIT_CONNECTED BIT0

static struct {
    EventGroupHandle_t events;
    esp_netif_t *netif;
    esp_timer_handle_t retry_timer;
    esp_event_handler_instance_t h_wifi;
    esp_event_handler_instance_t h_ip;
    uint32_t backoff_ms;
    bool started;
    bool stopping;
} s_wifi;

static void retry_timer_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Reintentando conexión...");
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
    }
}

static void schedule_retry(void)
{
    if (s_wifi.stopping) {
        return;
    }
    ESP_LOGI(TAG, "Próximo intento en %" PRIu32 " ms", s_wifi.backoff_ms);
    esp_timer_stop(s_wifi.retry_timer);  // por si ya estaba armado
    esp_timer_start_once(s_wifi.retry_timer, (uint64_t)s_wifi.backoff_ms * 1000ULL);

    uint32_t next = s_wifi.backoff_ms * 2;
    s_wifi.backoff_ms = next > CONFIG_MG_WIFI_BACKOFF_MAX_MS ? CONFIG_MG_WIFI_BACKOFF_MAX_MS : next;
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *ev = data;
        bool was_connected = xEventGroupGetBits(s_wifi.events) & BIT_CONNECTED;
        xEventGroupClearBits(s_wifi.events, BIT_CONNECTED);
        ESP_LOGW(TAG, "Desconectado (reason=%d)", ev->reason);
        if (was_connected) {
            uint8_t reason = ev->reason;
            esp_event_post(MG_WIFI_EVENT, MG_WIFI_EVENT_DISCONNECTED, &reason, sizeof(reason), 0);
        }
        schedule_retry();
        break;
    }
    default:
        break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id != IP_EVENT_STA_GOT_IP) {
        return;
    }
    const ip_event_got_ip_t *ev = data;
    ESP_LOGI(TAG, "Conectado, IP " IPSTR, IP2STR(&ev->ip_info.ip));
    s_wifi.backoff_ms = CONFIG_MG_WIFI_BACKOFF_MIN_MS;
    xEventGroupSetBits(s_wifi.events, BIT_CONNECTED);
    esp_event_post(MG_WIFI_EVENT, MG_WIFI_EVENT_CONNECTED, &ev->ip_info, sizeof(ev->ip_info), 0);
}

esp_err_t mg_wifi_start(const mg_wifi_config_t *cfg)
{
    if (s_wifi.started) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *ssid = (cfg && cfg->ssid) ? cfg->ssid : CONFIG_MG_WIFI_SSID;
    const char *pass = (cfg && cfg->password) ? cfg->password : CONFIG_MG_WIFI_PASSWORD;
    if (ssid[0] == '\0') {
        ESP_LOGE(TAG, "SSID vacío: configurarlo en menuconfig → MasterGrow - WiFi");
        return ESP_ERR_INVALID_ARG;
    }

    s_wifi.events = xEventGroupCreate();
    if (!s_wifi.events) {
        return ESP_ERR_NO_MEM;
    }
    s_wifi.backoff_ms = CONFIG_MG_WIFI_BACKOFF_MIN_MS;
    s_wifi.stopping = false;

    const esp_timer_create_args_t targs = { .callback = retry_timer_cb, .name = "wifi_retry" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&targs, &s_wifi.retry_timer), TAG, "timer");

    s_wifi.netif = esp_netif_create_default_wifi_sta();
    if (cfg && cfg->hostname) {
        esp_netif_set_hostname(s_wifi.netif, cfg->hostname);
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "esp_wifi_init");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, &s_wifi.h_wifi), TAG, "reg wifi");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL, &s_wifi.h_ip), TAG, "reg ip");

    wifi_config_t wcfg = { 0 };
    strlcpy((char *)wcfg.sta.ssid, ssid, sizeof(wcfg.sta.ssid));
    strlcpy((char *)wcfg.sta.password, pass, sizeof(wcfg.sta.password));
    wcfg.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wcfg.sta.pmf_cfg.capable = true;

    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");  // credenciales: las maneja la app
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wcfg), TAG, "config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
#if CONFIG_MG_WIFI_POWER_SAVE
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
#else
    esp_wifi_set_ps(WIFI_PS_NONE);
#endif

    s_wifi.started = true;
    ESP_LOGI(TAG, "WiFi iniciado, conectando a \"%s\"", ssid);
    return ESP_OK;
}

esp_err_t mg_wifi_stop(void)
{
    if (!s_wifi.started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_wifi.stopping = true;
    esp_timer_stop(s_wifi.retry_timer);
    esp_timer_delete(s_wifi.retry_timer);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi.h_wifi);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_wifi.h_ip);
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(s_wifi.netif);
    vEventGroupDelete(s_wifi.events);
    memset(&s_wifi, 0, sizeof(s_wifi));
    return ESP_OK;
}

esp_err_t mg_wifi_wait_connected(TickType_t timeout)
{
    if (!s_wifi.started) {
        return ESP_ERR_INVALID_STATE;
    }
    EventBits_t bits = xEventGroupWaitBits(s_wifi.events, BIT_CONNECTED, pdFALSE, pdTRUE, timeout);
    return (bits & BIT_CONNECTED) ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool mg_wifi_is_connected(void)
{
    return s_wifi.started && (xEventGroupGetBits(s_wifi.events) & BIT_CONNECTED);
}

int8_t mg_wifi_get_rssi(void)
{
    wifi_ap_record_t ap;
    if (!mg_wifi_is_connected() || esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return 0;
    }
    return ap.rssi;
}

esp_err_t mg_wifi_get_ip_str(char *buf, size_t len)
{
    if (!buf || len < 16) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_netif_ip_info_t ip = { 0 };
    if (s_wifi.netif) {
        esp_netif_get_ip_info(s_wifi.netif, &ip);
    }
    snprintf(buf, len, IPSTR, IP2STR(&ip.ip));
    return ESP_OK;
}
