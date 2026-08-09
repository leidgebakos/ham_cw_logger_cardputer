#pragma once

#include <string>

namespace ham::logger {

struct AppSettings {
  std::string wifi_ssid;
  std::string wifi_password;
};

class SettingsStore {
public:
  bool initialize();
  bool load(AppSettings &settings) const;
  bool save(const AppSettings &settings) const;

private:
  bool initialized_{false};
};

} // namespace ham::logger
