idf_component_register(
    SRCS "mg_ota.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES app_update esp_app_format esp_http_client esp_timer mbedtls nvs_flash mg_api
)
