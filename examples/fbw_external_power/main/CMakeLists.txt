idf_component_register(SRCS "app_main.c" "app_settings.c" "board_led.c"
    INCLUDE_DIRS "."
    REQUIRES simconnect esp_wifi esp_event esp_netif nvs_flash
             esp_driver_gpio esp_driver_rmt esp_timer console heap)

# __has_include alone cannot make a previously absent header a compiler
# dependency. Detect creation/removal too, then force recompilation on addition.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    file(GLOB local_config CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/app_config.h")
    if(local_config)
        set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/app_settings.c"
                     APPEND PROPERTY OBJECT_DEPENDS "${local_config}")
    endif()
endif()
