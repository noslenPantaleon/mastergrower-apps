idf_component_register(
    SRCS "src/hal_board.c" "src/hal_lcd.c" "src/hal_touch.c" "src/hal_rtc.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_driver_i2c
    PRIV_REQUIRES esp_driver_gpio esp_timer
)
