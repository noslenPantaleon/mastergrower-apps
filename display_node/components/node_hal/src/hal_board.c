/**
 * @file hal_board.c
 * @brief Ver hal_board.h.
 *
 * El CH422G no tiene registros: cada "dirección I2C" es un comando. 0x24 = configuración del
 * sistema (bit 0: EXIO como salidas), 0x38 = valor de las 8 salidas EXIO. Se guarda una copia
 * del último valor escrito para poder cambiar un bit sin tocar los demás.
 */
#include "hal_board.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "hal_board";

#define I2C_SDA        8
#define I2C_SCL        9
#define I2C_HZ         400000
#define I2C_TIMEOUT_MS 100

#define CH422G_CMD_SET  0x24  /**< configuración del sistema */
#define CH422G_CMD_IO   0x38  /**< salidas EXIO0–7 */
#define CH422G_IO_OE    0x01  /**< EXIO como salidas */

static struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t ch_set, ch_io;
    SemaphoreHandle_t lock;
    uint8_t exio;
} s_b;

i2c_master_bus_handle_t hal_board_i2c(void)
{
    return s_b.bus;
}

static esp_err_t exio_write(uint8_t value)
{
    esp_err_t err = i2c_master_transmit(s_b.ch_io, &value, 1, I2C_TIMEOUT_MS);
    if (err == ESP_OK) {
        s_b.exio = value;
    }
    return err;
}

esp_err_t hal_board_exio_set(uint8_t mask, bool level)
{
    ESP_RETURN_ON_FALSE(s_b.lock, ESP_ERR_INVALID_STATE, TAG, "placa no iniciada");
    xSemaphoreTake(s_b.lock, portMAX_DELAY);
    esp_err_t err = exio_write(level ? (s_b.exio | mask) : (s_b.exio & ~mask));
    xSemaphoreGive(s_b.lock);
    return err;
}

esp_err_t hal_board_init(void)
{
    if (s_b.bus) {
        return ESP_OK;
    }
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_b.bus), TAG, "bus I2C");

    i2c_device_config_t dev = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .scl_speed_hz = I2C_HZ };
    dev.device_address = CH422G_CMD_SET;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_b.bus, &dev, &s_b.ch_set), TAG, "CH422G set");
    dev.device_address = CH422G_CMD_IO;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_b.bus, &dev, &s_b.ch_io), TAG, "CH422G io");

    s_b.lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_b.lock, ESP_ERR_NO_MEM, TAG, "mutex");

    uint8_t oe = CH422G_IO_OE;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_b.ch_set, &oe, 1, I2C_TIMEOUT_MS), TAG,
                        "CH422G no responde");
    /* Retroiluminación apagada hasta tener la primera imagen; LCD fuera de reset; SD sin
     * seleccionar; táctil en reset (lo libera hal_touch_init). */
    ESP_RETURN_ON_ERROR(exio_write(HAL_EXIO_LCD_RST | HAL_EXIO_SD_CS), TAG, "CH422G salidas");
    ESP_LOGI(TAG, "I2C (SDA %d, SCL %d) y CH422G listos", I2C_SDA, I2C_SCL);
    return ESP_OK;
}
