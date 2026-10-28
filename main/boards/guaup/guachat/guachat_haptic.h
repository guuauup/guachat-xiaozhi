#ifndef GUACHAT_HAPTIC_H
#define GUACHAT_HAPTIC_H

#include <cstdint>

#include <driver/i2c_master.h>

// DRV2605L LRA driver for the guachat board (I2C0 address 0x5A).
//
// Ported from guachat:src/I2C0/DRV2605L.cpp. This board has no power LED, so a
// boot vibration is the only feedback that the power latch actually engaged and
// the device is alive. That is the one thing this driver exists for; the extra
// effect helpers are there because they cost nothing once the chip is up.
//
// Everything here is optional: if the chip does not answer, IsReady() stays
// false and every call becomes a no-op. A missing haptic must never stop the
// board from booting.
//
// The LRA auto-calibration result is cached in NVS namespace "drv2605" using
// the same keys the guachat firmware wrote ("done", "comp", "emp", "fb"), so a
// device that was already calibrated by the original firmware reuses that
// result instead of buzzing through a 1-3 second calibration on every flash.
class GuachatHaptic {
public:
    // ROM Library 6 (LRA) effect ids, see DRV2605L datasheet section 11.2.
    static constexpr uint8_t kEffectStrongClick100 = 1;
    static constexpr uint8_t kEffectKeyClick = 26;  // Sharp Tick 3, 60%

    explicit GuachatHaptic(i2c_master_bus_handle_t i2c_bus, uint8_t addr = 0x5A);
    ~GuachatHaptic();

    // Probes the chip, configures it, loads or runs LRA calibration and leaves
    // it in standby. Blocking for up to ~3 s only when calibration is needed.
    bool Initialize();

    bool IsReady() const { return ready_; }

    // Strong click at the original firmware's default alert level. Used as the
    // power-on confirmation.
    bool PlayBootFeedback();

    // Light tick for the volume keys, so they feel like the original device.
    bool Click();

    // Call periodically (100-500 ms). Returns the chip to standby once playback
    // finishes; without this the driver stays enabled and drains current.
    void Tick();

    // Stops playback and enters standby immediately.
    void Stop();

private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t device_ = nullptr;
    uint8_t addr_;
    bool ready_ = false;
    bool standby_ = true;
    bool playing_ = false;
    int64_t play_deadline_us_ = 0;

    bool ReadReg(uint8_t reg, uint8_t* value);
    bool WriteReg(uint8_t reg, uint8_t value);

    bool Configure();
    bool LoadCalibration();
    bool Calibrate();
    bool RunCalibration(uint8_t* status, uint8_t* comp, uint8_t* emp,
                        uint8_t* period, uint8_t* feedback);
    bool SaveCalibration(uint8_t comp, uint8_t emp, uint8_t period, uint8_t feedback);

    bool ExitStandby();
    void EnterIdle();
    bool Play(uint8_t effect, uint8_t rated_rv);
};

#endif  // GUACHAT_HAPTIC_H
