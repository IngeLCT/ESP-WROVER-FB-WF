idf_component_register(
    SRCS "wifi.c" "sensors.c" "Geoapify.c" "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_firebase driver esp_timer esp_event esp_netif nvs_flash esp_wifi
)