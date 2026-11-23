idf_component_register(
    SRCS "app_main.c"
         "model/state_store.c"
         "processes/net_task.c"
         "services/command_publisher.c"
         "services/thumb_service.c"
         "services/live_view.c"
         "services/api_jobs.c"
         "services/jpeg_util.c"
         "services/rtc_sync.c"
         "services/net_setup.c"
         "ui/ui_bridge.c"
         "ui/lv_mem_psram.c"
         "ui/c/theme.c"
         "ui/c/ui_common.c"
         "ui/c/ui_thumbs.c"
         "ui/c/ui_shell.c"
         "ui/c/scr_home.c"
         "ui/c/scr_cameras.c"
         "ui/c/scr_events.c"
         "ui/c/scr_live.c"
         "ui/c/scr_camera.c"
         "ui/c/scr_config.c"
         "ui/c/scr_placeholder.c"
         "ui/c/ui_env.c"
         "ui/c/scr_env.c"
         "ui/c/scr_grow.c"
         "ui/c/scr_act.c"
         "ui/c/scr_net.c"
    INCLUDE_DIRS "." "config" "model" "processes" "services" "ui" "ui/c"
    EMBED_FILES "fonts/Montserrat-Medium.ttf"
    PRIV_REQUIRES node_hal mg_wifi mg_mqtt mg_time mg_contract mg_api mg_ota mg_settings
                  nvs_flash esp_netif esp_event esp_timer esp_psram esp_app_format
)

# lv_mem_psram.c define funciones que solo pide la librería de LVGL (enlazada después de main):
# se fuerzan para que el linker no descarte el objeto.
foreach(sym lv_mem_init lv_mem_deinit lv_mem_add_pool lv_mem_remove_pool lv_malloc_core
            lv_realloc_core lv_free_core lv_mem_monitor_core lv_mem_test_core)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u ${sym}")
endforeach()
