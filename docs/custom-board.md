# Custom Board Guide

This repository ships exactly one board implementation, the GUACHAT 2G4
(`main/boards/guaup/guachat`). This guide describes how to add a second one.
Core code depends on the `Board` interface in
[`main/boards/common/board.h`](../main/boards/common/board.h), never on a
concrete board class or a board's `config.h`, so a new board is a new directory
plus one entry in each link of the selection chain.

> **CI note**: the prepare job in `.github/workflows/build.yml` asserts that
> `--list-boards` returns exactly one board, `guaup/guachat`. Adding a board
> means relaxing that assertion in the same change, otherwise CI fails before
> anything builds.

## Important

> **Warning**: to support hardware whose IO differs from an existing board,
> never edit the existing board's pins. Add a uniquely named board or release
> variant. Board identity affects OTA compatibility: an existing device may
> receive the firmware built for its board type, and overwriting that board's
> configuration can push a pin map the hardware cannot survive onto devices
> already in the field.

If the board id is not registered with the server the device checks in with, keep
`CONFIG_DISABLE_OTA_UPGRADE` on: the check-in still delivers the activation code
and the server address, but a firmware offered for that id is logged and ignored
instead of being flashed onto hardware it was not built for.

Board selection is a coupled chain. A new board has to appear in every link or
the build will fail or silently select the wrong directory:

```
config.json  ->  scripts/build.py  ->  main/Kconfig.projbuild
             ->  main/CMakeLists.txt  ->  board source + config.h
```

## Directory Layout

```
main/boards/<manufacturer>/<board>/
├── <board>_board.cc   board class, initialization, DECLARE_BOARD
├── config.h           pins and board-level constants
├── config.json        reported type, target chip, build variants
├── README.md          hardware notes, measurements, quirks
└── *.cc               optional extra drivers, picked up by a file GLOB
```

Boards can also sit flat under `main/boards/<board>/`. Use
`main/boards/<manufacturer>/<board>/` when the vendor name is meaningful; the
GUACHAT port uses `guaup/guachat` and reports `board.manufacturer` as `guaup`.

## Steps

### 1. Create the board directory

```bash
mkdir -p main/boards/my-vendor/my-board
```

Directory names are lowercase with dashes. The name becomes the firmware's
reported board type, so pick it before the first release and keep it.

### 2. Write `config.h`

Put every hardware constant here. The GUACHAT
[`config.h`](../main/boards/guaup/guachat/config.h) is a worked example; a
minimal one looks like this:

```c
#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// Audio: bare I2S to an external amplifier, no codec chip
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_NC
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_16
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_17
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_18

#define AUDIO_CODEC_PA_PIN       GPIO_NUM_42
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_2
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_1

// Buttons
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define BOOT_BUTTON_ACTIVE_HIGH false

// Display
#define DISPLAY_SPI_SCK_PIN  GPIO_NUM_14
#define DISPLAY_SPI_MOSI_PIN GPIO_NUM_13
#define DISPLAY_DC_PIN       GPIO_NUM_21
#define DISPLAY_SPI_CS_PIN   GPIO_NUM_NC
#define DISPLAY_SPI_CLOCK_HZ (80 * 1000 * 1000)

#define DISPLAY_WIDTH  320
#define DISPLAY_HEIGHT 480
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY  false
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0

#define DISPLAY_RGB_ELEMENT_ORDER LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_INVERT_COLOR true

#define DISPLAY_BACKLIGHT_PIN           GPIO_NUM_46
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

#endif // _BOARD_CONFIG_H_
```

`GPIO_NUM_NC` is the right value for a pin that is not connected: a hard-wired
chip select, an unused MCLK, or a reset line driven through an IO expander.

### 3. Write `config.json`

```json
{
    "manufacturer": "my-vendor",
    "type": "my-board",
    "target": "esp32s3",
    "builds": [
        {
            "name": "my-board",
            "sdkconfig_append": [
                "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y",
                "CONFIG_USE_WECHAT_MESSAGE_STYLE=y"
            ]
        }
    ]
}
```

**Fields**:

- `manufacturer`: optional vendor name, reported as `board.manufacturer`.
- `type`: compatibility-sensitive board identity reported by the firmware.
- `target`: the real chip (`esp32s3`, `esp32p4`, `esp32c3`, ...). It selects
  the target defaults and decides whether the build directory must be wiped.
- `builds[]`: one entry per firmware variant. `name` is the OTA-reported
  variant name; it must be stable after release.
