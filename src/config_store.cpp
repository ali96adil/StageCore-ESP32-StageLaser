#include "config_store.h"

#include <vector>

#include "nvs.h"

namespace stagecore {
namespace {

constexpr char kNamespace[] = "stagecore";
constexpr char kSSIDKey[] = "wifi_ssid";
constexpr char kPasswordKey[] = "wifi_pass";
constexpr char kDisplayKey[] = "display_name";
constexpr char kHubIDKey[] = "hub_id";
constexpr char kHubFingerprintKey[] = "hub_fp";
constexpr char kHubTLSKey[] = "hub_tls";

esp_err_t read_string(nvs_handle_t handle, const char *key, std::string *value,
                      bool *found = nullptr) {
  if (value == nullptr) return ESP_ERR_INVALID_ARG;
  if (found != nullptr) *found = false;

  size_t length = 0;
  esp_err_t err = nvs_get_str(handle, key, nullptr, &length);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    value->clear();
    return ESP_OK;
  }
  if (err != ESP_OK) return err;

  if (found != nullptr) *found = true;
  if (length == 0) {
    value->clear();
    return ESP_OK;
  }

  std::vector<char> buffer(length);
  err = nvs_get_str(handle, key, buffer.data(), &length);
  if (err == ESP_OK) *value = buffer.data();
  return err;
}

esp_err_t write_string(nvs_handle_t handle, const char *key,
                       const std::string &value) {
  return nvs_set_str(handle, key, value.c_str());
}

esp_err_t erase_key_if_present(nvs_handle_t handle, const char *key) {
  const esp_err_t err = nvs_erase_key(handle, key);
  return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

}  // namespace

bool DeviceConfig::complete() const {
  return !wifi_ssid.empty() && wifi_password.size() >= 8 &&
         !display_name.empty();
}

esp_err_t load_device_config(DeviceConfig *config) {
  if (config == nullptr) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    *config = DeviceConfig{};
    return ESP_OK;
  }
  if (err != ESP_OK) return err;

  DeviceConfig loaded;
  err = read_string(handle, kSSIDKey, &loaded.wifi_ssid);
  if (err == ESP_OK) err = read_string(handle, kPasswordKey, &loaded.wifi_password);
  if (err == ESP_OK) err = read_string(handle, kDisplayKey, &loaded.display_name);
  nvs_close(handle);
  if (err == ESP_OK) *config = std::move(loaded);
  return err;
}

esp_err_t save_device_config(const DeviceConfig &config) {
  if (!config.complete()) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = write_string(handle, kSSIDKey, config.wifi_ssid);
  if (err == ESP_OK) err = write_string(handle, kPasswordKey, config.wifi_password);
  if (err == ESP_OK) err = write_string(handle, kDisplayKey, config.display_name);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

bool HubBinding::complete() const {
  return hub_id.size() == 36 && !fingerprint.empty() && tls_sha256.size() == 64;
}

esp_err_t load_hub_binding(HubBinding *binding) {
  if (binding == nullptr) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    *binding = HubBinding{};
    return ESP_OK;
  }
  if (err != ESP_OK) return err;

  HubBinding loaded;
  bool hub_id_found = false;
  bool fingerprint_found = false;
  bool tls_found = false;

  err = read_string(handle, kHubIDKey, &loaded.hub_id, &hub_id_found);
  if (err == ESP_OK) {
    err = read_string(handle, kHubFingerprintKey, &loaded.fingerprint,
                      &fingerprint_found);
  }
  if (err == ESP_OK) {
    err = read_string(handle, kHubTLSKey, &loaded.tls_sha256, &tls_found);
  }
  nvs_close(handle);
  if (err != ESP_OK) return err;

  const int present_count =
      static_cast<int>(hub_id_found) +
      static_cast<int>(fingerprint_found) +
      static_cast<int>(tls_found);

  if (present_count == 0) {
    *binding = HubBinding{};
    return ESP_OK;
  }

  // A partially present trust record is not equivalent to a never-paired
  // device. Treat torn/corrupt trust state as a hard error so discovery cannot
  // silently bind to a different Hub.
  if (present_count != 3 || !loaded.complete()) {
    return ESP_ERR_INVALID_STATE;
  }

  *binding = std::move(loaded);
  return ESP_OK;
}

esp_err_t save_hub_binding(const HubBinding &binding) {
  if (!binding.complete()) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = write_string(handle, kHubIDKey, binding.hub_id);
  if (err == ESP_OK) err = write_string(handle, kHubFingerprintKey, binding.fingerprint);
  if (err == ESP_OK) err = write_string(handle, kHubTLSKey, binding.tls_sha256);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

esp_err_t clear_hub_binding() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (err != ESP_OK) return err;
  err = erase_key_if_present(handle, kHubIDKey);
  if (err == ESP_OK) err = erase_key_if_present(handle, kHubFingerprintKey);
  if (err == ESP_OK) err = erase_key_if_present(handle, kHubTLSKey);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

}  // namespace stagecore
