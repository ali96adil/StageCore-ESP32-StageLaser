#pragma once

#include <string>

#include "esp_err.h"

namespace stagecore {

esp_err_t init_network_stack();
esp_err_t connect_station(const std::string &ssid,
                          const std::string &password,
                          int timeout_ms);
esp_err_t wait_for_station_connection(int timeout_ms);

}  // namespace stagecore
