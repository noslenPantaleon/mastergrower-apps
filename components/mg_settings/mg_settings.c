/**
 * @file mg_settings.c
 * @brief Ver mg_settings.h.
 */
#include "mg_settings.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "mg_settings";

#define NS "mg_net"

/* Valores por defecto: los Kconfig de mg_wifi, mg_mqtt y mg_api (si el componente está en el
 * proyecto; si no, vacío). */
#ifndef CONFIG_MG_WIFI_SSID
#define CONFIG_MG_WIFI_SSID ""
#define CONFIG_MG_WIFI_PASSWORD ""
#endif
#ifndef CONFIG_MG_MQTT_BROKER_URI
#define CONFIG_MG_MQTT_BROKER_URI ""
#define CONFIG_MG_MQTT_PASSWORD ""
#endif
#ifndef CONFIG_MG_API_TOKEN
#define CONFIG_MG_API_TOKEN ""
#endif

/** "mqtt://192.168.1.10:1883" → "192.168.1.10". */
static void host_of(const char *uri, char *out, size_t len)
{
    const char *h = strstr(uri, "://");
    h = h ? h + 3 : uri;
    size_t n = strcspn(h, ":/");
    snprintf(out, len, "%.*s", (int)n, h);
}

static void get(nvs_handle_t h, const char *key, char *out, size_t len)
{
    size_t l = len;
    if (nvs_get_str(h, key, out, &l) != ESP_OK) {
        /* queda el valor por defecto que ya tenía out */
    }
}

esp_err_t mg_settings_load(mg_settings_t *s)
{
    memset(s, 0, sizeof(*s));
    strlcpy(s->wifi_ssid, CONFIG_MG_WIFI_SSID, sizeof(s->wifi_ssid));
    strlcpy(s->wifi_pass, CONFIG_MG_WIFI_PASSWORD, sizeof(s->wifi_pass));
    host_of(CONFIG_MG_MQTT_BROKER_URI, s->server, sizeof(s->server));
    strlcpy(s->mqtt_pass, CONFIG_MG_MQTT_PASSWORD, sizeof(s->mqtt_pass));
    strlcpy(s->api_token, CONFIG_MG_API_TOKEN, sizeof(s->api_token));

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return ESP_OK;  // nada guardado: todo de Kconfig
    }
    get(h, "ssid", s->wifi_ssid, sizeof(s->wifi_ssid));
    get(h, "wpass", s->wifi_pass, sizeof(s->wifi_pass));
    get(h, "server", s->server, sizeof(s->server));
    get(h, "mpass", s->mqtt_pass, sizeof(s->mqtt_pass));
    get(h, "token", s->api_token, sizeof(s->api_token));
    nvs_close(h);
    ESP_LOGI(TAG, "Red y servidor desde NVS: WiFi \"%s\", servidor %s", s->wifi_ssid, s->server);
    return ESP_OK;
}

bool mg_settings_stored(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t l = 0;
    bool any = nvs_get_str(h, "ssid", NULL, &l) == ESP_OK || nvs_get_str(h, "server", NULL, &l) == ESP_OK;
    nvs_close(h);
    return any;
}

esp_err_t mg_settings_save(const mg_settings_t *s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    const struct { const char *k, *v; } kv[] = {
        { "ssid", s->wifi_ssid }, { "wpass", s->wifi_pass }, { "server", s->server },
        { "mpass", s->mqtt_pass }, { "token", s->api_token },
    };
    for (size_t i = 0; i < sizeof(kv) / sizeof(kv[0]) && err == ESP_OK; i++) {
        err = nvs_set_str(h, kv[i].k, kv[i].v);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    ESP_LOGI(TAG, "Guardado: WiFi \"%s\", servidor %s (%s)", s->wifi_ssid, s->server, esp_err_to_name(err));
    return err;
}

esp_err_t mg_settings_erase(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_erase_all(h);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return err;
}

esp_err_t mg_settings_request_setup(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "setup", 1);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    return err;
}

bool mg_settings_take_setup_request(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    if (nvs_get_u8(h, "setup", &v) == ESP_OK) {
        nvs_erase_key(h, "setup");
        nvs_commit(h);
    }
    nvs_close(h);
    return v == 1;
}

void mg_settings_mqtt_uri(const mg_settings_t *s, char *out, size_t len)
{
    snprintf(out, len, "mqtt://%s:%d", s->server, CONFIG_MG_SETTINGS_MQTT_PORT);
}

void mg_settings_api_url(const mg_settings_t *s, char *out, size_t len)
{
    snprintf(out, len, "http://%s:%d%s", s->server, CONFIG_MG_SETTINGS_API_PORT, CONFIG_MG_SETTINGS_API_PATH);
}
