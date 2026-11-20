idf_component_register(
    SRCS "hsv.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_timer esp_psram heap espressif__esp_jpeg
)
