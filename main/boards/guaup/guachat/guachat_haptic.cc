#include "guachat_haptic.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>

#define TAG "GuachatHaptic"

namespace {

// Register map, DRV2605L datasheet section 7.
constexpr uint8_t kRegStatus = 0x00;
constexpr uint8_t kRegMode = 0x01;
constexpr uint8_t kRegLibrary = 0x03;
constexpr uint8_t kRegWaveSeq1 = 0x04;
constexpr uint8_t kRegGo = 0x0C;
constexpr uint8_t kRegOverdrive = 0x0D;
constexpr uint8_t kRegSustainPos = 0x0E;
constexpr uint8_t kRegSustainNeg = 0x0F;
constexpr uint8_t kRegBreak = 0x10;
constexpr uint8_t kRegRatedV = 0x16;
constexpr uint8_t kRegClampV = 0x17;
constexpr uint8_t kRegAutoCalComp = 0x18;
constexpr uint8_t kRegAutoCalEmp = 0x19;
constexpr uint8_t kRegFeedback = 0x1A;
constexpr uint8_t kRegControl1 = 0x1B;
constexpr uint8_t kRegControl2 = 0x1C;
constexpr uint8_t kRegControl3 = 0x1D;
constexpr uint8_t kRegControl4 = 0x1E;
constexpr uint8_t kRegLraReson = 0x22;

constexpr uint8_t kModeIntTrig = 0x00;
constexpr uint8_t kModeAutoCal = 0x07;
constexpr uint8_t kModeStandby = 0x40;

// Tuning values measured on this LRA by the guachat firmware. Do not "improve"
// them: with CONTROL1 bit 4 cleared the LRA only whines and never oscillates.
constexpr uint8_t kFeedbackLra = 0xA8;   // N_ERM_LRA + brake factor 2 + loop gain 2
constexpr uint8_t kControl1 = 0xF9;      // brake gain 7 | bit4 | blanking 2 | idiss 1
constexpr uint8_t kControl2 = 0x60;      // 300 us BEMF sample + brake stabilizer
constexpr uint8_t kRatedRv = 0x74;       // ~2.51 V, used while calibrating
constexpr uint8_t kClampRv = 0x97;       // ~3.26 V
constexpr uint8_t kIdleRv = 0x0A;        // low resting voltage
constexpr uint8_t kAlertRv = 0x44;       // ~1.47 V, original default strength
constexpr uint8_t kLraPeriod = 0x3C;     // resonance ~169 Hz

constexpr int64_t kCalTimeoutUs = 3'000'000;
constexpr int64_t kPlayTimeoutUs = 3'000'000;

// Same namespace and keys as the guachat firmware, so an already-calibrated
// device is not forced to recalibrate.
constexpr const char* kNvsNamespace = "drv2605";

}  // namespace

GuachatHaptic::GuachatHaptic(i2c_master_bus_handle_t i2c_bus, uint8_t addr)
    : i2c_bus_(i2c_bus), addr_(addr) {
}

GuachatHaptic::~GuachatHaptic() {
    if (device_ != nullptr) {
        i2c_master_bus_rm_device(device_);
        device_ = nullptr;
    }
}

bool GuachatHaptic::ReadReg(uint8_t reg, uint8_t* value) {
    if (device_ == nullptr || value == nullptr) {
        return false;
    }
    return i2c_master_transmit_receive(device_, &reg, 1, value, 1, 100) == ESP_OK;
}

bool GuachatHaptic::WriteReg(uint8_t reg, uint8_t value) {
    if (device_ == nullptr) {
        return false;
    }
    const uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(device_, buf, sizeof(buf), 100) == ESP_OK;
}

