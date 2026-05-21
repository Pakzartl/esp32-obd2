idf_component_register(
    SRCS "main.c" "espnow_rx.c" "ble_relay.c" "smart_features.c"
         "wifi_portal.c" "flash_logger.c" "ble_ota.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bt esp_wifi esp_event esp_netif esp_timer
                  nvs_flash driver esp_http_server spiffs app_update
)
