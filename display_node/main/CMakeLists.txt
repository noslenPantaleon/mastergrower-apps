idf_component_register(
    SRCS "app_main.c"
         "model/state_store.c"
         "processes/net_task.c"
         "ui/ui_bridge.c"
         "ui/ui_status.c"
    INCLUDE_DIRS "." "config" "model" "processes" "ui"
    PRIV_REQUIRES node_hal mg_wifi mg_mqtt mg_time mg_contract
                  nvs_flash esp_netif esp_event esp_timer esp_psram esp_app_format
)
