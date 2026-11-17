/**
 * @file hal_storage_sd.c
 * @brief microSD de la placa Sense del XIAO ESP32-S3 por SPI (SDSPI) + FATFS.
 */
#include "hal_storage.h"

#include "board_xiao_esp32s3_sense.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"

static const char *TAG = "hal_storage";

#define SD_SPI_HOST    SPI2_HOST
#define SD_INIT_TRIES  3

static struct {
    sdmmc_card_t *card;
    bool bus_ok;
} s_sd;

esp_err_t hal_storage_init(void)
{
    if (s_sd.card) {
        return ESP_ERR_INVALID_STATE;
    }

    const spi_bus_config_t bus = {
        .mosi_io_num = BOARD_SD_PIN_MOSI,
        .miso_io_num = BOARD_SD_PIN_MISO,
        .sclk_io_num = BOARD_SD_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16 * 1024,
    };
    /* En modo SPI la tarjeta necesita pull-ups en CS, MISO y MOSI (spec SD). La placa Sense puede
     * no tenerlos en todas las líneas: se habilitan los internos (~45 kΩ) como respaldo. */
    const int pulled[] = { BOARD_SD_PIN_CS, BOARD_SD_PIN_MISO, BOARD_SD_PIN_MOSI };
    for (size_t i = 0; i < sizeof(pulled) / sizeof(pulled[0]); i++) {
        gpio_set_pull_mode(pulled[i], GPIO_PULLUP_ONLY);
    }

    if (!s_sd.bus_ok) {
        ESP_RETURN_ON_ERROR(spi_bus_initialize(SD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi_bus_initialize");
        s_sd.bus_ok = true;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = CONFIG_MG_SD_SPI_FREQ_KHZ;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = BOARD_SD_PIN_CS;
    slot.host_id = SD_SPI_HOST;

    const esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    /* Algunas tarjetas tardan en estar listas tras el encendido: se reintenta con espera creciente. */
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < SD_INIT_TRIES; i++) {
        err = esp_vfs_fat_sdspi_mount(HAL_STORAGE_ROOT, &host, &slot, &mount, &s_sd.card);
        if (err == ESP_OK || err == ESP_FAIL) {  // ESP_FAIL = tarjeta presente pero sin FAT: no reintentar
            break;
        }
        ESP_LOGW(TAG, "Intento %d/%d de montar la microSD: %s", i + 1, SD_INIT_TRIES, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(200 * (i + 1)));
    }
    if (err != ESP_OK) {
        s_sd.card = NULL;
        spi_bus_free(SD_SPI_HOST);
        s_sd.bus_ok = false;
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "Tarjeta detectada pero sin FAT válido: formatearla en FAT32");
            return ESP_FAIL;
        }
        ESP_LOGW(TAG, "Sin microSD (%s): el nodo opera sin buffer local", esp_err_to_name(err));
        return ESP_ERR_NOT_FOUND;
    }

    uint64_t total = 0, free_b = 0;
    hal_storage_get_usage(&total, &free_b);
    ESP_LOGI(TAG, "microSD \"%s\" montada en %s: %llu MB, libre %llu MB (%d%%)", s_sd.card->cid.name,
             HAL_STORAGE_ROOT, total >> 20, free_b >> 20, hal_storage_free_pct());
    return ESP_OK;
}

esp_err_t hal_storage_deinit(void)
{
    if (!s_sd.card) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_vfs_fat_sdcard_unmount(HAL_STORAGE_ROOT, s_sd.card);
    s_sd.card = NULL;
    spi_bus_free(SD_SPI_HOST);
    s_sd.bus_ok = false;
    return err;
}

bool hal_storage_is_mounted(void)
{
    return s_sd.card != NULL;
}

esp_err_t hal_storage_get_usage(uint64_t *total, uint64_t *free_bytes)
{
    if (!s_sd.card) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_vfs_fat_info(HAL_STORAGE_ROOT, total, free_bytes);
}

int hal_storage_free_pct(void)
{
    uint64_t total = 0, free_b = 0;
    if (hal_storage_get_usage(&total, &free_b) != ESP_OK || total == 0) {
        return -1;
    }
    return (int)(free_b * 100 / total);
}
