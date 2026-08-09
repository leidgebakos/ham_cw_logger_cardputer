#include "app_settings.hpp"

#include <vector>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace ham::logger {
namespace {

constexpr char TAG[] = "settings_store";
constexpr char NAMESPACE[] = "cwlogger";

bool read_string(nvs_handle_t handle, const char *key, std::string &value) {
  size_t required = 0;
  esp_err_t error = nvs_get_str(handle, key, nullptr, &required);
  if (error == ESP_ERR_NVS_NOT_FOUND) {
    value.clear();
    return true;
  }
  if (error != ESP_OK || required == 0) return false;

  std::vector<char> buffer(required);
  error = nvs_get_str(handle, key, buffer.data(), &required);
  if (error != ESP_OK) return false;
  value.assign(buffer.data());
  return true;
}

} // namespace

bool SettingsStore::initialize() {
  esp_err_t error = nvs_flash_init();
  if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS requires reinitialization");
    if (nvs_flash_erase() != ESP_OK) return false;
    error = nvs_flash_init();
  }
  initialized_ = error == ESP_OK;
  if (!initialized_) ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(error));
  return initialized_;
}

bool SettingsStore::load(AppSettings &settings) const {
  if (!initialized_) return false;
  nvs_handle_t handle = 0;
  esp_err_t error = nvs_open(NAMESPACE, NVS_READONLY, &handle);
  if (error == ESP_ERR_NVS_NOT_FOUND) return true;
  if (error != ESP_OK) return false;

  const bool ok = read_string(handle, "wifi_ssid", settings.wifi_ssid) &&
                  read_string(handle, "wifi_pass", settings.wifi_password);
  nvs_close(handle);
  return ok;
}

bool SettingsStore::save(const AppSettings &settings) const {
  if (!initialized_) return false;
  nvs_handle_t handle = 0;
  esp_err_t error = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
  if (error != ESP_OK) return false;

  error = nvs_set_str(handle, "wifi_ssid", settings.wifi_ssid.c_str());
  if (error == ESP_OK) {
    error = nvs_set_str(handle, "wifi_pass", settings.wifi_password.c_str());
  }
  if (error == ESP_OK) error = nvs_commit(handle);
  nvs_close(handle);
  if (error != ESP_OK) ESP_LOGE(TAG, "Settings save failed: %s", esp_err_to_name(error));
  return error == ESP_OK;
}

} // namespace ham::logger
