/**
 * @file hal_rtc.c
 * @brief Ver hal_rtc.h. Registros del PCF85063: 0x04 segundos (bit 7 = OS, oscilador detenido),
 *        0x05 minutos, 0x06 horas (modo 24 h), 0x07 día, 0x08 día de la semana, 0x09 mes,
 *        0x0A año (00–99). Todo en BCD.
 */
#include "hal_rtc.h"

#include "esp_check.h"
#include "hal_board.h"

static const char *TAG = "hal_rtc";

#define RTC_ADDR      0x51
#define REG_SECONDS   0x04
#define OS_FLAG       0x80
#define TIMEOUT_MS    100

static i2c_master_dev_handle_t s_dev;

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/* Días desde 1970-01-01 (algoritmo civil de H. Hinnant): sin depender de TZ ni de mktime. */
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

esp_err_t hal_rtc_init(void)
{
    ESP_RETURN_ON_FALSE(hal_board_i2c(), ESP_ERR_INVALID_STATE, TAG, "placa no iniciada");
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RTC_ADDR,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(hal_board_i2c(), &dev, &s_dev);
}

esp_err_t hal_rtc_get(time_t *out)
{
    ESP_RETURN_ON_FALSE(s_dev && out, ESP_ERR_INVALID_STATE, TAG, "RTC no iniciado");
    uint8_t reg = REG_SECONDS, r[7];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, r, sizeof(r), TIMEOUT_MS), TAG, "lectura");
    if (r[0] & OS_FLAG) {
        return ESP_ERR_INVALID_STATE;
    }
    int year = 2000 + bcd2bin(r[6]);
    unsigned month = bcd2bin(r[5] & 0x1F), day = bcd2bin(r[3] & 0x3F);
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return ESP_ERR_INVALID_STATE;
    }
    int64_t days = days_from_civil(year, month, day);
    *out = (time_t)(days * 86400 + bcd2bin(r[2] & 0x3F) * 3600 + bcd2bin(r[1] & 0x7F) * 60 +
                    bcd2bin(r[0] & 0x7F));
    return ESP_OK;
}

esp_err_t hal_rtc_set(time_t t)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "RTC no iniciado");
    struct tm tm;
    gmtime_r(&t, &tm);
    ESP_RETURN_ON_FALSE(tm.tm_year >= 100 && tm.tm_year < 200, ESP_ERR_INVALID_ARG, TAG, "año fuera de 2000–2099");
    uint8_t w[8] = {
        REG_SECONDS,
        bin2bcd(tm.tm_sec),  // OS = 0
        bin2bcd(tm.tm_min),
        bin2bcd(tm.tm_hour),
        bin2bcd(tm.tm_mday),
        (uint8_t)tm.tm_wday,
        bin2bcd(tm.tm_mon + 1),
        bin2bcd(tm.tm_year - 100),
    };
    return i2c_master_transmit(s_dev, w, sizeof(w), TIMEOUT_MS);
}
