idf_component_register(
    SRCS "main.cpp" 
         "cocktail_server.cpp" 
         "arduino_comm.cpp"
         "wifi_manager.cpp"
         "display_manager.cpp"
         "recipes.cpp"
         "flow_calibration.cpp"
         "button_manager.cpp"
         "local_discovery.cpp"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_netif esp_event esp_http_server spiffs espressif__mdns esp_wifi esp_timer esp_driver_uart esp_driver_spi esp_driver_gpio
)

set(SPIFFS_DATA_DIR "${CMAKE_BINARY_DIR}/spiffs_data")
set(PREPARE_SPIFFS_COMMAND
    ${PYTHON} "${CMAKE_SOURCE_DIR}/tools/prepare_spiffs_data.py"
    "${CMAKE_SOURCE_DIR}/data/www" "${SPIFFS_DATA_DIR}")
execute_process(COMMAND ${PREPARE_SPIFFS_COMMAND} RESULT_VARIABLE PREPARE_SPIFFS_RESULT)
if(NOT PREPARE_SPIFFS_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not prepare SPIFFS web assets")
endif()
add_custom_target(prepare_spiffs_data COMMAND ${PREPARE_SPIFFS_COMMAND})
spiffs_create_partition_image(webdata "${SPIFFS_DATA_DIR}" DEPENDS prepare_spiffs_data)
