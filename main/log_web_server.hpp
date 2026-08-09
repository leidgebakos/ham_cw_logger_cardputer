#pragma once

#include <atomic>

#include "adif_log.hpp"
#include "esp_http_server.h"

namespace ham::logger {

class LogWebServer {
public:
  bool start(AdifLog &log);
  bool running() const { return running_.load(); }

private:
  static esp_err_t root_handler(httpd_req_t *request);
  static esp_err_t download_handler(httpd_req_t *request);

  AdifLog *log_{nullptr};
  httpd_handle_t server_{nullptr};
  std::atomic<bool> running_{false};
};

} // namespace ham::logger
