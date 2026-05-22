if(DEFINED FORCE_FW_VER)
    set(GIT_VER "${FORCE_FW_VER}")
else()
    execute_process(
        COMMAND git describe --tags --always --dirty
        WORKING_DIRECTORY ${COMPONENT_DIR}/../..
        OUTPUT_VARIABLE GIT_VER
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT GIT_VER)
        set(GIT_VER "0.0.0")
    endif()
    string(REGEX REPLACE "^v" "" GIT_VER "${GIT_VER}")
endif()
message(STATUS "Firmware version: ${GIT_VER}-relay")

idf_component_register(
    SRCS "main.c" "espnow_rx.c" "ble_relay.c" "smart_features.c"
         "wifi_portal.c" "flash_logger.c" "ble_ota.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bt esp_wifi esp_event esp_netif esp_timer
                  nvs_flash driver esp_http_server spiffs app_update
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE FW_VERSION_S3="${GIT_VER}-relay")
