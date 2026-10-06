idf_component_register(
  SRCS
    "main.cpp"
    "config_store.cpp"
    "device_identity.cpp"
    "laser_state_machine.cpp"
    "network_station.cpp"
    "relay_output.cpp"
  INCLUDE_DIRS "."
  REQUIRES
    driver
    esp_event
    esp_netif
    esp_timer
    esp_wifi
    mbedtls
    nvs_flash
)
