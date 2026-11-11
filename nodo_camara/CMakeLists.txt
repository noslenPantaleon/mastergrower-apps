cmake_minimum_required(VERSION 3.22)

# Valores por defecto versionados + overrides locales NO versionados (credenciales WiFi, broker).
# Copiar sdkconfig.defaults.local.example → sdkconfig.defaults.local y completar.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults.local")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nodo_camara)
