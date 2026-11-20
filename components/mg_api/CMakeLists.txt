set(priv_reqs esp_http_client esp_timer)
if(CONFIG_MG_API_USE_TLS)
    list(APPEND priv_reqs esp-tls mbedtls)
endif()

idf_component_register(
    SRCS "mg_api.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${priv_reqs}
)
