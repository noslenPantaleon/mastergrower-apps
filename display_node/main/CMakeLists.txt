idf_component_register(
    SRCS "app_main.c" "ui/ui_hello.c"
    INCLUDE_DIRS "." "ui"
    PRIV_REQUIRES node_hal esp_timer esp_psram esp_app_format
)
