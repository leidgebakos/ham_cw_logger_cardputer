#include <chrono>
#include <cstdio>
#include <mutex>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "m5stack-cardputer.hpp"
#include "task.hpp"

using namespace std::chrono_literals;

namespace {

constexpr char TAG[] = "cardputer_smoke";
std::recursive_mutex ui_mutex;
lv_obj_t *input_area = nullptr;
lv_obj_t *status_label = nullptr;

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
    .task_config =
        {
            .name = "smoke_ui",
            .stack_size_bytes = 6 * 1024,
        },
});

void create_ui() {
  std::lock_guard<std::recursive_mutex> lock(ui_mutex);

  lv_obj_t *background = lv_obj_create(lv_screen_active());
  lv_obj_set_size(background, lv_pct(100), lv_pct(100));
  lv_obj_center(background);
  lv_obj_set_style_bg_color(background, lv_color_hex(0xFFD000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(background, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(background, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(background, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(background, 3, LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(background);
  lv_label_set_text(title, "CARDPUTER ADV - KEYBOARD TEST");
  lv_obj_set_style_text_color(title, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

  input_area = lv_textarea_create(background);
  lv_obj_set_size(input_area, lv_pct(100), 72);
  lv_obj_align(input_area, LV_ALIGN_TOP_MID, 0, 20);
  lv_textarea_set_placeholder_text(input_area, "Type here...");
  lv_textarea_set_one_line(input_area, false);
  lv_obj_set_style_bg_color(input_area, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(input_area, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(input_area, lv_color_hex(0x0040FF), LV_PART_MAIN);
  lv_obj_set_style_border_width(input_area, 2, LV_PART_MAIN);
  lv_obj_set_style_text_color(input_area, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_remove_flag(input_area, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_state(input_area, LV_STATE_FOCUSED);

  status_label = lv_label_create(background);
  lv_obj_set_width(status_label, lv_pct(100));
  lv_label_set_long_mode(status_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(status_label, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_label_set_text(status_label, "Initializing keyboard...");
}

void set_status(const char *text) {
  std::lock_guard<std::recursive_mutex> lock(ui_mutex);
  if (status_label) {
    lv_label_set_text(status_label, text);
  }
}

void handle_key(const espp::M5StackCardputer::KeyEvent &event) {
  if (!event.pressed) {
    return;
  }

  std::lock_guard<std::recursive_mutex> lock(ui_mutex);

  if (event.special != espp::M5StackCardputer::SpecialKey::NONE) {
    switch (event.special) {
    case espp::M5StackCardputer::SpecialKey::LEFT:
      lv_textarea_cursor_left(input_area);
      break;
    case espp::M5StackCardputer::SpecialKey::RIGHT:
      lv_textarea_cursor_right(input_area);
      break;
    case espp::M5StackCardputer::SpecialKey::UP:
      lv_textarea_cursor_up(input_area);
      break;
    case espp::M5StackCardputer::SpecialKey::DOWN:
      lv_textarea_cursor_down(input_area);
      break;
    case espp::M5StackCardputer::SpecialKey::DELETE:
      lv_textarea_delete_char_forward(input_area);
      break;
    case espp::M5StackCardputer::SpecialKey::ESC:
      lv_textarea_set_text(input_area, "");
      break;
    default:
      break;
    }
    return;
  }

  if (event.value == '\b') {
    lv_textarea_delete_char(input_area);
  } else if (event.value != 0) {
    lv_textarea_add_char(input_area, event.value);
  }
}

} // namespace

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "Cardputer ADV display + keyboard smoke test starting");

  auto &cardputer = espp::M5StackCardputer::get();
  cardputer.set_log_level(espp::Logger::Verbosity::INFO);

  if (!cardputer.initialize_lcd()) {
    ESP_LOGE(TAG, "LCD initialization FAILED");
    return;
  }

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
    set_status("KEYBOARD INIT FAILED");
    return;
  }

  ESP_LOGI(TAG, "Detected board: %s",
           espp::M5StackCardputer::variant_name(cardputer.variant()));

  char status[96];
  while (true) {
    cardputer.brightness(100.0f);
    std::snprintf(status, sizeof(status), "%s  BAT %.2f V  %.0f%%",
                  espp::M5StackCardputer::variant_name(cardputer.variant()),
                  cardputer.battery_voltage(), cardputer.battery_soc());
    set_status(status);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
