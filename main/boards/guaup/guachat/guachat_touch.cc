#include "guachat_touch.h"

#include "config.h"
#include "guachat_tca9535.h"

#include <esp_lcd_io_i2c.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_touch_ft5x06.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdlib>

#define TAG "GuachatTouch"

namespace {

// FT6336U reports a raw coordinate grid that matches the panel one-to-one, so
// no scaling is needed - only the horizontal flip, because the panel runs with
// MADCTL mirror_x set (see DISPLAY_MIRROR_X in config.h). Touch is a separate
// chip with its own fixed orientation, so the flip has to be applied in
// software here; esp_lcd_touch does it for us when flags.mirror_x is set.
constexpr uint16_t kXMax = DISPLAY_WIDTH - 1;
constexpr uint16_t kYMax = DISPLAY_HEIGHT - 1;

// See GuachatTouch::arm_at_ms_.
constexpr uint32_t kArmDelayMs = 2000;

}  // namespace

GuachatTouch::GuachatTouch(GuachatTca9535* expander, uint8_t addr)
    : expander_(expander), addr_(addr) {}

GuachatTouch::~GuachatTouch() {
    if (indev_ != nullptr) {
        // Caller is responsible for holding the LVGL lock if this ever runs
        // after startup; in practice the board never tears touch down.
        lv_indev_delete(indev_);
        indev_ = nullptr;
    }
    if (touch_ != nullptr) {
        esp_lcd_touch_del(touch_);
        touch_ = nullptr;
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
        panel_io_ = nullptr;
    }
    if (i2c_bus_ != nullptr) {
        i2c_del_master_bus(i2c_bus_);
        i2c_bus_ = nullptr;
    }
}

bool GuachatTouch::InitializeBus() {
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_1;
    bus_cfg.sda_io_num = BOARD_I2C1_SDA_PIN;
    bus_cfg.scl_io_num = BOARD_I2C1_SCL_PIN;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.intr_priority = 0;
    bus_cfg.trans_queue_depth = 0;
    bus_cfg.flags.enable_internal_pullup = 1;

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &i2c_bus_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C1 init failed: %s", esp_err_to_name(err));
        i2c_bus_ = nullptr;
        return false;
    }
    return true;
}

// The reset line lives on the expander. The FT6336U needs a good 100 ms after
// release before it answers on I2C; 50 ms reads back chip_id 0x00. The guachat
// BSP settled on 200 ms and there is no reason to be more aggressive.
void GuachatTouch::PulseReset() {
    if (expander_ == nullptr || !expander_->IsPresent()) {
        return;
    }
    expander_->SetPin(BOARD_TCA9535_PIN_TOUCH_RST, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    expander_->SetPin(BOARD_TCA9535_PIN_TOUCH_RST, true);
    vTaskDelay(pdMS_TO_TICKS(200));
}

bool GuachatTouch::Initialize() {
    if (touch_ != nullptr) {
        return true;
    }
    if (!InitializeBus()) {
        return false;
    }

    PulseReset();

    // The component's default config runs the bus at 100 kHz; the guachat BSP
    // uses 400 kHz and the FT6336U is happy with it, so polling stays cheap.
    esp_lcd_panel_io_i2c_config_t io_config = {};
    io_config.dev_addr = addr_;
    io_config.scl_speed_hz = BOARD_I2C1_FREQ_HZ;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 0;
    io_config.lcd_cmd_bits = 8;
    io_config.flags.disable_control_phase = 1;

    esp_err_t err = esp_lcd_new_panel_io_i2c(i2c_bus_, &io_config, &panel_io_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel io init failed: %s", esp_err_to_name(err));
        return false;
    }

    esp_lcd_touch_config_t touch_cfg = {};
    touch_cfg.x_max = kXMax;
    touch_cfg.y_max = kYMax;
    touch_cfg.rst_gpio_num = GPIO_NUM_NC;  // reset via TCA9535 P15
    touch_cfg.int_gpio_num = GPIO_NUM_NC;  // polled from the LVGL read callback
    touch_cfg.levels.reset = 0;
    touch_cfg.levels.interrupt = 0;
    touch_cfg.flags.mirror_x = DISPLAY_MIRROR_X ? 1 : 0;
    touch_cfg.flags.mirror_y = DISPLAY_MIRROR_Y ? 1 : 0;
    touch_cfg.flags.swap_xy = DISPLAY_SWAP_XY ? 1 : 0;

    err = esp_lcd_touch_new_i2c_ft5x06(panel_io_, &touch_cfg, &touch_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "FT6336U not responding at 0x%02X (%s); touch disabled", addr_,
                 esp_err_to_name(err));
        touch_ = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "FT6336U ready at 0x%02X (I2C1 %d Hz), mirror_x=%d", addr_, BOARD_I2C1_FREQ_HZ,
             DISPLAY_MIRROR_X ? 1 : 0);
    return true;
}

