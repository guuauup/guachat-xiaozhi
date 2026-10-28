#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// ============================================================================
// GUACHAT 2G4 board definition for XiaoZhi.
//
// Every hardware constant below is copied from the guachat repository's
// single source of truth: src/bsp/bsp_pins.h.
// https://gitee.com/guaup/guachat
//
// Board: ESP32-S3-WROOM-1U N16R8 (16 MB flash, 8 MB OPI PSRAM).
// Differences from the FoloToy AI Passport this port is based on:
//   * S3 instead of C3, and PSRAM is available.
//   * Audio is a bare I2S full-duplex link to a MAX98357 amplifier plus an
//     I2S microphone. There is no codec chip, so NoAudioCodec is used.
//   * Display is a 320x480 ST7796 (BGR, inverted), not a 240x320 ST7789.
//   * The panel CS line is hard-wired, and panel/touch reset are driven by a
//     TCA9535 I2C expander, not by GPIOs.
//   * Volume keys live on the TCA9535, not on an ADC resistor ladder.
// ============================================================================

// XiaoZhi's protocol runs at 16 kHz. The guachat BSP defaults to 8 kHz only
// because its own self-test UI does not need more; the I2S hardware supports
// 16 kHz fine, and the S3 has PSRAM so the resampler buffer is not a concern.
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000

// I2S0 full-duplex. MAX98357 needs no MCLK and the I2S mic generates its own,
// so MCLK stays unconnected (matches bsp_pins.h, which has no MCLK pin).
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_NC
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_39   // BSP_I2S_PIN_LRC
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_38   // BSP_I2S_PIN_BCLK
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_40   // mic -> ESP32 (BSP_I2S_PIN_DIN)
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_41   // ESP32 -> amp (BSP_I2S_PIN_DOUT)

// MAX98357 SD_MODE: high = amplifier enabled. Driven by the board code
// because NoAudioCodec does not own an amplifier pin.
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_42
// No codec chip: these only exist so the shared headers compile.
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_2
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_1

// ============================================================================
// I2C0: sensors / fuel gauge / IO expander, 100 kHz.
// ============================================================================
#define BOARD_I2C0_SDA_PIN GPIO_NUM_2
#define BOARD_I2C0_SCL_PIN GPIO_NUM_1
#define BOARD_I2C0_FREQ_HZ 100000

// TCA9535 IO expander (I2C0 0x20). Pin indices are encoded as port.bit,
// so P10 (port 1 bit 0) == 8, matching bsp_pins.h.
#define BOARD_TCA9535_ADDR        0x20
#define BOARD_TCA9535_PIN_VOL_DOWN 0    // P00, low active
#define BOARD_TCA9535_PIN_VOL_UP   1    // P01, low active
#define BOARD_TCA9535_PIN_GPS_LDO  2    // P02, high = on
#define BOARD_TCA9535_PIN_POWER_SW 3    // P03, high = run (PULLING THIS LOW POWERS OFF)
#define BOARD_TCA9535_PIN_VERSION  8    // P10, hardware version strap, input only
#define BOARD_TCA9535_PIN_SHT30_RST 12  // P14
#define BOARD_TCA9535_PIN_TOUCH_RST 13  // P15
#define BOARD_TCA9535_PIN_DISPLAY_RST 14 // P16
#define BOARD_TCA9535_PIN_TF_CD    15   // P17, low = card inserted

// Power-on output state: every line high. This keeps the power latch (P03)
// asserted and releases the SHT30 / touch / display reset lines.
#define BOARD_TCA9535_POWER_ON_MASK 0xFFFF
// Only the hardware version strap stays an input; everything else drives out.
#define BOARD_TCA9535_INPUT_MASK (1u << BOARD_TCA9535_PIN_VERSION)

// ============================================================================
// Buttons.
//   BOOT / lock  GPIO0, low active, internal pull-up.
//   Volume up    TCA9535 P01, low active.
//   Volume down  TCA9535 P00, low active.
// The power key is a hardware-only latch and cannot be read by firmware.
// ============================================================================
#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define BOOT_BUTTON_ACTIVE_HIGH false

// ============================================================================
// Display: ST7796, 320x480 portrait, SPI2, BGR element order, inverted panel.
// CS is hard-wired (no GPIO); RST is driven through the TCA9535.
// ============================================================================
#define DISPLAY_SPI_SCK_PIN  GPIO_NUM_14
#define DISPLAY_SPI_MOSI_PIN GPIO_NUM_13
#define DISPLAY_DC_PIN       GPIO_NUM_21
#define DISPLAY_SPI_CS_PIN   GPIO_NUM_NC   // hard-wired chip select
#define DISPLAY_RST_PIN      GPIO_NUM_NC   // reset via TCA9535 P16