- `sdkconfig_append`: per-variant overrides merged after the project and target
  defaults. List only values that actually differ — a repeat of a project
  default is rejected by `scripts/tests/test_build.py`.

`manufacturer`, `type`, and `name` may contain only lowercase letters, digits,
periods, and hyphens (`^[a-z0-9.-]+$`); CMake enforces this and aborts the build
otherwise.

This repository ships one partition table, [`partitions/16m.csv`](../partitions/16m.csv):
16 MB flash, two 4 MB OTA slots, and an 8 MB `assets` partition. 16 MB and that
CSV are already the project defaults (`sdkconfig.defaults`), so repeating them
here is the most common mistake. A board with different flash needs its own CSV
under `partitions/` first.

Do not select a language or a wake word in `config.json`. Those are user build
options and must stay reachable from `menuconfig`, the CLI, and any automated
build, so `scripts/build.py` sets them through `--language` / `--wake-word`.

### 4. Implement the board class

Derive from `WifiBoard`, which already implements `GetNetwork()`,
`StartNetwork()`, `SetPowerSaveLevel()`, and the board-type/status JSON. A
direct `Board` subclass must implement these pure virtuals itself:

```cpp
std::string GetBoardType();
AudioCodec* GetAudioCodec();
NetworkInterface* GetNetwork();
void StartNetwork();
const char* GetNetworkStateIcon();
void SetPowerSaveLevel(PowerSaveLevel level);
std::string GetBoardJson();
std::string GetDeviceStatusJson();
```

Everything else — `GetDisplay()`, `GetBacklight()`, `GetLed()`, `GetCamera()`,
`GetBatteryLevel()`, `GetTemperature()` — has a safe default (null or `false`).
Treat display, backlight, LED, camera, and battery as optional: a board without
a screen must still build and boot.

```cpp
#include "wifi_board.h"
#include "audio/codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"

#include <driver/gpio.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_st7796.h>

#define TAG "MyBoard"

class MyBoard : public WifiBoard {
private:
    Button boot_button_;
    Display* display_ = nullptr;

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

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 2;
        io_config.pclk_hz = DISPLAY_SPI_CLOCK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = DISPLAY_RGB_ELEMENT_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);

        display_ = new SpiLcdDisplay(panel_io, panel,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

public:
    MyBoard() : boot_button_(BOOT_BUTTON_GPIO, BOOT_BUTTON_ACTIVE_HIGH) {
        InitializeSpi();
        InitializeDisplay();
        InitializeButtons();
        GetBacklight()->SetBrightness(100);
    }

    virtual AudioCodec* GetAudioCodec() override {
        // Bare I2S full duplex: amplifier on DOUT, I2S microphone on DIN.
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
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(MyBoard);
```

`DECLARE_BOARD` must appear exactly once across all compiled board sources; it
exports the single board factory the application calls. Two registrations in one
build is a link-time error by design.

The snippet maps BOOT to `ToggleChatState()`, the upstream default. The shipped
board maps the same key differently: a click blanks and restores the backlight,
a long press while `kDeviceStateStarting` enters Wi-Fi config, and waking XiaoZhi
lights the screen again. Button behaviour is board policy, so choose what the
hardware needs.

### 5. Hook up the build system

#### Kconfig entry

Add a symbol inside `choice BOARD_TYPE` in
[`main/Kconfig.projbuild`](../main/Kconfig.projbuild):

```kconfig
choice BOARD_TYPE
    prompt "Target Board"
    default BOARD_TYPE_GUAUP_GUACHAT

    # ... existing entries ...

    config BOARD_TYPE_MY_BOARD
        bool "My Board (ESP32-S3)"
        depends on IDF_TARGET_ESP32S3
endchoice
```

The identifier is uppercase and underscore-separated. `depends on` gates the
entry to the chip it belongs to; naming an undefined symbol in `depends on`
makes the entry permanently invisible, not merely disabled.

#### CMake branch

Extend the board chain in [`main/CMakeLists.txt`](../main/CMakeLists.txt):

```cmake
if(CONFIG_BOARD_TYPE_GUAUP_GUACHAT)
    set(BOARD_DIR "guaup/guachat")
    set(BUILTIN_TEXT_FONT font_noto_sans_basic_20_4)
    set(BUILTIN_ICON_FONT font_material_symbols_20_4)
    set(DEFAULT_EMOJI_COLLECTION otto-gif)
elseif(CONFIG_BOARD_TYPE_MY_BOARD)
    set(BOARD_DIR "my-vendor/my-board")
    set(BUILTIN_TEXT_FONT font_noto_sans_basic_20_4)
    set(BUILTIN_ICON_FONT font_material_symbols_20_4)
    set(DEFAULT_EMOJI_COLLECTION noto-color-emoji_32)
endif()
```

