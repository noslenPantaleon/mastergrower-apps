/**
 * @file rtc_sync.c
 * @brief Ver rtc_sync.h.
 */
#include "rtc_sync.h"

#include <sys/time.h>
#include "esp_event.h"
#include "esp_log.h"
#include "hal_rtc.h"
#include "mg_time.h"
#include "sdkconfig.h"

static const char *TAG = "rtc_sync";

/* Un RTC nunca ajustado puede traer cualquier fecha sin marcar "oscilador detenido" (se vio
 * 2054): solo se acepta un año razonable. */
#define RTC_MAX_YEAR (CONFIG_MG_TIME_VALID_AFTER_YEAR + 15)

static bool plausible(time_t t)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    int y = tm.tm_year + 1900;
    return y >= CONFIG_MG_TIME_VALID_AFTER_YEAR && y <= RTC_MAX_YEAR;
}

static void on_ntp(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    time_t now = time(NULL);
    time_t rtc = 0;
    bool had = hal_rtc_get(&rtc) == ESP_OK && plausible(rtc);
    esp_err_t err = hal_rtc_set(now);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo ajustar el RTC: %s", esp_err_to_name(err));
    } else if (had) {
        ESP_LOGI(TAG, "RTC ajustado por NTP (desvío %+lld s)", (long long)(rtc - now));
    } else {
        ESP_LOGI(TAG, "RTC ajustado por NTP");
    }
}

esp_err_t rtc_sync_start(void)
{
    esp_err_t err = hal_rtc_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC no disponible: %s", esp_err_to_name(err));
        return err;
    }
    time_t t;
    err = hal_rtc_get(&t);
    if (err == ESP_OK && !plausible(t)) {
        char iso[MG_TIME_ISO_LEN];
        mg_time_to_iso(t, iso, sizeof(iso));
        ESP_LOGW(TAG, "RTC con fecha imposible (%s): se ajusta con el primer NTP", iso);
        err = ESP_ERR_INVALID_RESPONSE;
    } else if (err == ESP_OK) {
        struct timeval tv = { .tv_sec = t };
        settimeofday(&tv, NULL);
        char iso[MG_TIME_ISO_LEN];
        mg_time_to_iso(t, iso, sizeof(iso));
        ESP_LOGI(TAG, "Hora del RTC: %s%s", iso, mg_time_is_valid() ? "" : " (no válida: se espera NTP)");
    } else {
        ESP_LOGW(TAG, "RTC sin hora válida (%s): se ajusta con el primer NTP", esp_err_to_name(err));
    }
    return esp_event_handler_register(MG_TIME_EVENT, MG_TIME_EVENT_SYNCED, on_ntp, NULL);
}