// Matches BSP_LCD_SPI_WRITE_HZ in the guachat BSP. This PCB drives the same
// panel at 80 MHz in the stock firmware, so 40 MHz was leaving bus time on the
// table for nothing.
#define DISPLAY_SPI_CLOCK_HZ (80 * 1000 * 1000)

#define DISPLAY_WIDTH  320
#define DISPLAY_HEIGHT 480

// Horizontal mirror only. The panel scans left-to-right in the opposite
// direction to what LVGL assumes, so a plain unmirrored MADCTL renders the UI
// left-right flipped with the vertical axis already correct. Enabling mirror_y
// as well would flip it upside down again.
//
// These three flags reach the hardware twice: once from esp_lcd_panel_* calls
// in InitializeDisplay(), and again from esp_lvgl_port when the LVGL display is
// registered. The second write wins (MADCTL bits are set absolutely, not
// toggled), so this macro - not the board code - is the knob that matters.
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY  false   // portrait

#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0

// ST7796 on this board is BGR + inverted (see lib/TFT_eSPI/my_user_setup.h).
#define DISPLAY_RGB_ELEMENT_ORDER LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_INVERT_COLOR true

#define DISPLAY_BACKLIGHT_PIN           GPIO_NUM_46
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// Backlight is not a bare LED string. It is an op-amp + MOS active filter
// driving a DC constant-current sink, so duty cycle maps linearly to current
// and the panel's 160 mA hardware ceiling is reached at 8.3% duty. The guachat
// BSP expresses that as 170/2047 on its 11-bit LEDC timer; XiaoZhi's
// PwmBacklight runs a 10-bit timer, where the same ratio is 85/1023.
//
// DISPLAY_BACKLIGHT_MAX_PERCENT caps the requested 0-100 brightness scale
// before it reaches the LEDC duty calculation, so 100% here means "8% of the
// timer range" = 81/1023 = 7.9% duty, i.e. ~152 mA. That is deliberately just
// under the 160 mA ceiling rather than exactly on it: overshooting a
// constant-current limit by a few percent is what kills backlight strings, and
// the 5% brightness difference is not visible.
#define DISPLAY_BACKLIGHT_MAX_PERCENT 8

// ============================================================================
// Haptics: DRV2605L LRA on I2C0 0x5A.
// This board has no power LED, so the boot vibration is the only confirmation
// that the power latch engaged. Do not remove it.
// ============================================================================
#define BOARD_HAPTIC_ADDR 0x5A

// ============================================================================
// Battery: MAX17055 on I2C0 0x36, single 18650 cell (4.4 V / 5200 mAh).
// ============================================================================
#define BATTERY_MAX17055_ADDR 0x36
#define BATTERY_DESIGN_CAPACITY_MAH 5200
#define BATTERY_FULL_MV  4400
#define BATTERY_EMPTY_MV 3000

// ============================================================================
// Touch: FT6336U on its own bus, I2C1 at 400 kHz, address 0x38.
// Reset is TCA9535 P15; the interrupt line (GPIO45) is left unused because the
// LVGL pointer device polls instead, which keeps the ISR out of the picture.
// ============================================================================
#define BOARD_I2C1_SDA_PIN GPIO_NUM_47
#define BOARD_I2C1_SCL_PIN GPIO_NUM_48
#define BOARD_I2C1_FREQ_HZ 400000
#define BOARD_TOUCH_ADDR   0x38
#define BOARD_TOUCH_INT_PIN GPIO_NUM_45

// ============================================================================
// One-shot acoustic echo-path diagnostic.
//
// Plays two short chirps at boot and prints the echo delay, attenuation and
// linearity, which is what decides whether device-side AEC is worth building.
// It makes noise and adds ~5 s to boot, so it is off in normal builds.
//
//   0 = disabled (default, silent boot)
//   1 = run once, then remembered in NVS; a crash-reboot cannot replay it
//   2 = run on every boot, for repeat measurements only
//
// Measured 2026-10-28 with this probe: echo delay 151.7 ms, echo path gain
// ~+44 dB, amplifier linear to within 0.4 dB, and the microphone saturates
// whenever the speaker plays at a usable volume. That last number is why
// device-side AEC is not implemented - see README.
// ============================================================================
#define GUACHAT_ACOUSTIC_PROBE 0

// ============================================================================
// One-shot UI snapshot: seeds a few chat messages and dumps a JPEG of the
// screen over the serial port, so the layout can be checked without the device
// being connected to a server. Development only - it pollutes the chat area and
// blocks the boot log for a couple of seconds.
// ============================================================================
#define GUACHAT_UI_SNAPSHOT 0

// ============================================================================
// One-shot FreeRTOS task report: after 15 s of settling, samples every task's
// priority, CPU share and stack headroom twice (5 s apart) and prints the
// difference. Development only.
// ============================================================================
#define GUACHAT_TASK_REPORT 0

#endif // _BOARD_CONFIG_H_
