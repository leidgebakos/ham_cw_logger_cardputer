#pragma once

#include <atomic>
#include <string>

#include "qso_model.hpp"

namespace ham::logger {

class AdifLog {
public:
  bool initialize(std::string file_path);
  bool append(const QsoRecord &record);
  bool ready() const { return ready_.load(); }
  const std::string &file_path() const { return file_path_; }

private:
  bool write_header_if_needed();

  std::string file_path_;
  std::atomic<bool> ready_{false};
};

} // namespace ham::logger
