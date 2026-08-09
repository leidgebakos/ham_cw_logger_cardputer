#include "qso_model.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace ham::logger {
namespace {

long parse_frequency_khz(std::string_view frequency_khz) {
  const std::string value(frequency_khz);
  char *end = nullptr;
  const long khz = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0' || khz <= 0) return -1;
  return khz;
}

} // namespace

std::string_view band_from_frequency_khz(std::string_view frequency_khz) {
  const long khz = parse_frequency_khz(frequency_khz);
  if (khz >= 1800 && khz <= 2000) return "160m";
  if (khz >= 3500 && khz <= 4000) return "80m";
  if (khz >= 5250 && khz <= 5450) return "60m";
  if (khz >= 7000 && khz <= 7300) return "40m";
  if (khz >= 10100 && khz <= 10150) return "30m";
  if (khz >= 14000 && khz <= 14350) return "20m";
  if (khz >= 18068 && khz <= 18168) return "17m";
  if (khz >= 21000 && khz <= 21450) return "15m";
  if (khz >= 24890 && khz <= 24990) return "12m";
  if (khz >= 28000 && khz <= 29700) return "10m";
  if (khz >= 50000 && khz <= 54000) return "6m";
  return "?";
}

std::string frequency_mhz_from_khz(std::string_view frequency_khz) {
  const long khz = parse_frequency_khz(frequency_khz);
  if (khz < 0) return {};
  char result[16]{};
  std::snprintf(result, sizeof(result), "%ld.%03ld", khz / 1000, khz % 1000);
  return result;
}

std::optional<QsoRecord> make_qso_record(const QsoDraft &draft, std::time_t timestamp_utc) {
  if (draft.callsign.empty() || draft.frequency_khz.empty() ||
      band_from_frequency_khz(draft.frequency_khz) == "?" || timestamp_utc <= 0) {
    return std::nullopt;
  }
  return QsoRecord{
      .timestamp_utc = timestamp_utc,
      .callsign = draft.callsign,
      .frequency_khz = draft.frequency_khz,
      .band = std::string(band_from_frequency_khz(draft.frequency_khz)),
      .rst_sent = draft.rst_sent,
      .rst_received = draft.rst_received,
      .pota_reference = draft.pota_reference,
  };
}

} // namespace ham::logger
