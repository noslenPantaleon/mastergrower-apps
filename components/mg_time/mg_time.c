/**
 * @file mg_time.c
 */
#include "mg_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "sdkconfig.h"

ESP_EVENT_DEFINE_BASE(MG_TIME_EVENT);

static const char *TAG = "mg_time";
static bool s_started;

static void on_sync(struct timeval *tv)
{
    char iso[MG_TIME_ISO_LEN];
    mg_time_to_iso(tv->tv_sec, iso, sizeof(iso));
    ESP_LOGI(TAG, "Hora sincronizada por NTP: %s", iso);
    esp_event_post(MG_TIME_EVENT, MG_TIME_EVENT_SYNCED, NULL, 0, 0);
}

esp_err_t mg_time_start(const char *server)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    setenv("TZ", "UTC0", 1);
    tzset();

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(server ? server : CONFIG_MG_TIME_NTP_SERVER);
    if (!server) {
        const char *extra[] = { CONFIG_MG_TIME_NTP_SERVER2, CONFIG_MG_TIME_NTP_SERVER3 };
        for (size_t i = 0; i < sizeof(extra) / sizeof(extra[0]); i++) {
            if (extra[i][0] && cfg.num_of_servers < CONFIG_LWIP_SNTP_MAX_SERVERS) {
                cfg.servers[cfg.num_of_servers++] = extra[i];
            }
        }
    }
    cfg.sync_cb = on_sync;

    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err == ESP_OK) {
        s_started = true;
        for (int i = 0; i < cfg.num_of_servers; i++) {
            ESP_LOGI(TAG, "SNTP servidor %d: %s", i, cfg.servers[i]);
        }
    }
    return err;
}

esp_err_t mg_time_wait_sync(TickType_t timeout)
{
    if (mg_time_is_valid()) {
        return ESP_OK;
    }
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_netif_sntp_sync_wait(timeout);
}

bool mg_time_is_valid(void)
{
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    return tm.tm_year + 1900 >= CONFIG_MG_TIME_VALID_AFTER_YEAR;
}

void mg_time_to_iso(time_t t, char *buf, size_t len)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

void mg_time_now_iso(char *buf, size_t len)
{
    mg_time_to_iso(time(NULL), buf, len);
}

/* Días desde 1970-01-01 para una fecha civil (algoritmo de H. Hinnant); evita depender de timegm. */
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

esp_err_t mg_time_parse_iso(const char *iso, time_t *out)
{
    if (!iso || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    int y, mo, d, h, mi, s, consumed = 0;
    if (sscanf(iso, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi, &s, &consumed) != 6) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *rest = iso + consumed;
    if (*rest == '.') {  // fracción de segundo: se ignora
        rest++;
        while (*rest >= '0' && *rest <= '9') {
            rest++;
        }
    }
    if (strcmp(rest, "Z") != 0 && strcmp(rest, "+00:00") != 0) {
        return ESP_ERR_INVALID_ARG;  // el contrato exige UTC
    }
    *out = (time_t)(days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + s);
    return ESP_OK;
}
