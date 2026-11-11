# HAL del nodo de cámara. Se llama "node_hal" (no "hal") porque ESP-IDF ya tiene un
# componente interno "hal" y uno del proyecto con el mismo nombre lo reemplazaría.
set(srcs
    "src/hal_camera.c"
    "src/hal_camera_dvp_common.c"
)
if(CONFIG_MG_CAMERA_OV3660)
    list(APPEND srcs "src/hal_camera_ov3660.c")
elseif(CONFIG_MG_CAMERA_OV5640)
    list(APPEND srcs "src/hal_camera_ov5640.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_timer esp_psram
)