bool GuachatHaptic::Initialize() {
    if (ready_) {
        return true;
    }
    if (i2c_bus_ == nullptr) {
        return false;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = addr_;
    dev_cfg.scl_speed_hz = 100000;

    if (i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &device_) != ESP_OK) {
        ESP_LOGW(TAG, "add device failed");
        device_ = nullptr;
        return false;
    }

    uint8_t status = 0;
    if (!ReadReg(kRegStatus, &status)) {
        ESP_LOGW(TAG, "no response at 0x%02X; boot feedback disabled", addr_);
        i2c_master_bus_rm_device(device_);
        device_ = nullptr;
        return false;
    }

    if (!ExitStandby() || !Configure()) {
        ESP_LOGW(TAG, "register configuration failed");
        EnterIdle();
        return false;
    }

    if (LoadCalibration()) {
        ESP_LOGI(TAG, "calibration restored from NVS");
    } else {
        ESP_LOGI(TAG, "no cached calibration; running LRA auto-calibration");
        Calibrate();
    }

    if (!ExitStandby()) {
        ESP_LOGW(TAG, "failed to leave standby");
        return false;
    }

    // The measured resonance is ~169 Hz. Auto-calibration can drift away from
    // it, so the fixed period always wins.
    WriteReg(kRegLraReson, kLraPeriod);
    WriteReg(kRegOverdrive, 0);
    WriteReg(kRegSustainPos, 0);
    WriteReg(kRegSustainNeg, 0);
    WriteReg(kRegBreak, 0);
    WriteReg(kRegRatedV, kIdleRv);
    EnterIdle();

    ready_ = true;
    ESP_LOGI(TAG, "ready at 0x%02X (LRA, %.0f Hz), standby",
             addr_, 1000000.0f / (kLraPeriod * 98.46f));
    return true;
}

bool GuachatHaptic::Configure() {
    bool ok = WriteReg(kRegFeedback, kFeedbackLra);

    // LRA must run closed-loop: clear ERM_OPEN_LOOP in CONTROL3.
    uint8_t ctrl3 = 0;
    if (ReadReg(kRegControl3, &ctrl3)) {
        ok = WriteReg(kRegControl3, static_cast<uint8_t>(ctrl3 & ~0x20)) && ok;
    } else {
        ok = false;
    }

    ok = WriteReg(kRegControl1, kControl1) && ok;
    ok = WriteReg(kRegControl2, kControl2) && ok;

    // LRA_DRIVE_TIME = 0 (1x).
    uint8_t ctrl4 = 0;
    if (ReadReg(kRegControl4, &ctrl4)) {
        ok = WriteReg(kRegControl4, static_cast<uint8_t>(ctrl4 & ~(0x03 << 2))) && ok;
    } else {
        ok = false;
    }

    ok = WriteReg(kRegRatedV, kRatedRv) && ok;
    ok = WriteReg(kRegClampV, kClampRv) && ok;
    ok = WriteReg(kRegLibrary, 6) && ok;  // Library 6 = LRA
    return ok;
}

bool GuachatHaptic::LoadCalibration() {
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    uint8_t done = 0;
    uint8_t comp = 0;
    uint8_t emp = 0;
    uint8_t feedback = 0;
    const bool read_ok = nvs_get_u8(handle, "done", &done) == ESP_OK &&
                         nvs_get_u8(handle, "comp", &comp) == ESP_OK &&
                         nvs_get_u8(handle, "emp", &emp) == ESP_OK &&
                         nvs_get_u8(handle, "fb", &feedback) == ESP_OK;
    nvs_close(handle);

    // feedback bit 7 (N_ERM_LRA) must be set, otherwise the cached blob was not
    // written for an LRA and is not usable.
    if (!read_ok || done == 0 || (feedback & 0x80) == 0) {
        return false;
    }

    WriteReg(kRegAutoCalComp, comp);
    WriteReg(kRegAutoCalEmp, emp);
    WriteReg(kRegFeedback, feedback);
    return true;
}

bool GuachatHaptic::Calibrate() {
    uint8_t status = 0;
    uint8_t comp = 0;
    uint8_t emp = 0;
    uint8_t period = 0;
    uint8_t feedback = 0;
    if (!RunCalibration(&status, &comp, &emp, &period, &feedback)) {
        return false;
    }
    SaveCalibration(comp, emp, period, feedback);
    return (status & 0x08) != 0;
}