void GuachatTouch::RegisterIndev() {
    if (touch_ == nullptr || indev_ != nullptr) {
        return;
    }
    indev_ = lv_indev_create();
    if (indev_ == nullptr) {
        ESP_LOGE(TAG, "lv_indev_create failed");
        return;
    }
    lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_, &GuachatTouch::ReadCallback);
    lv_indev_set_user_data(indev_, this);
    // Bind explicitly instead of relying on the implicit "default display",
    // which would silently retarget if a second display is ever added.
    lv_display_t* disp = lv_display_get_default();
    if (disp != nullptr) {
        lv_indev_set_display(indev_, disp);
    }
    // Stay deaf for two seconds. Boot is ~2.4 s from reset to idle, and a
    // phantom point from the reset pulse would otherwise be read as a tap and
    // bounce the device straight into Wi-Fi config mode.
    arm_at_ms_ = static_cast<uint32_t>(esp_log_timestamp()) + kArmDelayMs;
    ESP_LOGI(TAG, "LVGL pointer device registered (armed in %u ms)", kArmDelayMs);
}

void GuachatTouch::ReadCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* self = static_cast<GuachatTouch*>(lv_indev_get_user_data(indev));
    if (self == nullptr) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    self->HandleRead(data);
}

void GuachatTouch::HandleRead(lv_indev_data_t* data) {
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t points = 0;

    esp_lcd_touch_read_data(touch_);
    const bool touched = esp_lcd_touch_get_coordinates(touch_, &x, &y, nullptr, &points, 1);

    if (touched) {
        if (!pressed_) {
            pressed_ = true;
            dragged_ = false;
            start_x_ = x;
            start_y_ = y;
            start_ms_ = static_cast<uint32_t>(esp_log_timestamp());
        }
        last_x_ = x;
        last_y_ = y;
        // LVGL started scrolling the chat under this finger. Reading the scroll
        // object is more reliable than distance thresholds: a short drag can
        // stay within a few pixels yet still counts as a scroll, and a drag that
        // returns near its start would otherwise pass the net-displacement test.
        if (indev_ != nullptr && lv_indev_get_scroll_obj(indev_) != nullptr) {
            dragged_ = true;
        }
        data->point.x = static_cast<lv_coord_t>(x);
        data->point.y = static_cast<lv_coord_t>(y);
        data->state = LV_INDEV_STATE_PRESSED;
        return;
    }

    if (pressed_) {
        pressed_ = false;
        EmitTap();
    }
    // Report the last known position on release so a consumer that tracks
    // coordinates sees the finger come up where it actually lifted.
    data->point.x = static_cast<lv_coord_t>(last_x_);
    data->point.y = static_cast<lv_coord_t>(last_y_);
    data->state = LV_INDEV_STATE_RELEASED;
}

void GuachatTouch::EmitTap() {
    if (static_cast<uint32_t>(esp_log_timestamp()) < arm_at_ms_) {
        return;
    }

    const int dx = static_cast<int>(last_x_) - static_cast<int>(start_x_);
    const int dy = static_cast<int>(last_y_) - static_cast<int>(start_y_);
    const uint32_t elapsed = static_cast<uint32_t>(esp_log_timestamp()) - start_ms_;

    // Only a finger that stayed put and did not linger counts as a tap. Anything
    // else is a drag, which LVGL has already used to scroll the chat area -
    // and once LVGL scrolled, the gesture is a drag no matter how little the
    // finger moved overall.
    if (!dragged_ && std::abs(dx) < kTapMaxTravelPx && std::abs(dy) < kTapMaxTravelPx &&
        elapsed < kTapMaxDurationMs) {
        ESP_LOGI(TAG, "tap at (%u,%u) in %ums", last_x_, last_y_, static_cast<unsigned>(elapsed));
        if (tap_handler_) {
            tap_handler_();
        }
        return;
    }

    ESP_LOGD(TAG, "drag dx=%d dy=%d in %ums", dx, dy, static_cast<unsigned>(elapsed));
}