`BOARD_DIR` is the path relative to `main/boards/`. Sources are loaded by a
file GLOB over that directory, so adding a `.cc` to a board needs an
`idf.py reconfigure` before the next build.

#### Fonts and emoji

Fonts come from the `78/xiaozhi-fonts` component. The text font name is
validated by a CMake regex, `^font_noto_sans_basic_<size>_<bpp>$`, and the
build aborts on anything else. Match the size to the panel:

| Panel | Text font | Icon font |
| --- | --- | --- |
| No display, or 128x64 OLED | project default `font_noto_sans_basic_14_1` | `font_material_symbols_14_1` |
| ~240x240 | `font_noto_sans_basic_16_4` | `font_material_symbols_16_4` |
| 240x320 to 320x480 | `font_noto_sans_basic_20_4` | `font_material_symbols_20_4` |
| 480x320 and larger | `font_noto_sans_basic_30_4` | `font_material_symbols_30_4` |

The component ships exactly four text sizes — 14_1, 16_4, 20_4, 30_4 — and
matching icon fonts, plus `font_material_symbols_30_1` and
`font_noto_emoji_30_1`. A size outside that set fails to link.

`BUILTIN_TEXT_FONT` must always be set to a valid name — CMake checks it even
for a board with no display, and the project-level default is
`font_noto_sans_basic_14_1`.

Emoji collections are `noto-color-emoji_32` (32 px), `noto-color-emoji_64`, and
`noto-color-emoji_128` for large panels. This repository's board instead uses
`otto-gif` — the animated 240x240 Otto face set from
`txp666/otto-emoji-gif-component` — because its idle face is the only element
on screen while idle, and a static 32 px set upscaled to that size looked soft.
`DEFAULT_EMOJI_COLLECTION` is optional; leave it unset if the board has no
display.

#### Component manifest

Custom driver headers must come from a component in
[`main/idf_component.yml`](../main/idf_component.yml). The manifest lists only
what the firmware includes directly; anything else arrives transitively through
those manifests. A board using an ST7789 instead of the shipped ST7796 adds:

```yaml
  espressif/esp_lcd_st7789: ~1.2.0
```

#### Partition table

If the board's flash size or layout differs, add a CSV under `partitions/` and
select it in that variant's `sdkconfig_append`:

```json
"CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y",
"CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions/8m.csv\""
```

Check that `assets` still fits: `main/CMakeLists.txt` reads the partition size
and fails the assets build when the generated `assets.bin` overflows it.

### 6. Build and flash

#### Option A — `scripts/build.py` (recommended)

```bash
python scripts/build.py --list-boards
python scripts/build.py my-vendor/my-board --name my-board
```

The script:

- prints help when run without arguments; `--list-boards` shows every board and
  variant, and `--list-boards --json` includes the per-variant build options;
- prompts for a variant when the board has more than one, and accepts
  `--name <variant>` for non-interactive builds;
- reads `target` from `config.json`, wipes the build directory only when its
  target differs, then applies the target, board name, project defaults, and the
  variant's `sdkconfig_append` in a single `idf.py reconfigure`;
- produces `build/merged-binary.bin` (flash at `0x0`); pass `--zip` to also
  write `releases/v<version>_<name>.zip`.

Language and wake-word selection are user options, not board options:

```bash
python scripts/build.py my-vendor/my-board --name my-board \
  --language en-US --wake-word nihaoxiaozhi

python scripts/build.py --list-languages
python scripts/build.py --list-wake-words
```

`--language` accepts a locale directory under `main/assets/locales/`
(`zh-CN` is the default, `en-US` ships too). `--wake-word` accepts a model name
reported by `--list-wake-words`, `nihaoxiaozhi`, or `disabled`. The wake-word
list is read from the resolved ESP-SR component, so run `idf.py reconfigure`
once if `managed_components/` is still empty.

#### Option B — `idf.py` directly