bool GuachatHaptic::RunCalibration(uint8_t* status, uint8_t* comp, uint8_t* emp,
                                   uint8_t* period, uint8_t* feedback) {
    // Overdrive and braking interfere with the back-EMF measurement.
    WriteReg(kRegOverdrive, 0);
    WriteReg(kRegSustainPos, 0);
    WriteReg(kRegSustainNeg, 0);
    WriteReg(kRegBreak, 0);

    if (!WriteReg(kRegMode, kModeAutoCal) || !WriteReg(kRegGo, 1)) {
        ESP_LOGW(TAG, "failed to start auto-calibration");
        return false;
    }

    const int64_t start = esp_timer_get_time();
    uint8_t go = 1;
    while (esp_timer_get_time() - start < kCalTimeoutUs) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (!ReadReg(kRegGo, &go) || go == 0) {
            break;
        }
    }
    if (go != 0) {
        ESP_LOGW(TAG, "auto-calibration timed out");
        return false;
    }

    ReadReg(kRegStatus, status);
    ReadReg(kRegAutoCalComp, comp);
    ReadReg(kRegAutoCalEmp, emp);
    ReadReg(kRegLraReson, period);
    ReadReg(kRegFeedback, feedback);

    ESP_LOGI(TAG, "auto-calibration %s | status=0x%02X comp=0x%02X emp=0x%02X period=0x%02X",
             (*status & 0x08) ? "OK" : "WARN", *status, *comp, *emp, *period);
    return true;
}

bool GuachatHaptic::SaveCalibration(uint8_t comp, uint8_t emp, uint8_t period,
                                    uint8_t feedback) {
    if (period == 0 || period == 0xFF || feedback == 0) {
        return false;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    const esp_err_t err = nvs_set_u8(handle, "done", 1);
    if (err == ESP_OK) {
        nvs_set_u8(handle, "comp", comp);
        nvs_set_u8(handle, "emp", emp);
        nvs_set_u8(handle, "fb", feedback);
        nvs_commit(handle);
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "calibration saved to NVS");
    return err == ESP_OK;
}

bool GuachatHaptic::ExitStandby() {
    if (!standby_) {
        return true;
    }
    if (!WriteReg(kRegMode, kModeIntTrig)) {
        return false;
    }
    standby_ = false;
    vTaskDelay(pdMS_TO_TICKS(1));  // let the internal oscillator settle
    return true;
}

void GuachatHaptic::EnterIdle() {
    WriteReg(kRegRatedV, kIdleRv);
    if (WriteReg(kRegMode, kModeStandby)) {
        standby_ = true;
    }
    playing_ = false;
    play_deadline_us_ = 0;
}

bool GuachatHaptic::Play(uint8_t effect, uint8_t rated_rv) {
    if (!ready_ || effect == 0) {
        return false;
    }

    bool ok = WriteReg(kRegRatedV, rated_rv) && ExitStandby();
    if (ok) {
        ok = WriteReg(kRegWaveSeq1, effect) &&
             WriteReg(kRegWaveSeq1 + 1, 0) &&
             WriteReg(kRegMode, kModeIntTrig) &&
             WriteReg(kRegGo, 1);
    }
    if (!ok) {
        EnterIdle();
        return false;
    }

    playing_ = true;
    play_deadline_us_ = esp_timer_get_time() + kPlayTimeoutUs;
    return true;
}

bool GuachatHaptic::PlayBootFeedback() {
    if (!ready_) {
        return false;
    }
    // kAlertRv is the original firmware's default strength; a boot cue at the
    // user's configured alert level would need the settings UI this port does
    // not have.
    const bool ok = Play(kEffectStrongClick100, kAlertRv);
    if (ok) {
        ESP_LOGI(TAG, "boot feedback");
    } else {
        ESP_LOGW(TAG, "boot feedback failed");
    }
    return ok;
}

bool GuachatHaptic::Click() {
    if (!ready_) {
        return false;
    }
    return Play(kEffectKeyClick, kIdleRv);
}

void GuachatHaptic::Tick() {
    if (!ready_ || !playing_) {
        return;
    }
    if (esp_timer_get_time() >= play_deadline_us_) {
        ESP_LOGW(TAG, "playback timeout, forcing standby");
        EnterIdle();
        return;
    }

    uint8_t go = 0;
    if (!ReadReg(kRegGo, &go)) {
        return;
    }
    if (go == 0) {
        EnterIdle();
    }
}

void GuachatHaptic::Stop() {
    if (ready_) {
        EnterIdle();
    }
}
