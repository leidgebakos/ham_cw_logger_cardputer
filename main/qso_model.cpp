#include "qso_model.hpp"

#include <cstdlib>
#include <string>

namespace ham::logger {

std::string_view band_from_frequency(std::string_view frequency_mhz) {
  const std::string value(frequency_mhz);
  char *end = nullptr;
  const double mhz = std::strtod(value.c_str(), &end);
  if (end == value.c_str() || *end != '\0') return "?";
  if (mhz >= 1.8 && mhz <= 2.0) return "160m";
  if (mhz >= 3.5 && mhz <= 4.0) return "80m";
  if (mhz >= 5.25 && mhz <= 5.45) return "60m";
  if (mhz >= 7.0 && mhz <= 7.3) return "40m";
  if (mhz >= 10.1 && mhz <= 10.15) return "30m";
  if (mhz >= 14.0 && mhz <= 14.35) return "20m";
  if (mhz >= 18.068 && mhz <= 18.168) return "17m";
  if (mhz >= 21.0 && mhz <= 21.45) return "15m";
  if (mhz >= 24.89 && mhz <= 24.99) return "12m";
  if (mhz >= 28.0 && mhz <= 29.7) return "10m";
  if (mhz >= 50.0 && mhz <= 54.0) return "6m";
  return "?";
}

std::optional<QsoRecord> make_qso_record(const QsoDraft &draft, std::time_t timestamp_utc) {
  if (draft.callsign.empty() || draft.frequency_mhz.empty() ||
      band_from_frequency(draft.frequency_mhz) == "?" || timestamp_utc <= 0) {
    return std::nullopt;
  }
  return QsoRecord{
      .timestamp_utc = timestamp_utc,
      .callsign = draft.callsign,
      .frequency_mhz = draft.frequency_mhz,
      .band = std::string(band_from_frequency(draft.frequency_mhz)),
      .rst_sent = draft.rst_sent,
      .rst_received = draft.rst_received,
      .pota_reference = draft.pota_reference,
  };
}

} // namespace ham::logger
