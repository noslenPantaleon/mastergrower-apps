set(srcs "app_main.c")
if(CONFIG_MG_DEBUG_HTTP)
    list(APPEND srcs "debug/debug_http.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "." "config"
    PRIV_REQUIRES node_hal mg_wifi nvs_flash esp_netif esp_event esp_timer esp_psram esp_http_server esp_app_format
)
