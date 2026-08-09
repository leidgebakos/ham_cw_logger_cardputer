#pragma once

#include <ctime>
#include <optional>
#include <string>
#include <string_view>

namespace ham::logger {

struct QsoDraft {
  std::string callsign;
  std::string frequency_khz;
  std::string rst_sent;
  std::string rst_received;
  std::string pota_reference;
};

struct QsoRecord {
  std::time_t timestamp_utc;
  std::string callsign;
  std::string frequency_khz;
  std::string band;
  std::string rst_sent;
  std::string rst_received;
  std::string pota_reference;
};

std::string_view band_from_frequency_khz(std::string_view frequency_khz);
std::string frequency_mhz_from_khz(std::string_view frequency_khz);
std::optional<QsoRecord> make_qso_record(const QsoDraft &draft, std::time_t timestamp_utc);

} // namespace ham::logger
