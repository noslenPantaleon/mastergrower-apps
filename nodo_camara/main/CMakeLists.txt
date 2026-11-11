set(srcs
    "app_main.c"
    "app_state.c"
    "processes/net_task.c"
    "processes/pipeline.c"
    "processes/trigger_manager.c"
    "processes/capture_task.c"
    "processes/analysis_task.c"
)
if(CONFIG_MG_DEBUG_HTTP)
    list(APPEND srcs "debug/debug_http.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "." "config"
    PRIV_REQUIRES node_hal mg_wifi mg_mqtt mg_time mg_contract
                  nvs_flash esp_netif esp_event esp_timer esp_psram esp_http_server esp_app_format
)
