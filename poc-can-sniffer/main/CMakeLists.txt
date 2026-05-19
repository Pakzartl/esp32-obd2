idf_component_register(SRCS "main.c" "obd2.c" "metrics.c"
                       INCLUDE_DIRS "."
                       REQUIRES mdns driver esp_timer nvs_flash esp_wifi esp_http_server app_update esp_netif esp_event bt)
