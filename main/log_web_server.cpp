#include "log_web_server.hpp"

#include <string_view>

#include "esp_log.h"

namespace ham::logger {
namespace {

constexpr char TAG[] = "log_web";
constexpr std::string_view PAGE = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>HAM CW Logger</title><style>
body{font-family:sans-serif;max-width:34rem;margin:3rem auto;padding:0 1rem;background:#eef2f5;color:#17212b}
main{background:white;padding:2rem;border-radius:.6rem}a{display:inline-block;padding:.8rem 1rem;background:#0057b8;color:white;text-decoration:none;border-radius:.3rem}
</style></head><body><main><h1>HAM CW Logger</h1><p>Download the LOG4OM-compatible ADIF log.</p>
<a href="/cw_log.adi" download>Download cw_log.adi</a></main></body></html>)HTML";

} // namespace

bool LogWebServer::start(AdifLog &log) {
  if (running_.load()) return true;
  log_ = &log;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 6144;
  config.max_uri_handlers = 4;
  if (httpd_start(&server_, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Could not start HTTP server");
    server_ = nullptr;
    return false;
  }

  httpd_uri_t root{};
  root.uri = "/";
  root.method = HTTP_GET;
  root.handler = root_handler;
  root.user_ctx = this;

  httpd_uri_t download{};
  download.uri = "/cw_log.adi";
  download.method = HTTP_GET;
  download.handler = download_handler;
  download.user_ctx = this;

  if (httpd_register_uri_handler(server_, &root) != ESP_OK ||
      httpd_register_uri_handler(server_, &download) != ESP_OK) {
    ESP_LOGE(TAG, "Could not register HTTP handlers");
    httpd_stop(server_);
    server_ = nullptr;
    return false;
  }

  running_.store(true);
  ESP_LOGI(TAG, "Read-only ADIF download server started");
  return true;
}

esp_err_t LogWebServer::root_handler(httpd_req_t *request) {
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, PAGE.data(), PAGE.size());
}

esp_err_t LogWebServer::download_handler(httpd_req_t *request) {
  auto *server = static_cast<LogWebServer *>(request->user_ctx);
  if (!server || !server->log_ || !server->log_->ready()) {
    httpd_resp_set_status(request, "503 Service Unavailable");
    return httpd_resp_sendstr(request, "SD card or ADIF log is not ready.");
  }

  httpd_resp_set_type(request, "application/octet-stream");
  httpd_resp_set_hdr(request, "Content-Disposition", "attachment; filename=\"cw_log.adi\"");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  const bool sent = server->log_->stream([request](const char *data, size_t size) {
    return httpd_resp_send_chunk(request, data, size) == ESP_OK;
  });
  if (!sent) {
    ESP_LOGE(TAG, "ADIF download failed");
    httpd_resp_send_chunk(request, nullptr, 0);
    return ESP_FAIL;
  }
  return httpd_resp_send_chunk(request, nullptr, 0);
}

} // namespace ham::logger
