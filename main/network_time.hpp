#pragma once

#include <atomic>
#include <ctime>
#include <string>

#include "app_settings.hpp"
#include "esp_event_base.h"

namespace ham::logger {

enum class NetworkState {
  DISABLED,
  CONNECTING,
  ONLINE,
  SYNCING,
  FAILED,
};

struct NetworkTimeStatus {
  NetworkState network_state{NetworkState::DISABLED};
  bool wifi_connected{false};
  bool utc_valid{false};
};

class NetworkTimeService {
public:
  bool initialize();
  bool configure(const AppSettings &settings);
  NetworkTimeStatus status() const;
  bool utc_now(std::tm &utc) const;
  std::string ipv4_address() const;

private:
  static void event_handler(void *argument, esp_event_base_t event_base,
                            int32_t event_id, void *event_data);
  void handle_event(esp_event_base_t event_base, int32_t event_id, void *event_data);

  std::atomic<NetworkState> state_{NetworkState::DISABLED};
  std::atomic<bool> enabled_{false};
  std::atomic<bool> connected_{false};
  std::atomic<unsigned> retry_count_{0};
  std::atomic<uint32_t> ipv4_address_{0};
  bool initialized_{false};
  bool wifi_started_{false};
};

} // namespace ham::logger
