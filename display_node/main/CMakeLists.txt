idf_component_register(
    SRCS "app_main.c"
         "model/state_store.c"
         "processes/net_task.c"
         "services/command_publisher.c"
         "services/thumb_service.c"
         "ui/ui_bridge.c"
         "ui/c/theme.c"
         "ui/c/ui_common.c"
         "ui/c/ui_thumbs.c"
         "ui/c/ui_shell.c"
         "ui/c/scr_home.c"
         "ui/c/scr_cameras.c"
         "ui/c/scr_events.c"
         "ui/c/scr_placeholder.c"
    INCLUDE_DIRS "." "config" "model" "processes" "services" "ui" "ui/c"
    EMBED_FILES "fonts/Montserrat-Medium.ttf"
    PRIV_REQUIRES node_hal mg_wifi mg_mqtt mg_time mg_contract mg_api
                  nvs_flash esp_netif esp_event esp_timer esp_psram esp_app_format
)
