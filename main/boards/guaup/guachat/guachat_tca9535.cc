#include "guachat_tca9535.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "GuachatTca9535"

GuachatTca9535::~GuachatTca9535() {
    if (device_ != nullptr) {
        i2c_master_bus_rm_device(device_);
        device_ = nullptr;
    }
}

esp_err_t GuachatTca9535::Initialize(i2c_master_bus_handle_t bus, uint8_t address) {
    if (device_ != nullptr) {
        return ESP_OK;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = address;
    dev_cfg.scl_speed_hz = 100000;

    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &device_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(err));
        device_ = nullptr;
        return err;
    }

    // Probe by reading the input register. A missing expander must not abort
    // boot: the board still runs, just without the volume keys and with a
    // software display reset.
    uint8_t probe = 0;
    err = i2c_master_transmit_receive(device_, &kRegInputPort0, 1, &probe, 1, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no response at 0x%02X: %s", address, esp_err_to_name(err));
        i2c_master_bus_rm_device(device_);
        device_ = nullptr;
        return err;
    }

    ESP_LOGI(TAG, "present at 0x%02X, input port0 = 0x%02X", address, probe);
    return ESP_OK;
}

esp_err_t GuachatTca9535::WriteOutputs(uint16_t value) {
    if (device_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[3] = {
        kRegOutputPort0,
        static_cast<uint8_t>(value & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
    };
    esp_err_t err = i2c_master_transmit(device_, buf, sizeof(buf), 100);
    if (err == ESP_OK) {
        output_cache_ = value;
    }
    return err;
}

esp_err_t GuachatTca9535::SetDirections(uint16_t input_mask) {
    if (device_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[3] = {
        kRegConfigPort0,
        static_cast<uint8_t>(input_mask & 0xFF),
        static_cast<uint8_t>((input_mask >> 8) & 0xFF),
    };
    return i2c_master_transmit(device_, buf, sizeof(buf), 100);
}

esp_err_t GuachatTca9535::ReadInputs(uint16_t* out) {
    if (device_ == nullptr || out == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[2] = {0, 0};
    esp_err_t err = i2c_master_transmit_receive(device_, &kRegInputPort0, 1, buf, 2, 100);
    if (err == ESP_OK) {
        *out = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
    }
    return err;
}

esp_err_t GuachatTca9535::SetPin(uint8_t pin, bool level) {
    if (device_ == nullptr || pin > 15) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t next = output_cache_;
    if (level) {
        next |= static_cast<uint16_t>(1u << pin);
    } else {
        next &= static_cast<uint16_t>(~(1u << pin));
    }
    return WriteOutputs(next);
}

esp_err_t GuachatTca9535::PulseLow(uint8_t pin, uint32_t low_ms) {
    esp_err_t err = SetPin(pin, false);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(low_ms));
    return SetPin(pin, true);
}
