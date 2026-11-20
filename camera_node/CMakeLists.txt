cmake_minimum_required(VERSION 3.22)

# Valores por defecto versionados + overrides locales NO versionados (credenciales WiFi, broker).
# Copiar sdkconfig.defaults.local.example → sdkconfig.defaults.local y completar.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults.local")
endif()

# Servicios del nodo (Arquitectura §3.3: components/services/*): IDF solo busca componentes en el
# primer nivel de components/, así que esta subcarpeta se agrega explícitamente.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/services")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(camera_node)
