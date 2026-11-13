idf_component_register(
    SRCS "storage_queue.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_psram esp_hw_support freertos fatfs vfs
)
