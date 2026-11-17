idf_component_register(SRCS "${CMAKE_CURRENT_LIST_DIR}/src/simconnect_client.c"
                       INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include"
                       REQUIRES lwip esp_timer)
