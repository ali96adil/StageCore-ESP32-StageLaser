#include "provisioning.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include "config_store.h"
#include "foundation_contract.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_station.h"

namespace stagecore {
namespace {

constexpr char kTag[] = "stagelaser-setup";

static_assert(sizeof(kDefaultSetupAPPassword) - 1 >= 8 &&
                  sizeof(kDefaultSetupAPPassword) - 1 <= 63,
              "StageCore setup AP password must be 8-63 bytes");

std::string effective_setup_ap_password() {
  std::string password;
  if (foundation_store().EffectiveSetupAPPassword(&password) == ESP_OK) {
    return password;
  }
  // Storage failure must not create an open or random recovery network.
  return kDefaultSetupAPPassword;
}

struct PortalContext {
  std::string default_display_name;
  bool recovery = false;
};

int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
  if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
  return -1;
}

std::string url_decode(const std::string &input) {
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '+') {
      out.push_back(' ');
    } else if (input[i] == '%' && i + 2 < input.size()) {
      const int hi = hex_value(input[i + 1]);
      const int lo = hex_value(input[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
      } else {
        out.push_back(input[i]);
      }
    } else {
      out.push_back(input[i]);
    }
  }
  return out;
}

std::string form_value(const std::string &body, const std::string &key) {
  size_t start = 0;
  while (start < body.size()) {
    const size_t end = body.find('&', start);
    const std::string pair =
        body.substr(start, end == std::string::npos ? std::string::npos
                                                    : end - start);
    const size_t eq = pair.find('=');
    if (eq != std::string::npos && pair.substr(0, eq) == key) {
      return url_decode(pair.substr(eq + 1));
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return {};
}

std::string html_escape(const std::string &input) {
  std::string out;
  for (char ch : input) {
    switch (ch) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out.push_back(ch); break;
    }
  }
  return out;
}

std::string id_suffix(const std::string &device_id) {
  std::string compact;
  for (char ch : device_id) {
    if (std::isxdigit(static_cast<unsigned char>(ch))) compact.push_back(ch);
  }
  if (compact.size() > 6) compact = compact.substr(compact.size() - 6);
  return compact.empty() ? "setup" : compact;
}

esp_err_t root_handler(httpd_req_t *req) {
  auto *ctx = static_cast<PortalContext *>(req->user_ctx);
  const bool recovery = ctx != nullptr && ctx->recovery;
  const std::string name =
      html_escape(ctx == nullptr ? "StageLaser" : ctx->default_display_name);

  std::string page =
      "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>StageLaser Setup</title>"
      "<style>body{font-family:system-ui;max-width:620px;margin:40px auto;"
      "padding:0 18px}label{display:block;margin:14px 0 5px}"
      "input{width:100%;padding:10px;box-sizing:border-box}"
      "button{margin-top:20px;padding:11px 18px}small{color:#666}</style>"
      "</head><body><h1>StageCore StageLaser</h1>";

  if (recovery) {
    page +=
        "<p>Stage LAN recovery. Hub command authority is unavailable while "
        "this portal is active, and recovery performs no relay actuation.</p>";
  } else {
    page +=
        "<p>First-run Stage LAN setup. Laser output remains disabled and "
        "DISARMED during provisioning.</p>";
  }

  page +=
      "<form method='post' action='/save'>"
      "<label>Wi-Fi SSID</label><input name='ssid' maxlength='32' required>"
      "<label>Wi-Fi password</label><input name='password' type='password' "
      "minlength='8' maxlength='63' required>";

  if (!recovery) {
    page +=
        "<label>Display name</label><input name='display_name' maxlength='64' "
        "value='" + name + "' required>"
        "<small>Project and Runtime Snapshot assignment are owned by the "
        "StageCore Hub after pairing.</small>";
  } else {
    page +=
        "<small>Only Stage LAN credentials are replaced. Device identity, "
        "display name, trusted Hub binding and StageCore assignment are "
        "preserved.</small>";
  }

  page +=
      "<button type='submit'>Save and restart</button></form></body></html>";

  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, page.c_str(), page.size());
}

esp_err_t save_handler(httpd_req_t *req) {
  if (req->content_len == 0 || req->content_len > 1024) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid form");
    return ESP_FAIL;
  }

  std::string body(req->content_len, '\0');
  size_t received = 0;
  while (received < req->content_len) {
    const int rc = httpd_req_recv(req, body.data() + received,
                                  req->content_len - received);
    if (rc <= 0) {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "receive failed");
      return ESP_FAIL;
    }
    received += static_cast<size_t>(rc);
  }

  auto *ctx = static_cast<PortalContext *>(req->user_ctx);
  const bool recovery = ctx != nullptr && ctx->recovery;
  DeviceConfig config;
  if (recovery) {
    const esp_err_t load_err = load_device_config(&config);
    if (load_err != ESP_OK || !config.complete()) {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "stored configuration unavailable");
      return load_err == ESP_OK ? ESP_ERR_INVALID_STATE : load_err;
    }
  }

  config.wifi_ssid = form_value(body, "ssid");
  config.wifi_password = form_value(body, "password");
  if (!recovery) config.display_name = form_value(body, "display_name");

  if (!config.complete() || config.wifi_ssid.size() > 32 ||
      config.wifi_password.size() > 63 || config.display_name.size() > 64) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid configuration");
    return ESP_FAIL;
  }

  const esp_err_t err = save_device_config(config);
  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    return err;
  }

  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  httpd_resp_sendstr(req, "Saved. StageLaser is restarting.");
  vTaskDelay(pdMS_TO_TICKS(250));
  esp_restart();
  return ESP_OK;
}

