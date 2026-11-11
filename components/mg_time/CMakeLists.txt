idf_component_register(
    SRCS "mg_time.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_event
    PRIV_REQUIRES esp_netif lwip
)
