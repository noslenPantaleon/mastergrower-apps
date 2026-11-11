set(priv_reqs esp_timer)
if(CONFIG_MG_MQTT_USE_TLS)
    list(APPEND priv_reqs mbedtls)
endif()

idf_component_register(
    SRCS "mg_mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event
    PRIV_REQUIRES ${priv_reqs}
)
