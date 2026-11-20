/**
 * @file board_xiao_esp32s3_sense.h
 * @brief Pines de la placa de expansión Sense del Seeed XIAO ESP32-S3 (cámara DVP y microSD).
 *        Fuente: esquemático y camera_pins.h (CAMERA_MODEL_XIAO_ESP32S3) de Seeed Studio.
 */
#pragma once

/* Cámara (conector B2B de la placa Sense) */
#define BOARD_CAM_PIN_PWDN   (-1)
#define BOARD_CAM_PIN_RESET  (-1)
#define BOARD_CAM_PIN_XCLK   10
#define BOARD_CAM_PIN_SIOD   40   /* SCCB SDA */
#define BOARD_CAM_PIN_SIOC   39   /* SCCB SCL */
#define BOARD_CAM_PIN_D7     48   /* Y9 */
#define BOARD_CAM_PIN_D6     11   /* Y8 */
#define BOARD_CAM_PIN_D5     12   /* Y7 */
#define BOARD_CAM_PIN_D4     14   /* Y6 */
#define BOARD_CAM_PIN_D3     16   /* Y5 */
#define BOARD_CAM_PIN_D2     18   /* Y4 */
#define BOARD_CAM_PIN_D1     17   /* Y3 */
#define BOARD_CAM_PIN_D0     15   /* Y2 */
#define BOARD_CAM_PIN_VSYNC  38
#define BOARD_CAM_PIN_HREF   47
#define BOARD_CAM_PIN_PCLK   13

/* microSD por SPI (fase C4). CS comparte GPIO21 con el LED de usuario. */
#define BOARD_SD_PIN_CS      21
#define BOARD_SD_PIN_SCK     7
#define BOARD_SD_PIN_MISO    8
#define BOARD_SD_PIN_MOSI    9

/* LED de usuario (activo en bajo). En conflicto con BOARD_SD_PIN_CS. */
#define BOARD_LED_PIN        21
