#include "network_station.h"

#include <algorithm>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "wifi_reconnect_policy.h"

namespace stagecore {
namespace {

constexpr char kTag[] = "stagelaser-net";
constexpr EventBits_t kConnectedBit = BIT0;

EventGroupHandle_t g_wifi_events = nullptr;
esp_timer_handle_t g_reconnect_timer = nullptr;
uint32_t g_reconnect_delay_ms = wifi_reconnect::kInitialDelayMs;
esp_event_handler_instance_t g_wifi_instance = nullptr;
esp_event_handler_instance_t g_ip_instance = nullptr;
bool g_initialized = false;

void stop_reconnect_timer() {
  if (g_reconnect_timer == nullptr) return;
  const esp_err_t err = esp_timer_stop(g_reconnect_timer);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "unable to stop reconnect timer: %s",
             esp_err_to_name(err));
  }
}

void reconnect_timer_callback(void *) {
  const esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
    ESP_LOGW(kTag, "scheduled Wi-Fi reconnect failed: %s",
             esp_err_to_name(err));
  }
}

void schedule_reconnect() {
  if (g_reconnect_timer == nullptr) return;
  stop_reconnect_timer();
  const uint32_t delay_ms = g_reconnect_delay_ms;
  const esp_err_t err =
      esp_timer_start_once(g_reconnect_timer,
                           static_cast<uint64_t>(delay_ms) * 1000ULL);
  if (err == ESP_OK) {
    g_reconnect_delay_ms = wifi_reconnect::next_delay_ms(delay_ms);
  } else {
    ESP_LOGW(kTag, "unable to schedule reconnect: %s",
             esp_err_to_name(err));
  }
}

void wifi_event_handler(void *, esp_event_base_t base, int32_t id, void *) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    g_reconnect_delay_ms = wifi_reconnect::kInitialDelayMs;
    const esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) schedule_reconnect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (g_wifi_events != nullptr) xEventGroupClearBits(g_wifi_events, kConnectedBit);
    schedule_reconnect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    g_reconnect_delay_ms = wifi_reconnect::kInitialDelayMs;
    stop_reconnect_timer();
    if (g_wifi_events != nullptr) xEventGroupSetBits(g_wifi_events, kConnectedBit);
  }
}

}  // namespace

esp_err_t init_network_stack() {
  if (g_initialized) return ESP_OK;

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

  if (esp_netif_create_default_wifi_sta() == nullptr) return ESP_FAIL;
  g_wifi_events = xEventGroupCreate();
  if (g_wifi_events == nullptr) return ESP_ERR_NO_MEM;

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&init);
  if (err != ESP_OK) return err;

  err = esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr,
      &g_wifi_instance);
  if (err != ESP_OK) return err;
  err = esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr,
      &g_ip_instance);
  if (err != ESP_OK) return err;

  esp_timer_create_args_t timer_args{};
  timer_args.callback = &reconnect_timer_callback;
  timer_args.name = "laser-wifi";
  err = esp_timer_create(&timer_args, &g_reconnect_timer);
  if (err != ESP_OK) return err;

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) return err;
  g_initialized = true;
  return ESP_OK;
}

esp_err_t connect_station(const std::string &ssid,
                          const std::string &password,
                          int timeout_ms) {
  if (ssid.empty() || ssid.size() > 32 ||
      password.size() < 8 || password.size() > 63) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = init_network_stack();
  if (err != ESP_OK) return err;

  wifi_config_t config{};
  std::memcpy(config.sta.ssid, ssid.data(),
              std::min(ssid.size(), sizeof(config.sta.ssid) - 1));
  std::memcpy(config.sta.password, password.data(),
              std::min(password.size(), sizeof(config.sta.password) - 1));
  config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  config.sta.pmf_cfg.capable = true;
  config.sta.pmf_cfg.required = false;

  err = esp_wifi_set_config(WIFI_IF_STA, &config);
  if (err != ESP_OK) return err;
  err = esp_wifi_start();
  if (err != ESP_OK) return err;
  return wait_for_station_connection(timeout_ms);
}

esp_err_t wait_for_station_connection(int timeout_ms) {
  if (g_wifi_events == nullptr) return ESP_ERR_INVALID_STATE;
  const TickType_t wait_ticks =
      timeout_ms <= 0 ? 0 : pdMS_TO_TICKS(timeout_ms);
  const EventBits_t bits =
      xEventGroupWaitBits(g_wifi_events, kConnectedBit, pdFALSE, pdTRUE,
                          wait_ticks);
  return (bits & kConnectedBit) != 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

}  // namespace stagecore
