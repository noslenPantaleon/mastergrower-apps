idf_component_register(
    SRCS "config_store.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES nvs_flash freertos espressif__cjson
)
