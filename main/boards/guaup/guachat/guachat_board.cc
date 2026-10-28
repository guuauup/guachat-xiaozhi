#include "wifi_board.h"
#include "display/lcd_display.h"
#include "codecs/no_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "assets/lang_config.h"
#include "guachat_tca9535.h"
#include "guachat_haptic.h"
#include "guachat_touch.h"
#include "guachat_acoustic_probe.h"
#include "guachat_ui_snapshot.h"
#include "guachat_task_report.h"
#include "max17055_battery_monitor.h"

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_st7796.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <string>

#define TAG "Guachat"

namespace {

// The guachat panel's backlight is a LEDC-driven white LED whose datasheet
// safe ceiling is 170/2047 (~8.3% duty). PwmBacklight maps 0-100 onto a
// 10-bit duty, so clamp the requested level before it reaches the hardware
// instead of letting a "100%" request overdrive the LED string.
class GuachatBacklight : public PwmBacklight {
public:
    GuachatBacklight()
        : PwmBacklight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT, 10000) {
    }

protected:
    void SetBrightnessImpl(uint8_t brightness) override {
        uint32_t scaled =
            (static_cast<uint32_t>(brightness) * DISPLAY_BACKLIGHT_MAX_PERCENT + 50) / 100;
        if (scaled > DISPLAY_BACKLIGHT_MAX_PERCENT) {
            scaled = DISPLAY_BACKLIGHT_MAX_PERCENT;
        }
        PwmBacklight::SetBrightnessImpl(static_cast<uint8_t>(scaled));
    }
};

}  // namespace

class GuachatBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    GuachatTca9535 expander_;
    // Allocated in the constructor body, after the I2C bus exists.
    GuachatHaptic* haptic_ = nullptr;
    GuachatTouch* touch_ = nullptr;
    LcdDisplay* display_ = nullptr;
    Max17055BatteryMonitor* battery_ = nullptr;
    Button* boot_button_ = nullptr;
    // Screen power. Only the backlight is blanked (see SetScreenOff); the flag
    // is atomic because the idle-timeout task polls it from another task.
    std::atomic<bool> screen_off_{false};
    uint8_t screen_brightness_ = 0;  // Restored on wake

    void InitializeI2c() {
        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.i2c_port = I2C_NUM_0;
        bus_cfg.sda_io_num = BOARD_I2C0_SDA_PIN;
        bus_cfg.scl_io_num = BOARD_I2C0_SCL_PIN;
        bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_cfg.glitch_ignore_cnt = 7;
        bus_cfg.intr_priority = 0;
        bus_cfg.trans_queue_depth = 0;
        bus_cfg.flags.enable_internal_pullup = 1;
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus_));
    }

    // Brings up the TCA9535, which owns the power latch, the display/touch
    // resets and the volume keys.
    //
    // ORDER MATTERS: the output latch must be written while the pins are still
    // inputs, and only then may the direction registers switch them to outputs.
    // P03 is the power latch (high = run) behind an AO3401 + GEK100 reset
    // supervisor; switching direction first would briefly drive the power-on
    // default (low) and shut the board down. This mirrors
    // guachat:src/I2C0/TCA9535.cpp.
    void InitializeExpander() {
        esp_err_t err = expander_.Initialize(i2c_bus_, BOARD_TCA9535_ADDR);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "TCA9535 unavailable; volume keys disabled, display uses software reset");
            return;
        }

        ESP_ERROR_CHECK(expander_.WriteOutputs(BOARD_TCA9535_POWER_ON_MASK));
        ESP_ERROR_CHECK(expander_.SetDirections(BOARD_TCA9535_INPUT_MASK));

        // P00/P01 are low-active keys with external pull-ups, so they must be
        // inputs. The version strap (P10) is already left as an input above.
        ESP_LOGI(TAG, "TCA9535 ready (power latch held, resets released)");
    }

    // MAX98357 SD_MODE: high enables the amplifier. NoAudioCodec does not own
    // an amplifier pin, so the board drives it.
    void InitializeAmplifier() {
        gpio_config_t io_cfg = {};
        io_cfg.pin_bit_mask = 1ULL << AUDIO_CODEC_PA_PIN;
        io_cfg.mode = GPIO_MODE_OUTPUT;
        io_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_cfg.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&io_cfg));
        gpio_set_level(AUDIO_CODEC_PA_PIN, 1);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SPI_SCK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        // The panel reset line is not a GPIO on this board; pulse it through
        // the expander before talking to the controller.
        if (expander_.IsPresent()) {
            expander_.SetPin(BOARD_TCA9535_PIN_DISPLAY_RST, false);
            vTaskDelay(pdMS_TO_TICKS(20));
            expander_.SetPin(BOARD_TCA9535_PIN_DISPLAY_RST, true);
            vTaskDelay(pdMS_TO_TICKS(120));
        }

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;  // -1: chip select is hard-wired
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 0;
        // 80 MHz, matching BSP_LCD_SPI_WRITE_HZ in the guachat BSP. The stock
        // firmware runs this panel at 80 MHz on the same PCB, so the wiring is
        // proven; at 40 MHz a full-screen refresh costs twice the bus time for
        // no benefit.
        io_config.pclk_hz = DISPLAY_SPI_CLOCK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        // Let the SPI DMA read the frame buffer straight out of PSRAM instead
        // of bouncing it through an internal-RAM staging buffer. Without this
        // the driver tries to allocate a staging buffer as large as the
        // transfer, so a full-frame push fails outright with
        // "Failed to allocate priv TX buffer" - PSRAM is not DMA-capable for
        // SPI on the S3 unless this flag asks for it explicitly.
        // esp_lcd_io_spi.h: "use PSRAM for DMA buffer directly, has speed
        // limit, but no temp buffer and save memory".
        io_config.flags.psram_dma_direct = 1;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;  // -1 -> software reset
        panel_config.rgb_ele_order = DISPLAY_RGB_ELEMENT_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);

        // ST7796 on guachat ships inverted with a BGR element order.
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_set_gap(panel, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);

        display_ = new SpiLcdDisplay(panel_io, panel,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    // XiaoZhi's UI is a status display with no widgets to press, so the touch
    // panel is wired to two behaviours: drags scroll the chat area (plain LVGL
    // pointer scrolling), and a tap on the screen toggles the chat. Volume is
    // left to the hardware keys only.
    void InitializeTouch() {
        touch_ = new GuachatTouch(&expander_, BOARD_TOUCH_ADDR);
        if (!touch_->Initialize()) {
            return;
        }

        touch_->SetTapHandler([this]() {
            // Called from the LVGL task, which already holds the display lock.
            // Hand the work to the application task so the state change does
            // not run under that lock.
            Application::GetInstance().Schedule([this]() { ToggleChat(); });
        });

        // The LVGL task is already running by now, so the input device has to
        // be created under the display lock like any other LVGL call.
        DisplayLockGuard lock(display_);
        touch_->RegisterIndev();
    }

    // The DRV2605L may need a 1-3 s LRA auto-calibration the first time, so it
    // gets its own task instead of delaying boot. The vibration is the point:
    // with no power LED, it is the only proof the power latch engaged.
    static void HapticTaskEntry(void* arg) {
        static_cast<GuachatBoard*>(arg)->HapticTask();
    }

    void HapticTask() {
        haptic_->Initialize();
        if (haptic_->IsReady()) {
            haptic_->PlayBootFeedback();
        }
        // Tick() returns the chip to standby once the effect finishes; without
        // it the driver stays enabled and keeps drawing current.
        while (true) {
            haptic_->Tick();
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    // The board's USB port is the S3's native USB-Serial-JTAG, which
    // re-enumerates on every reset, so roughly the first 200 ms of the boot
    // log never reaches the host - and the fuel gauge is initialised inside
    // that window. Re-print its register dump once the link is stable again.
    // Temporary bring-up aid: drop this task once the decoding is verified.
    static void BatteryProbeTaskEntry(void* arg) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        static_cast<GuachatBoard*>(arg)->battery_->LogDiagnostics();
        vTaskDelete(nullptr);
    }

    void ChangeVolume(int delta) {
        auto codec = GetAudioCodec();
        auto volume = codec->output_volume() + delta;
        if (volume > 100) {
            volume = 100;
        }
        if (volume < 0) {
            volume = 0;
        }
        codec->SetOutputVolume(volume);
        // Light tick so the keys feel like the original firmware.
        if (haptic_ != nullptr) {
            haptic_->Click();
        }
        GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
    }

    // The power key is a hardware latch the firmware cannot read or drive, so
    // BOOT stands in for it as a screen off/on key. Only the backlight is
    // blanked: LVGL keeps running underneath, so chat position, the idle face
    // and the battery level are all exactly as they were on wake.
    //
    // Runs on the application task: BOOT clicks and the idle timeout both go
    // through Application::Schedule(), so screen_off_ is written in one place.
    void SetScreenOff(bool off) {
        if (screen_off_.load() == off) {
            return;
        }
        auto* backlight = GetBacklight();
        if (off) {
            screen_brightness_ = backlight->brightness();
            backlight->SetBrightness(0);
        } else if (screen_brightness_ > 0) {
            backlight->SetBrightness(screen_brightness_);
        } else {
            // Never captured a level (pressed during the boot fade): fall back
            // to the saved setting, brightness 0 would leave the panel dark.
            backlight->RestoreBrightness();
        }
        screen_off_ = off;
    }

    void ToggleScreen() {
        SetScreenOff(!screen_off_.load());
    }

    // Screen power policy: leaving the idle state (wake word, screen tap,
    // server message) lights the panel again, and five idle minutes blank it.
    // Application exposes no public state listener, so polling the atomic
    // DeviceState is the cheapest way to follow it without touching core code.
    static void ScreenTaskEntry(void* arg) {
        static_cast<GuachatBoard*>(arg)->ScreenTask();
    }

    void ScreenTask() {
        // 250 ms: fast enough that the panel starts fading up almost as soon as
        // the wake word lands, cheap enough to be a plain atomic load.
        constexpr uint32_t kPollMs = 250;
        constexpr uint32_t kIdleMsBeforeOff = 5 * 60 * 1000;

        auto& app = Application::GetInstance();
        DeviceState prev_state = app.GetDeviceState();
        uint32_t idle_ms = 0;

        while (true) {
            vTaskDelay(pdMS_TO_TICKS(kPollMs));

            const DeviceState state = app.GetDeviceState();
            const bool off = screen_off_.load();

            // Waking XiaoZhi - by wake word, tap or a server push - always
            // brings the panel back, even if BOOT blanked it while busy.
            if (off && prev_state == kDeviceStateIdle && state != kDeviceStateIdle) {
                app.Schedule([this]() { SetScreenOff(false); });
            }

            if (state == kDeviceStateIdle && !off) {
                idle_ms += kPollMs;
                if (idle_ms >= kIdleMsBeforeOff) {
                    idle_ms = 0;
                    app.Schedule([this]() { SetScreenOff(true); });
                }
            } else {
                idle_ms = 0;
            }

            prev_state = state;
        }
    }

    void ToggleChat() {
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() == kDeviceStateStarting) {
            EnterWifiConfigMode();
            return;
        }
        app.ToggleChatState();
    }

    // Polls the two expander keys. They are raw reads with no interrupt
    // support, so debounce here and only act on a clean edge.
    static void VolumeTaskEntry(void* arg) {
        static_cast<GuachatBoard*>(arg)->VolumeTask();
    }

    void VolumeTask() {
        constexpr int kPollMs = 25;
        constexpr int kStableSamples = 3;  // ~75 ms of a stable level

        bool stable_up = false;
        bool stable_down = false;
        int up_run = 0;
        int down_run = 0;

        while (true) {
            uint16_t inputs = 0;
            if (expander_.ReadInputs(&inputs) == ESP_OK) {
                const bool raw_up = ((inputs >> BOARD_TCA9535_PIN_VOL_UP) & 1u) == 0;
                const bool raw_down = ((inputs >> BOARD_TCA9535_PIN_VOL_DOWN) & 1u) == 0;

                up_run = (raw_up == stable_up) ? 0 : up_run + 1;
                down_run = (raw_down == stable_down) ? 0 : down_run + 1;

                if (up_run >= kStableSamples) {
                    stable_up = raw_up;
                    up_run = 0;
                    if (stable_up) {
                        Application::GetInstance().Schedule([this]() { ChangeVolume(10); });
                    }
                }
                if (down_run >= kStableSamples) {
                    stable_down = raw_down;
                    down_run = 0;
                    if (stable_down) {
                        Application::GetInstance().Schedule([this]() { ChangeVolume(-10); });
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(kPollMs));
        }
    }

    void InitializeButtons() {
        // BOOT / lock key is a plain GPIO0 button, low active. A click is the
        // screen off/on key; the long press keeps its bring-up use of entering
        // Wi-Fi config straight from the starting state.
        boot_button_ = new Button(BOOT_BUTTON_GPIO, BOOT_BUTTON_ACTIVE_HIGH);
        boot_button_->OnClick([this]() {
            Application::GetInstance().Schedule([this]() { ToggleScreen(); });
        });
        boot_button_->OnLongPress([this]() {
            Application::GetInstance().Schedule([this]() {
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting) {
                    EnterWifiConfigMode();
                }
            });
        });

        // Volume keys live on the expander, so they get a polling task.
        if (expander_.IsPresent()) {
            xTaskCreate(VolumeTaskEntry, "guachat_vol", 3072, this, 3, nullptr);
        }

        // Screen off/on policy: follow the device state (see ScreenTask).
        xTaskCreate(ScreenTaskEntry, "guachat_screen", 3072, this, 2, nullptr);
    }

public:
    GuachatBoard() : display_(nullptr), battery_(nullptr), boot_button_(nullptr) {
        InitializeI2c();
        InitializeExpander();

        // The fuel gauge shares I2C0 with the expander and is optional.
        battery_ = new Max17055BatteryMonitor(i2c_bus_, BATTERY_MAX17055_ADDR);
        battery_->Initialize();

        haptic_ = new GuachatHaptic(i2c_bus_, BOARD_HAPTIC_ADDR);

        InitializeAmplifier();
        InitializeSpi();
        InitializeDisplay();
        InitializeTouch();
        InitializeButtons();
        GetBacklight()->RestoreBrightness();

#if GUACHAT_ACOUSTIC_PROBE
        // Runs here, synchronously, on purpose: AudioService has not started
        // yet, so this is the only window where nothing else is reading the
        // I2S RX channel. See guachat_acoustic_probe.h.
        GuachatRunAcousticProbe(GetAudioCodec());
#endif

        // Started last so the display and audio paths are already up by the
        // time the motor fires; the boot cue then lands roughly when the UI
        // first appears, not before the user can see anything.
        xTaskCreate(HapticTaskEntry, "guachat_haptic", 4096, this, 3, nullptr);

        if (battery_->IsPresent()) {
            xTaskCreate(BatteryProbeTaskEntry, "guachat_batt", 3072, this, 2, nullptr);
        }

#if GUACHAT_UI_SNAPSHOT
        // Delayed task, so it lands after Application::Initialize() has called
        // display->SetupUI(). Development aid only; see guachat_ui_snapshot.h.
        GuachatScheduleUiSnapshot(display_);
#endif

#if GUACHAT_TASK_REPORT
        GuachatScheduleTaskReport();
#endif
    }

    virtual AudioCodec* GetAudioCodec() override {
        // Bare I2S full duplex: MAX98357 on DOUT, I2S mic on DIN, no codec chip.
        static NoAudioCodecDuplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static GuachatBacklight backlight;
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (battery_ == nullptr || !battery_->IsPresent()) {
            return false;
        }
        int soc = battery_->GetBatteryLevel();
        if (soc < 0) {
            return false;
        }
        level = soc;

        // Current is signed, positive into the battery. Anything under the noise
        // threshold counts as not charging, which keeps the icon from flickering
        // between the bolt and a plain battery at the end of a charge cycle.
        const bool is_charging = battery_->IsCharging();
        charging = is_charging;
        discharging = !is_charging;
        return true;
    }
};

DECLARE_BOARD(GuachatBoard);
