#include "max17055_battery_monitor.h"

#include <esp_log.h>

#define TAG "Max17055"

// BYTE ORDER: the datasheet says MSB first, but this part delivers LSB first
// on the wire. Both reference implementations on the guachat side agree -
// bsp_power.cpp's read_reg16() takes the low byte first, and the vendored
// MAX17055 library combines as (buf[1] << 8) | buf[0] - and a raw register
// dump on real hardware settles it beyond doubt:
//
//   DesignCap (0x18) = A0 28  -> LSB first gives 0x28A0 = 10400 * 0.5 mAh
//                               = 5200 mAh, exactly the configured capacity
//   VCell    (0x09) = 84 DB  -> LSB first gives 0xDB84 = 56196 * 78.125 uV
//                               = 4390 mV, consistent with RepSOC = 100%
//
// Reading it MSB first instead yields 20500 mAh and 2657 mV, i.e. a pack that
// is simultaneously over-capacity and nearly dead. Do not "fix" this back.
Max17055BatteryMonitor::Max17055BatteryMonitor(i2c_master_bus_handle_t i2c_bus, uint8_t addr)
    : i2c_bus_(i2c_bus), i2c_device_(nullptr), device_address_(addr) {
}

Max17055BatteryMonitor::~Max17055BatteryMonitor() {
    if (i2c_device_ != nullptr) {
        i2c_master_bus_rm_device(i2c_device_);
        i2c_device_ = nullptr;
    }
}

bool Max17055BatteryMonitor::Initialize() {
    if (present_) {
        return true;
    }
    if (i2c_bus_ == nullptr) {
        return false;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = device_address_;
    dev_cfg.scl_speed_hz = 100000;

    esp_err_t err = i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &i2c_device_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(err));
        i2c_device_ = nullptr;
        return false;
    }

    uint16_t status = 0;
    if (ReadReg16(kRegStatus, &status) < 0) {
        ESP_LOGW(TAG, "no response at 0x%02X", device_address_);
        i2c_master_bus_rm_device(i2c_device_);
        i2c_device_ = nullptr;
        return false;
    }

    present_ = true;
    ESP_LOGI(TAG, "present at 0x%02X, status = 0x%04X", device_address_, status);
    return true;
}

// One-shot diagnostic so the register decoding can be checked against a real
// device without a debugger: a plausible pack voltage (~3.0-4.4 V) proves the
// byte order is right, and the sign of the current says which way the charger
// is pointing.
void Max17055BatteryMonitor::LogDiagnostics() {
    if (!present_) {
        ESP_LOGW(TAG, "diagnostics skipped: chip not present");
        return;
    }

    const int soc = GetBatteryLevel();
    const int mv = GetBatteryVoltageMv();
    int current_ma = 0;
    const bool have_current = GetCurrentMa(&current_ma);
    ESP_LOGI(TAG, "soc=%d%% vcell=%dmV current=%s%d mA%s",
             soc, mv,
             have_current ? "" : "?", current_ma,
             (have_current && current_ma > kChargeThresholdMa) ? " (charging)" : "");

    // Print the byte stream exactly as delivered, plus both interpretations,
    // so the correct one can be read straight off the log. Off by default:
    // the decoding is settled, and eight extra lines per boot is noise.
#if GUACHAT_BATTERY_DEBUG
    static constexpr uint8_t kDumpRegs[] = {0x00, 0x06, 0x09, 0x0A, 0x18, 0x1C, 0x1D};
    for (uint8_t reg : kDumpRegs) {
        uint8_t b[2] = {0, 0};
        if (i2c_master_transmit_receive(i2c_device_, &reg, 1, b, 2, 100) != ESP_OK) {
            ESP_LOGW(TAG, "  reg 0x%02X read failed", reg);
            continue;
        }
        const uint16_t be = static_cast<uint16_t>((b[0] << 8) | b[1]);
        const uint16_t le = static_cast<uint16_t>((b[1] << 8) | b[0]);
        ESP_LOGI(TAG, "  reg 0x%02X bytes=%02X %02X  be=0x%04X le=0x%04X  v_be=%umV v_le=%umV",
                 reg, b[0], b[1], be, le,
                 static_cast<unsigned>(be * 5u / 64u),
                 static_cast<unsigned>(le * 5u / 64u));
    }
#endif
}

int Max17055BatteryMonitor::ReadReg16(uint8_t reg, uint16_t* value) {
    if (i2c_device_ == nullptr || value == nullptr) {
        return -1;
    }
    uint8_t buf[2] = {0, 0};
    esp_err_t err = i2c_master_transmit_receive(i2c_device_, &reg, 1, buf, sizeof(buf), 100);
    if (err != ESP_OK) {
        return -1;
    }
    *value = (static_cast<uint16_t>(buf[1]) << 8) | static_cast<uint16_t>(buf[0]);
    return 0;
}

int Max17055BatteryMonitor::GetBatteryLevel() {
    if (!present_) {
        return -1;
    }

    uint16_t raw = 0;
    if (ReadReg16(kRegRepSoc, &raw) < 0) {
        return -1;
    }

    // RepSOC: whole percent in the high byte, 1/256 % in the low byte.
    int percent = static_cast<int>(raw >> 8);

    // 0xFFFF is the "no valid reading" pattern; 0 and 100 on an unconfigured
    // chip are equally untrustworthy, so surface "unknown" instead.
    if (raw == 0xFFFF || percent <= 0 || percent > 100) {
        return -1;
    }
    return percent;
}

int Max17055BatteryMonitor::GetBatteryVoltageMv() {
    if (!present_) {
        return -1;
    }

    uint16_t raw = 0;
    if (ReadReg16(kRegVCell, &raw) < 0) {
        return -1;
    }

    // VCell: 78.125 uV per LSB, i.e. exactly 5/64 mV. Written as a ratio
    // rather than raw * 78125 / 1000000 because the latter overflows 32-bit
    // arithmetic above ~54.9 k LSB (~4.29 V) - right in the middle of this
    // pack's 3.0-4.4 V range.
    int mv = static_cast<int>((static_cast<uint32_t>(raw) * 5u) / 64u);
    if (mv <= 0) {
        return -1;
    }
    return mv;
}

bool Max17055BatteryMonitor::GetCurrentMa(int* current_ma) {
    if (!present_ || current_ma == nullptr) {
        return false;
    }

    uint16_t raw = 0;
    if (ReadReg16(kRegCurrent, &raw) < 0) {
        return false;
    }

    // Current is the one signed register of the three: two's complement,
    // positive while charging.
    const int32_t signed_raw = static_cast<int16_t>(raw);
    *current_ma = static_cast<int>(signed_raw * kCurrentLsbNumerator / kCurrentLsbDenominator);
    return true;
}

bool Max17055BatteryMonitor::IsCharging() {
    int current_ma = 0;
    if (!GetCurrentMa(&current_ma)) {
        return false;
    }
    return current_ma > kChargeThresholdMa;
}
