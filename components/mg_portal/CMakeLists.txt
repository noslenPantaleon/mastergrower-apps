idf_component_register(
    SRCS "mg_portal.c" "dns_any.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_http_server lwip esp_timer esp_system mg_settings
)
