#ifndef GUACHAT_TOUCH_H
#define GUACHAT_TOUCH_H

#include <driver/i2c_master.h>
#include <esp_lcd_touch.h>
#include <lvgl.h>

#include <functional>

class GuachatTca9535;

// FT6336U capacitive touch for the guachat board.
//
// The controller sits on its own bus (I2C1, SDA 47 / SCL 48, 400 kHz, 0x38)
// and is reset through TCA9535 P15, not a GPIO. It is register-compatible
// with the FT5x06 family, so esp_lcd_touch_ft5x06 drives it unchanged.
//
// The pointer device itself scrolls the chat area like any other LVGL indev.
// XiaoZhi's UI has no interactive widgets, so a deliberate tap is recognised
// separately and reported through a callback; drags are left to LVGL.
class GuachatTouch {
public:
    using TapHandler = std::function<void()>;

    // Tuning for tap recognition, in display pixels and milliseconds.
    static constexpr int kTapMaxTravelPx = 24;
    static constexpr int kTapMaxDurationMs = 500;

    GuachatTouch(GuachatTca9535* expander, uint8_t addr = 0x38);
    ~GuachatTouch();

    // Brings up I2C1, resets the controller and creates the esp_lcd_touch
    // handle. Returns false when the chip does not answer; the board keeps
    // working without touch in that case.
    bool Initialize();

    bool IsReady() const { return touch_ != nullptr; }

    // Creates the LVGL pointer device. Must be called with the LVGL lock held
    // and after the display has been registered.
    void RegisterIndev();

    void SetTapHandler(TapHandler handler) { tap_handler_ = std::move(handler); }

private:
    GuachatTca9535* expander_;
    uint8_t addr_;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_touch_handle_t touch_ = nullptr;
    lv_indev_t* indev_ = nullptr;
    TapHandler tap_handler_;

    // Press state, owned by the LVGL task via the read callback.
    bool pressed_ = false;
    // Set once LVGL starts scrolling during this press. A finger that moved the
    // chat is a drag even if the net displacement ends up small (a short scroll
    // stays under kTapMaxTravelPx), so this is checked before the distance test.
    bool dragged_ = false;
    uint16_t start_x_ = 0;
    uint16_t start_y_ = 0;
    uint16_t last_x_ = 0;
    uint16_t last_y_ = 0;
    uint32_t start_ms_ = 0;
    // Taps are ignored until this timestamp. The controller can report a
    // phantom point right after its reset pulse, and a tap landing before the
    // device leaves kDeviceStateStarting would send it into Wi-Fi config mode.
    uint32_t arm_at_ms_ = 0;

    bool InitializeBus();
    void PulseReset();

    static void ReadCallback(lv_indev_t* indev, lv_indev_data_t* data);
    void HandleRead(lv_indev_data_t* data);
    void EmitTap();
};

#endif  // GUACHAT_TOUCH_H