```bash
idf.py set-target esp32s3
idf.py menuconfig     # Xiaozhi Assistant -> Target Board
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Switch targets with `idf.py set-target` and then `idf.py fullclean`; a build
directory left over from another chip produces confusing link errors.

### 7. Write the board README

Document what a schematic does not: measured backlight limits, amplifier gain
and clipping thresholds, which peripherals were probed and found absent, the
power-latch sequence, and anything a future maintainer would otherwise have to
rediscover with a scope. The GUACHAT
[board README](../main/boards/guaup/guachat/README.md) is the model.

## Manufacturer Sub-directories

Group boards under `main/boards/<manufacturer>/<board>/` when one vendor ships
several, and set `"manufacturer"` in each `config.json`. The firmware reports it
alongside `board.type` and `board.name` in `GetBoardJson()`. A board without the
field reports an empty manufacturer string.

Set `BOARD_DIR` to the full path relative to `main/boards/`:

```cmake
elseif(CONFIG_BOARD_TYPE_MY_VENDOR_MY_BOARD)
    set(BOARD_DIR "my-vendor/my-board")
```

If `config.json` is missing or has no top-level `type`, CMake falls back to
`BOARD_DIR` with `/` replaced by `-`, which is a valid but ugly board identity —
declare it explicitly.

## Common Board Components

`main/boards/common/` holds the board-facing layer:

| File | Contents |
| --- | --- |
| `board.h` | `Board` interface, `DECLARE_BOARD`, network events, `PowerSaveLevel` |
| `wifi_board.h` | `WifiBoard` base class, including `EnterWifiConfigMode()` |
| `backlight.h` | `Backlight` interface and `PwmBacklight(pin, invert, freq_hz)` |
| `button.h` | `Button` with click, long-press, and multi-click handlers |
| `blufi.h` | BluFi provisioning transport, built only with `CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING` |
| `camera.h` | `Camera` interface for optional camera hardware |

Reusable pieces outside that directory:

- **Audio** — `main/audio/codecs/no_audio_codec.h` provides `NoAudioCodecDuplex`,
  `NoAudioCodecSimplex`, and `NoAudioCodecSimplexPdm` for bare I2S links with no
  codec chip. A board with an ES8311 or similar needs its own `AudioCodec`
  subclass under its board directory; the shared code deliberately ships none.
- **Display** — `main/display/lcd_display.h` provides `SpiLcdDisplay` and
  `RgbLcdDisplay`; the panel driver itself comes from an ESP-IDF component.
- **MCP tools** — `main/mcp_server.h` and [MCP IoT usage](./mcp-usage.md).
  Register board-specific tools from the board class, not from core code.

## Board Class Hierarchy

```
Board
└── WifiBoard
```

`WifiBoard` is the only shipped base class. A board with a cellular modem or
Ethernet implements `NetworkInterface` itself and overrides `GetNetwork()`,
`StartNetwork()`, `GetNetworkStateIcon()`, and `SetPowerSaveLevel()` directly.

## Tips

1. **Copy the closest board**, rename the identity, strip what the hardware does
   not have. Only the GUACHAT implementation is shipped here, so strip rather
   than assume.
2. **Bring up incrementally** — boot log, then backlight, then display, then
   audio, then the full stack.
3. **Verify every pin twice** against the schematic, including strapping pins and
   pins that are inputs at reset.
4. **Check the optional-capability paths** — build and boot once with your
   display, touch, and battery code disabled, the same way a no-display board
   compiles.
5. **Clamp the backlight in software.** A panel rated for 160 mA does not
   survive a 100 % LEDC duty request; the GUACHAT board wraps `PwmBacklight` to
   cap it. See `GuachatBacklight` in `guachat_board.cc`.

## Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| Display mirrored, colour-swapped, or offset | `DISPLAY_MIRROR_*`, `DISPLAY_SWAP_XY`, `rgb_ele_order`, `esp_lcd_panel_invert_color`, `DISPLAY_OFFSET_*` |
| No audio output | I2S BCLK/WS/DOUT, amplifier enable pin, sample-rate mismatch |
| Microphone silent | I2S DIN pin, clock source, or the chosen `NoAudioCodec*` variant |
| Garbled or dropped capture | I2S DMA size and CPU load; check other SPI/I2S consumers |
| Boot loop with power latch | Latch output driven low on reset; set it high first thing in init |
| Cannot connect to Wi-Fi | Credentials, provisioning method, antenna |
| Build selects the wrong board | Missing Kconfig entry or CMake branch; run `idf.py reconfigure` |
| New `.cc` ignored by the build | The board source GLOB is not `CONFIGURE_DEPENDS`; run `idf.py reconfigure` |

## References

- [ESP-IDF documentation](https://docs.espressif.com/projects/esp-idf/)
- [LVGL documentation](https://docs.lvgl.io/)
- [ESP-SR documentation](https://github.com/espressif/esp-sr)
- [Board README for the shipped board](../main/boards/guaup/guachat/README.md)
