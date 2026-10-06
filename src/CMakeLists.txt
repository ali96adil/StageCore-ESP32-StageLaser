idf_component_register(
  SRCS
    "main.cpp"
    "config_store.cpp"
    "device_identity.cpp"
    "laser_state_machine.cpp"
    "relay_output.cpp"
  INCLUDE_DIRS "."
  REQUIRES
    driver
    mbedtls
    nvs_flash
)