esp_err_t init_provisioning_ap(const std::string &ssid,
                               const std::string &password,
                               bool coexist_with_station) {
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

  if (esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") == nullptr &&
      esp_netif_create_default_wifi_ap() == nullptr) {
    return ESP_FAIL;
  }

  wifi_mode_t existing_mode = WIFI_MODE_NULL;
  const bool wifi_initialized = esp_wifi_get_mode(&existing_mode) == ESP_OK;
  if (!wifi_initialized) {
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
  }

  wifi_config_t ap{};
  std::memcpy(ap.ap.ssid, ssid.data(),
              std::min(ssid.size(), sizeof(ap.ap.ssid) - 1));
  ap.ap.ssid_len = static_cast<uint8_t>(
      std::min(ssid.size(), sizeof(ap.ap.ssid) - 1));
  std::memcpy(ap.ap.password, password.data(),
              std::min(password.size(), sizeof(ap.ap.password) - 1));
  ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
  ap.ap.max_connection = 4;

  err = esp_wifi_set_mode(
      coexist_with_station ? WIFI_MODE_APSTA : WIFI_MODE_AP);
  if (err != ESP_OK) return err;
  err = esp_wifi_set_config(WIFI_IF_AP, &ap);
  if (err != ESP_OK) return err;

  // Recovery is entered only after station networking has already started.
  if (coexist_with_station) return ESP_OK;
  return esp_wifi_start();
}

}  // namespace

[[noreturn]] void run_provisioning_portal(
    const std::string &device_id,
    const std::string &default_display_name) {
  const std::string ssid = "StageLaser-" + id_suffix(device_id);
  const std::string password = effective_setup_ap_password();

  const esp_err_t network = init_provisioning_ap(ssid, password, false);
  if (network != ESP_OK) {
    ESP_LOGE(kTag, "unable to start provisioning AP: %s",
             esp_err_to_name(network));
    std::abort();
  }

  static PortalContext ctx;
  ctx.default_display_name = default_display_name;
  ctx.recovery = false;

  httpd_config_t http = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = nullptr;
  ESP_ERROR_CHECK(httpd_start(&server, &http));

  httpd_uri_t root{};
  root.uri = "/";
  root.method = HTTP_GET;
  root.handler = root_handler;
  root.user_ctx = &ctx;
  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &root));

  httpd_uri_t save{};
  save.uri = "/save";
  save.method = HTTP_POST;
  save.handler = save_handler;
  save.user_ctx = &ctx;
  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &save));
  ESP_LOGW(kTag, "first-run provisioning AP SSID=%s", ssid.c_str());
  ESP_LOGW(kTag,
           "relay remains NO-ACTUATION; provisioning cannot assign a Project");

  while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

esp_err_t run_recovery_portal(
    const std::string &device_id,
    const std::string &display_name) {
  const std::string ssid =
      "StageLaser-Recovery-" + id_suffix(device_id);
  const std::string password = effective_setup_ap_password();

  const esp_err_t network =
      init_provisioning_ap(ssid, password, true);
  if (network != ESP_OK) {
    ESP_LOGE(kTag, "unable to start recovery AP: %s",
             esp_err_to_name(network));
    return network;
  }

  PortalContext ctx;
  ctx.default_display_name = display_name;
  ctx.recovery = true;

  httpd_config_t http = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = nullptr;
  esp_err_t err = httpd_start(&server, &http);
  if (err != ESP_OK) return err;

  httpd_uri_t root{};
  root.uri = "/";
  root.method = HTTP_GET;
  root.handler = root_handler;
  root.user_ctx = &ctx;
  err = httpd_register_uri_handler(server, &root);
  if (err != ESP_OK) {
    httpd_stop(server);
    return err;
  }

  httpd_uri_t save{};
  save.uri = "/save";
  save.method = HTTP_POST;
  save.handler = save_handler;
  save.user_ctx = &ctx;
  err = httpd_register_uri_handler(server, &save);
  if (err != ESP_OK) {
    httpd_stop(server);
    return err;
  }

  ESP_LOGW(kTag, "Stage LAN recovery AP available: SSID=%s",
           ssid.c_str());
  ESP_LOGW(kTag,
           "recovery changes network credentials only; no relay actuation");

  while (wait_for_station_connection(1000) != ESP_OK) {
    // Keep the bounded reconnect policy active in APSTA mode. Saving new
    // credentials restarts the device from save_handler.
  }

  httpd_stop(server);
  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err == ESP_OK) {
    ESP_LOGI(kTag, "Stage LAN recovered; recovery AP disabled");
  }
  return err;
}

}  // namespace stagecore
