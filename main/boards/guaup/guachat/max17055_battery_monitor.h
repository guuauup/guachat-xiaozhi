#ifndef MAX17055_BATTERY_MONITOR_H
#define MAX17055_BATTERY_MONITOR_H

#include <cstddef>
#include <cstdint>

#include <driver/i2c_master.h>

// MAX17055 fuel-gauge helper for the guachat board (I2C0 address 0x36).
//
// Only the registers that are meaningful without a full ModelGauge learning
// cycle are read: RepSOC (0x06), VCell (0x09) and Current (0x0A). Capacity
// model configuration is deliberately NOT performed here. The guachat BSP
// already programs DesignCap and VEmpty, and those settings live in the chip,
// so this port must not fight it. On a chip that has never been configured,
// SOC can read 0 or 100%; callers get -1 rather than a wrong number.
//
// The chip is optional: a missing device must not crash the board, so reads
// return -1 / false instead of asserting.
//
// Registers arrive LSB first on this part, not MSB first as the datasheet
// implies. See ReadReg16() for the evidence before changing that.
class Max17055BatteryMonitor {
public:
    static constexpr uint8_t kRegStatus = 0x00;
    static constexpr uint8_t kRegRepSoc = 0x06;
    static constexpr uint8_t kRegVCell = 0x09;
    static constexpr uint8_t kRegCurrent = 0x0A;

    // The board uses a 0.01 ohm sense resistor, so one Current LSB (1.5625 uA)
    // works out to 0.15625 mA. Full scale is therefore about +/-5.1 A, which is
    // the right order for a 18650 handheld.
    static constexpr int32_t kCurrentLsbNumerator = 5;     // 0.15625 mA == 5/32
    static constexpr int32_t kCurrentLsbDenominator = 32;

    // Below this magnitude the reading is noise, not a real charge current.
    // Matches the guachat BSP's threshold in bsp_power.cpp.
    static constexpr int kChargeThresholdMa = 50;

    explicit Max17055BatteryMonitor(i2c_master_bus_handle_t i2c_bus, uint8_t addr = 0x36);
    ~Max17055BatteryMonitor();

    // Returns true when the chip answers on the bus. Safe to call at init.
    bool Initialize();

    // State of charge in percent (0-100), or -1 when unavailable.
    int GetBatteryLevel();

    // Battery voltage in mV, or -1 when unavailable.
    int GetBatteryVoltageMv();

    // Instant battery current in mA: positive charging, negative discharging.
    // Returns false when the chip cannot be read.
    bool GetCurrentMa(int* current_ma);

    // True when current is flowing into the battery. A reading under the noise
    // threshold counts as not charging.
    bool IsCharging();

    bool IsPresent() const { return present_; }

    // Dumps every register this driver depends on, as raw bytes plus both
    // plausible 16-bit interpretations. Byte order is the single most likely
    // thing to be wrong on a first port and it is invisible in the converted
    // values, so this exists to be read off a serial log. Call it a few
    // seconds after boot: the board's native USB-Serial-JTAG re-enumerates on
    // reset and the first ~200 ms of the log never reaches the host.
    void LogDiagnostics();

private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t i2c_device_;
    uint8_t device_address_;
    bool present_ = false;

    int ReadReg16(uint8_t reg, uint16_t* value);
};

#endif  // MAX17055_BATTERY_MONITOR_H
