#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <string>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "m5stack-cardputer.hpp"
#include "app_settings.hpp"
#include "qso_model.hpp"
#include "task.hpp"

using namespace std::chrono_literals;
using ham::logger::band_from_frequency;

namespace {

constexpr char TAG[] = "cw_logger_ui";

enum class Field : size_t {
  CALLSIGN,
  FREQUENCY,
  RST_SENT,
  RST_RECEIVED,
  POTA,
  COUNT,
};

constexpr size_t FIELD_COUNT = static_cast<size_t>(Field::COUNT);
constexpr std::array<const char *, FIELD_COUNT> FIELD_NAMES = {
    "CALL", "FREQ", "RST S", "RST R", "POTA"};

std::recursive_mutex ui_mutex;
std::array<std::string, FIELD_COUNT> values = {"", "7.032", "599", "599", ""};
std::array<size_t, FIELD_COUNT> cursors = {0, 5, 3, 3, 0};
std::array<lv_obj_t *, FIELD_COUNT> rows{};
std::array<lv_obj_t *, FIELD_COUNT> name_labels{};
std::array<lv_obj_t *, FIELD_COUNT> value_labels{};
std::array<bool, FIELD_COUNT> pristine_defaults = {false, true, true, true, false};
lv_obj_t *header_label = nullptr;
lv_obj_t *feedback_label = nullptr;
size_t active_field = 0;
std::string edit_original;
bool edit_original_was_default = false;
uint32_t mock_qso_count = 0;

ham::logger::SettingsStore settings_store;
ham::logger::AppSettings app_settings;
ham::logger::AppSettings settings_snapshot;
bool settings_store_ready = false;
bool settings_open = false;
lv_obj_t *settings_root = nullptr;
lv_obj_t *settings_feedback_label = nullptr;
std::array<lv_obj_t *, 3> settings_rows{};
std::array<lv_obj_t *, 3> settings_name_labels{};
std::array<lv_obj_t *, 3> settings_value_labels{};
std::array<size_t, 2> settings_cursors{};
size_t settings_active = 0;

espp::Task ui_task({
    .callback =
        [](std::mutex &mutex, std::condition_variable &cv) {
          {
            std::lock_guard<std::recursive_mutex> lock(ui_mutex);
            lv_task_handler();
          }
          std::unique_lock<std::mutex> lock(mutex);
          cv.wait_for(lock, 16ms);
          return false;
        },
    .task_config = {.name = "qso_ui", .stack_size_bytes = 6 * 1024},
});

std::string display_value(size_t index) {
  std::string text = values[index].empty() ? "-" : values[index];
  if (index == active_field) {
    const size_t cursor = values[index].empty() ? 0 : std::min(cursors[index], text.size());
    text.insert(cursor, "|");
  }
  if (index == static_cast<size_t>(Field::FREQUENCY)) {
    text += " MHz  ";
    text += std::string(band_from_frequency(values[index]));
  }
  return text;
}

void refresh_rows() {
  for (size_t i = 0; i < FIELD_COUNT; ++i) {
    const bool active = i == active_field;
    lv_obj_set_style_bg_color(rows[i],
                              lv_color_hex(active ? 0x0057B8 : 0xFFFFFF), LV_PART_MAIN);
    const lv_color_t text_color = lv_color_hex(active ? 0xFFFFFF : 0x000000);
    lv_obj_set_style_text_color(name_labels[i], text_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(value_labels[i], text_color, LV_PART_MAIN);
    const std::string text = display_value(i);
    lv_label_set_text(value_labels[i], text.c_str());
  }
}

void set_feedback(const char *text) {
  std::lock_guard<std::recursive_mutex> lock(ui_mutex);
  if (feedback_label) lv_label_set_text(feedback_label, text);
}

std::string &settings_value(size_t index) {
  return index == 0 ? app_settings.wifi_ssid : app_settings.wifi_password;
}

std::string settings_display_value(size_t index) {
  if (index == 2) return "[ ENTER TO SAVE ]";

  const std::string &value = settings_value(index);
  std::string text = index == 1 ? std::string(value.size(), '*') : value;
  if (text.empty()) text = "-";
  if (index == settings_active) {
    const size_t cursor = value.empty() ? 0 : std::min(settings_cursors[index], text.size());
    text.insert(cursor, "|");
  }
  return text;
}

void refresh_settings_rows() {
  for (size_t i = 0; i < settings_rows.size(); ++i) {
    const bool active = i == settings_active;
    lv_obj_set_style_bg_color(settings_rows[i],
                              lv_color_hex(active ? 0x0057B8 : 0xFFFFFF), LV_PART_MAIN);
    const lv_color_t text_color = lv_color_hex(active ? 0xFFFFFF : 0x000000);
    lv_obj_set_style_text_color(settings_name_labels[i], text_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(settings_value_labels[i], text_color, LV_PART_MAIN);
    const std::string text = settings_display_value(i);
    lv_label_set_text(settings_value_labels[i], text.c_str());
  }
}

void select_settings_field(size_t index) {
  settings_active = index % settings_rows.size();
  if (settings_active < settings_cursors.size()) {
    settings_cursors[settings_active] =
        std::min(settings_cursors[settings_active], settings_value(settings_active).size());
  }
  refresh_settings_rows();
}

void open_settings() {
  settings_snapshot = app_settings;
  settings_cursors[0] = app_settings.wifi_ssid.size();
  settings_cursors[1] = app_settings.wifi_password.size();
  settings_open = true;
  select_settings_field(0);
  lv_obj_remove_flag(settings_root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(settings_root);
  lv_label_set_text(settings_feedback_label, "TAB field  ENTER save  ESC back");
}

void close_settings(bool save) {
  if (save) {
    const bool ok = settings_store_ready && settings_store.save(app_settings);
    set_feedback(ok ? "WiFi settings saved" : "SETTINGS SAVE FAILED");
  } else {
    app_settings = settings_snapshot;
    set_feedback("Settings cancelled");
  }
  settings_open = false;
  lv_obj_add_flag(settings_root, LV_OBJ_FLAG_HIDDEN);
}

void refresh_settings_value() {
  refresh_settings_rows();
  lv_label_set_text(settings_feedback_label, "TAB field  ENTER save  ESC back");
}

void handle_settings_key(const espp::M5StackCardputer::KeyEvent &event) {
  if (event.special != espp::M5StackCardputer::SpecialKey::NONE) {
    switch (event.special) {
    case espp::M5StackCardputer::SpecialKey::LEFT:
      if (settings_active < 2 && settings_cursors[settings_active] > 0) {
        --settings_cursors[settings_active];
        refresh_settings_value();
      }
      break;
    case espp::M5StackCardputer::SpecialKey::RIGHT:
      if (settings_active < 2 &&
          settings_cursors[settings_active] < settings_value(settings_active).size()) {
        ++settings_cursors[settings_active];
        refresh_settings_value();
      }
      break;
    case espp::M5StackCardputer::SpecialKey::DELETE:
      if (settings_active < 2 &&
          settings_cursors[settings_active] < settings_value(settings_active).size()) {
        settings_value(settings_active).erase(settings_cursors[settings_active], 1);
        refresh_settings_value();
      }
      break;
    case espp::M5StackCardputer::SpecialKey::ESC:
      close_settings(false);
      break;
    default:
      break;
    }
    return;
  }

  if (event.value == '\t') {
    select_settings_field((settings_active + 1) % settings_rows.size());
  } else if (event.value == '\n' || event.value == '\r') {
    if (settings_active == 2) {
      close_settings(true);
    } else {
      select_settings_field(settings_active + 1);
    }
  } else if (event.value == '\b' && settings_active < 2) {
    std::string &value = settings_value(settings_active);
    if (settings_cursors[settings_active] > 0 && !value.empty()) {
      value.erase(settings_cursors[settings_active] - 1, 1);
      --settings_cursors[settings_active];
      refresh_settings_value();
    }
  } else if (settings_active < 2 && event.value >= 32 && event.value <= 126) {
    std::string &value = settings_value(settings_active);
    const size_t maximum = settings_active == 0 ? 32 : 63;
    if (value.size() < maximum) {
      value.insert(settings_cursors[settings_active], 1, event.value);
      ++settings_cursors[settings_active];
      refresh_settings_value();
    }
  }
}

void select_field(size_t new_field) {
  active_field = new_field % FIELD_COUNT;
  edit_original = values[active_field];
  edit_original_was_default = pristine_defaults[active_field];
  cursors[active_field] = std::min(cursors[active_field], values[active_field].size());
  refresh_rows();
}

bool character_allowed(size_t field, char value) {
  if (field == static_cast<size_t>(Field::FREQUENCY)) {
    return std::isdigit(static_cast<unsigned char>(value)) || value == '.';
  }
  if (field == static_cast<size_t>(Field::RST_SENT) ||
      field == static_cast<size_t>(Field::RST_RECEIVED)) {
    return std::isdigit(static_cast<unsigned char>(value));
  }
  return std::isalnum(static_cast<unsigned char>(value)) || value == '/' || value == '-';
}

size_t maximum_length(size_t field) {
  if (field == static_cast<size_t>(Field::CALLSIGN)) return 16;
  if (field == static_cast<size_t>(Field::FREQUENCY)) return 9;
  if (field == static_cast<size_t>(Field::RST_SENT) ||
      field == static_cast<size_t>(Field::RST_RECEIVED)) return 3;
  return 12;
}

void insert_character(char value) {
  if (!character_allowed(active_field, value)) return;
  if (pristine_defaults[active_field]) {
    values[active_field].clear();
    cursors[active_field] = 0;
    pristine_defaults[active_field] = false;
  }
  if (values[active_field].size() >= maximum_length(active_field)) return;
  if (active_field == static_cast<size_t>(Field::CALLSIGN) ||
      active_field == static_cast<size_t>(Field::POTA)) {
    value = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
  }
  values[active_field].insert(cursors[active_field], 1, value);
  ++cursors[active_field];
  refresh_rows();
}

void delete_character() {
  if (cursors[active_field] == 0 || values[active_field].empty()) return;
  values[active_field].erase(cursors[active_field] - 1, 1);
  --cursors[active_field];
  refresh_rows();
}

void save_mock_qso() {
  const ham::logger::QsoDraft draft{
      .callsign = values[static_cast<size_t>(Field::CALLSIGN)],
      .frequency_mhz = values[static_cast<size_t>(Field::FREQUENCY)],
      .rst_sent = values[static_cast<size_t>(Field::RST_SENT)],
      .rst_received = values[static_cast<size_t>(Field::RST_RECEIVED)],
      .pota_reference = values[static_cast<size_t>(Field::POTA)],
  };
  const auto record = ham::logger::make_qso_record(draft);
  if (!record) {
    set_feedback(draft.callsign.empty() ? "Enter a callsign" : "Invalid frequency");
    return;
  }

  ++mock_qso_count;
  char message[96];
  std::snprintf(message, sizeof(message), "#%lu saved: %s  %s %s",
                static_cast<unsigned long>(mock_qso_count),
                record->callsign.c_str(), record->frequency_mhz.c_str(),
                record->band.c_str());
  set_feedback(message);
  ESP_LOGI(TAG, "%s", message);

  values[static_cast<size_t>(Field::CALLSIGN)].clear();
  cursors[static_cast<size_t>(Field::CALLSIGN)] = 0;
  edit_original.clear();
  refresh_rows();
}

void handle_special_key(espp::M5StackCardputer::SpecialKey key) {
  switch (key) {
  case espp::M5StackCardputer::SpecialKey::UP:
    break;
  case espp::M5StackCardputer::SpecialKey::DOWN:
    break;
  case espp::M5StackCardputer::SpecialKey::LEFT:
    if (cursors[active_field] > 0) --cursors[active_field];
    refresh_rows();
    break;
  case espp::M5StackCardputer::SpecialKey::RIGHT:
    if (cursors[active_field] < values[active_field].size()) ++cursors[active_field];
    refresh_rows();
    break;
  case espp::M5StackCardputer::SpecialKey::DELETE:
    if (cursors[active_field] < values[active_field].size()) {
      values[active_field].erase(cursors[active_field], 1);
      refresh_rows();
    }
    break;
  case espp::M5StackCardputer::SpecialKey::ESC:
    if (active_field == static_cast<size_t>(Field::CALLSIGN) &&
        values[active_field].empty()) {
      open_settings();
      break;
    }
    values[active_field] = edit_original;
    pristine_defaults[active_field] = edit_original_was_default;
    cursors[active_field] = values[active_field].size();
    set_feedback("Edit cancelled");
    refresh_rows();
    break;
  default:
    break;
  }
}

void handle_key(const espp::M5StackCardputer::KeyEvent &event) {
  if (!event.pressed) return;
  std::lock_guard<std::recursive_mutex> lock(ui_mutex);

  if (settings_open) {
    handle_settings_key(event);
    return;
  }

  if (event.special != espp::M5StackCardputer::SpecialKey::NONE) {
    handle_special_key(event.special);
    return;
  }
  if (event.value == '\b') {
    delete_character();
  } else if (event.value == '\t') {
    select_field((active_field + 1) % FIELD_COUNT);
    set_feedback("TAB field   ENTER accept/save");
  } else if (event.value == '\n' || event.value == '\r') {
    if (active_field == static_cast<size_t>(Field::CALLSIGN)) {
      save_mock_qso();
    } else {
      select_field(static_cast<size_t>(Field::CALLSIGN));
      set_feedback("Setting accepted");
    }
  } else if (event.value != 0) {
    insert_character(event.value);
  }
}

void create_ui() {
  std::lock_guard<std::recursive_mutex> lock(ui_mutex);
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0xDDE4EA), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  header_label = lv_label_create(screen);
  lv_label_set_text(header_label, "CW LOGGER  BAT --");
  lv_obj_set_style_text_color(header_label, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_align(header_label, LV_ALIGN_TOP_MID, 0, 1);

  for (size_t i = 0; i < FIELD_COUNT; ++i) {
    rows[i] = lv_obj_create(screen);
    lv_obj_set_size(rows[i], 236, 18);
    lv_obj_set_pos(rows[i], 2, 17 + static_cast<int>(i) * 19);
    lv_obj_set_style_radius(rows[i], 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(rows[i], 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rows[i], 1, LV_PART_MAIN);
    lv_obj_remove_flag(rows[i], LV_OBJ_FLAG_SCROLLABLE);

    name_labels[i] = lv_label_create(rows[i]);
    lv_label_set_text(name_labels[i], FIELD_NAMES[i]);
    lv_obj_set_width(name_labels[i], 48);
    lv_obj_align(name_labels[i], LV_ALIGN_LEFT_MID, 1, 0);

    value_labels[i] = lv_label_create(rows[i]);
    lv_obj_set_width(value_labels[i], 178);
    lv_label_set_long_mode(value_labels[i], LV_LABEL_LONG_CLIP);
    lv_obj_align(value_labels[i], LV_ALIGN_RIGHT_MID, -1, 0);
  }

  lv_obj_t *footer = lv_obj_create(screen);
  lv_obj_set_size(footer, 240, 23);
  lv_obj_set_pos(footer, 0, 112);
  lv_obj_set_style_bg_color(footer, lv_color_hex(0xDDE4EA), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(footer, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(footer, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(footer, 0, LV_PART_MAIN);
  lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

  feedback_label = lv_label_create(footer);
  lv_obj_set_width(feedback_label, 226);
  lv_label_set_long_mode(feedback_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(feedback_label, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_align(feedback_label, LV_ALIGN_TOP_MID, 0, 1);
  lv_label_set_text(feedback_label, "TAB field   ENTER accept/save");

  settings_root = lv_obj_create(screen);
  lv_obj_set_size(settings_root, 240, 135);
  lv_obj_set_pos(settings_root, 0, 0);
  lv_obj_set_style_bg_color(settings_root, lv_color_hex(0xDDE4EA), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(settings_root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(settings_root, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(settings_root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(settings_root, 0, LV_PART_MAIN);
  lv_obj_remove_flag(settings_root, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *settings_title = lv_label_create(settings_root);
  lv_label_set_text(settings_title, "SETTINGS - WIFI");
  lv_obj_set_style_text_color(settings_title, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_align(settings_title, LV_ALIGN_TOP_MID, 0, 4);

  constexpr std::array<const char *, 3> SETTINGS_NAMES = {"SSID", "PASS", "SAVE"};
  for (size_t i = 0; i < settings_rows.size(); ++i) {
    settings_rows[i] = lv_obj_create(settings_root);
    lv_obj_set_size(settings_rows[i], 228, 20);
    lv_obj_set_pos(settings_rows[i], 6, 26 + static_cast<int>(i) * 24);
    lv_obj_set_style_radius(settings_rows[i], 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(settings_rows[i], 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(settings_rows[i], 1, LV_PART_MAIN);
    lv_obj_remove_flag(settings_rows[i], LV_OBJ_FLAG_SCROLLABLE);

    settings_name_labels[i] = lv_label_create(settings_rows[i]);
    lv_label_set_text(settings_name_labels[i], SETTINGS_NAMES[i]);
    lv_obj_set_width(settings_name_labels[i], 48);
    lv_obj_align(settings_name_labels[i], LV_ALIGN_LEFT_MID, 1, 0);

    settings_value_labels[i] = lv_label_create(settings_rows[i]);
    lv_obj_set_width(settings_value_labels[i], 168);
    lv_label_set_long_mode(settings_value_labels[i], LV_LABEL_LONG_CLIP);
    lv_obj_align(settings_value_labels[i], LV_ALIGN_RIGHT_MID, -1, 0);
  }

  settings_feedback_label = lv_label_create(settings_root);
  lv_obj_set_width(settings_feedback_label, 228);
  lv_label_set_long_mode(settings_feedback_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(settings_feedback_label, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_align(settings_feedback_label, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_label_set_text(settings_feedback_label, "TAB field  ENTER save  ESC back");
  refresh_settings_rows();
  lv_obj_add_flag(settings_root, LV_OBJ_FLAG_HIDDEN);

  edit_original = values[active_field];
  edit_original_was_default = pristine_defaults[active_field];
  refresh_rows();
}

void update_header(espp::M5StackCardputer &cardputer) {
  char header[64];
  std::snprintf(header, sizeof(header), "CW LOGGER   BAT %.2fV %.0f%%",
                cardputer.battery_voltage(), cardputer.battery_soc());
  std::lock_guard<std::recursive_mutex> lock(ui_mutex);
  lv_label_set_text(header_label, header);
}

} // namespace


extern "C" void app_main(void) {
  ESP_LOGI(TAG, "QSO entry usability prototype starting");
  settings_store_ready = settings_store.initialize();
  if (settings_store_ready && !settings_store.load(app_settings)) {
    ESP_LOGW(TAG, "Could not load saved settings; using empty WiFi credentials");
  }
  auto &cardputer = espp::M5StackCardputer::get();
  cardputer.set_log_level(espp::Logger::Verbosity::INFO);

  if (!cardputer.initialize_lcd()) {
    ESP_LOGE(TAG, "LCD initialization FAILED");
    return;
  }
  // The 135-pixel-tall visible window starts at row 53 in the ST7789's
  // 240-pixel GRAM. The BSP currently uses 52, leaving the physical bottom
  // row untouched and showing stale/random pixels.
  cardputer.display_driver()->set_offset(40, 53);
  ESP_LOGI(TAG, "Applied Cardputer ST7789 display offset correction: 40,53");
  constexpr size_t pixel_buffer_size = espp::M5StackCardputer::lcd_width() * 40;
  if (!cardputer.initialize_display(pixel_buffer_size)) {
    ESP_LOGE(TAG, "LVGL display initialization FAILED");
    return;
  }

  cardputer.brightness(100.0f);
  create_ui();
  ui_task.start();

  if (!cardputer.initialize_keyboard(handle_key)) {
    ESP_LOGE(TAG, "Keyboard initialization FAILED");
    set_feedback("KEYBOARD INIT FAILED");
    return;
  }

  ESP_LOGI(TAG, "Detected board: %s",
           espp::M5StackCardputer::variant_name(cardputer.variant()));
  while (true) {
    cardputer.brightness(100.0f);
    update_header(cardputer);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
