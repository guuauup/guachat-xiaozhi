#ifndef _GUACHAT_TCA9535_H_
#define _GUACHAT_TCA9535_H_

#include <cstdint>

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>

// Minimal TCA9535 16-bit IO expander driver for the guachat board.
//
// Only the operations the board needs are implemented: whole-port output
// writes, direction configuration, whole-port reads, and single-pin
// read-modify-write. Register layout is the standard TCA9535/TCA9555 one.
//
// IMPORTANT: on this board P03 is the power latch (high = run). Driving it
// low powers the device off. Always write the output register before
// switching the pin direction, so the pin never glitches low on power-up.
// See guachat:src/I2C0/TCA9535.cpp for the original sequencing notes.
class GuachatTca9535 {
public:
    static constexpr uint8_t kRegInputPort0 = 0x00;
    static constexpr uint8_t kRegInputPort1 = 0x01;
    static constexpr uint8_t kRegOutputPort0 = 0x02;
    static constexpr uint8_t kRegOutputPort1 = 0x03;
    static constexpr uint8_t kRegConfigPort0 = 0x06;
    static constexpr uint8_t kRegConfigPort1 = 0x07;

    GuachatTca9535() = default;
    ~GuachatTca9535();

    GuachatTca9535(const GuachatTca9535&) = delete;
    GuachatTca9535& operator=(const GuachatTca9535&) = delete;

    // Adds the expander to an existing I2C master bus. Does not touch pins.
    esp_err_t Initialize(i2c_master_bus_handle_t bus, uint8_t address);

    bool IsPresent() const { return device_ != nullptr; }

    // Writes the 16-bit output register. Safe to call while pins are inputs.
    esp_err_t WriteOutputs(uint16_t value);

    // Writes the 16-bit direction register. A set bit means INPUT.
    esp_err_t SetDirections(uint16_t input_mask);

    // Reads both input ports as one 16-bit value.
    esp_err_t ReadInputs(uint16_t* out);

    // Read-modify-write of a single output pin.
    esp_err_t SetPin(uint8_t pin, bool level);

    // Drives a reset line low, waits, then releases it high.
    esp_err_t PulseLow(uint8_t pin, uint32_t low_ms);

private:
    i2c_master_dev_handle_t device_ = nullptr;
    uint16_t output_cache_ = 0;
};

#endif // _GUACHAT_TCA9535_H_
