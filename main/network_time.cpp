#include "network_time.hpp"

#include <cstring>
#include <cstdio>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"

namespace ham::logger {
namespace {

constexpr char TAG[] = "network_time";
constexpr unsigned MAXIMUM_RETRIES = 5;
constexpr int MINIMUM_VALID_YEAR = 2024;

bool check(esp_err_t error, const char *operation) {
  if (error == ESP_OK) return true;
  ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(error));
  return false;
}

} // namespace

bool NetworkTimeService::initialize() {
  if (initialized_) return true;

  if (!check(esp_netif_init(), "esp_netif_init")) return false;
  esp_err_t error = esp_event_loop_create_default();
  if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
    return check(error, "esp_event_loop_create_default");
  }
  if (!esp_netif_create_default_wifi_sta()) {
    ESP_LOGE(TAG, "Could not create the default WiFi station interface");
    return false;
  }

  wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
  if (!check(esp_wifi_init(&wifi_init), "esp_wifi_init") ||
      !check(esp_wifi_set_storage(WIFI_STORAGE_RAM), "esp_wifi_set_storage") ||
      !check(esp_wifi_set_mode(WIFI_MODE_STA), "esp_wifi_set_mode") ||
      !check(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, this),
             "register WiFi event handler") ||
      !check(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, this),
             "register IP event handler")) {
    return false;
  }

  esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  sntp_config.start = false;
  if (!check(esp_netif_sntp_init(&sntp_config), "esp_netif_sntp_init")) return false;

  initialized_ = true;
  ESP_LOGI(TAG, "WiFi station and NTP service initialized");
  return true;
}

bool NetworkTimeService::configure(const AppSettings &settings) {
  if (!initialized_) return false;
  if (settings.wifi_ssid.size() > 32 || settings.wifi_password.size() > 63) {
    ESP_LOGE(TAG, "WiFi credentials exceed ESP32 limits");
    state_.store(NetworkState::FAILED);
    return false;
  }

  enabled_.store(false);
  connected_.store(false);
  ipv4_address_.store(0);
  retry_count_.store(0);
  if (wifi_started_) {
    const esp_err_t error = esp_wifi_disconnect();
    if (error != ESP_OK && error != ESP_ERR_WIFI_NOT_CONNECT) {
      ESP_LOGW(TAG, "WiFi disconnect before reconfigure: %s", esp_err_to_name(error));
    }
  }

  if (settings.wifi_ssid.empty()) {
    state_.store(NetworkState::DISABLED);
    if (wifi_started_) {
      if (!check(esp_wifi_stop(), "esp_wifi_stop")) return false;
      wifi_started_ = false;
    }
    ESP_LOGI(TAG, "WiFi disabled because no SSID is configured");
    return true;
  }

  wifi_config_t config{};
  std::memcpy(config.sta.ssid, settings.wifi_ssid.data(), settings.wifi_ssid.size());
  std::memcpy(config.sta.password, settings.wifi_password.data(),
              settings.wifi_password.size());
  config.sta.threshold.authmode = WIFI_AUTH_OPEN;
  config.sta.pmf_cfg.capable = true;
  config.sta.pmf_cfg.required = false;

  if (!check(esp_wifi_set_config(WIFI_IF_STA, &config), "esp_wifi_set_config")) {
    state_.store(NetworkState::FAILED);
    return false;
  }

  enabled_.store(true);
  state_.store(NetworkState::CONNECTING);
  if (!wifi_started_) {
    if (!check(esp_wifi_start(), "esp_wifi_start")) {
      enabled_.store(false);
      state_.store(NetworkState::FAILED);
      return false;
    }
    wifi_started_ = true;
  } else if (!check(esp_wifi_connect(), "esp_wifi_connect")) {
    state_.store(NetworkState::FAILED);
    return false;
  }

  ESP_LOGI(TAG, "Connecting to configured WiFi SSID");
  return true;
}

NetworkTimeStatus NetworkTimeService::status() const {
  NetworkTimeStatus result{
      .network_state = state_.load(),
      .wifi_connected = connected_.load(),
      .utc_valid = false,
  };
  std::tm utc{};
  result.utc_valid = utc_now(utc);
  return result;
}

bool NetworkTimeService::utc_now(std::tm &utc) const {
  const std::time_t now = std::time(nullptr);
  if (!gmtime_r(&now, &utc)) return false;
  return utc.tm_year + 1900 >= MINIMUM_VALID_YEAR;
}

std::string NetworkTimeService::ipv4_address() const {
  const uint32_t address = ipv4_address_.load();
  if (address == 0) return {};
  char text[16]{};
  std::snprintf(text, sizeof(text), "%u.%u.%u.%u",
                static_cast<unsigned>(address & 0xff),
                static_cast<unsigned>((address >> 8) & 0xff),
                static_cast<unsigned>((address >> 16) & 0xff),
                static_cast<unsigned>((address >> 24) & 0xff));
  return text;
}

void NetworkTimeService::event_handler(void *argument, esp_event_base_t event_base,
                                       int32_t event_id, void *event_data) {
  static_cast<NetworkTimeService *>(argument)->handle_event(event_base, event_id, event_data);
}

void NetworkTimeService::handle_event(esp_event_base_t event_base, int32_t event_id,
                                      void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    if (enabled_.load() && esp_wifi_connect() != ESP_OK) {
      state_.store(NetworkState::FAILED);
    }
    return;
  }

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    connected_.store(false);
    ipv4_address_.store(0);
    if (!enabled_.load()) return;

    const unsigned retry = retry_count_.fetch_add(1) + 1;
    if (retry <= MAXIMUM_RETRIES) {
      state_.store(NetworkState::CONNECTING);
      ESP_LOGW(TAG, "WiFi disconnected; retry %u/%u", retry, MAXIMUM_RETRIES);
      if (esp_wifi_connect() != ESP_OK) state_.store(NetworkState::FAILED);
    } else {
      state_.store(NetworkState::FAILED);
      ESP_LOGE(TAG, "WiFi connection failed after %u retries", MAXIMUM_RETRIES);
    }
    return;
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    const auto *event = static_cast<const ip_event_got_ip_t *>(event_data);
    connected_.store(true);
    ipv4_address_.store(event->ip_info.ip.addr);
    retry_count_.store(0);
    state_.store(NetworkState::ONLINE);
    ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));

    const esp_err_t error = esp_netif_sntp_start();
    if (error == ESP_OK) {
      state_.store(NetworkState::SYNCING);
      ESP_LOGI(TAG, "NTP synchronization started");
    } else {
      ESP_LOGE(TAG, "Could not start NTP: %s", esp_err_to_name(error));
    }
  }
}

} // namespace ham::logger
