# GUACHAT 2G4 (ESP32-S3) — XiaoZhi board port

XiaoZhi 2.5 for the GUACHAT 2G4 handheld. This is a **third-party board port**, not
part of the upstream FoloToy release: everything here was written against
[gitee.com/guaup/guachat](https://gitee.com/guaup/guachat) as the hardware source
of truth.

## Hardware

| Item | Value |
| --- | --- |
| MCU | ESP32-S3-WROOM-1U N16R8, 16 MB flash, 8 MB OPI PSRAM |
| Display | ST7796, 320x480 portrait, SPI2, **BGR + inverted**, CS hard-wired |
| Display reset | TCA9535 P16 (not a GPIO) |
| Touch | FT6336U on I2C1 (SDA 47 / SCL 48, 400 kHz, 0x38), reset via TCA9535 P15, INT GPIO45 unused |
| Haptics | DRV2605L LRA on I2C0 0x5A |
| Audio out | MAX98357 over I2S0, SD_MODE on GPIO42 |
| Audio in | I2S microphone on I2S0 RX |
| Fuel gauge | MAX17055 on I2C0 0x36 |
| IO expander | TCA9535 on I2C0 0x20 |
| Keys | BOOT/lock GPIO0, volume up P01, volume down P00 (low active) |
| Backlight | GPIO46, LEDC 10 kHz — op-amp + MOS constant-current sink, **160 mA ceiling at 8.3% duty** |
| Power LED | none — the boot vibration is the only power-on feedback |

## Differences from the AI Passport port this was based on

* **S3 instead of C3**, with PSRAM. Wake word (ESP-SR) is enabled.
* **No codec chip.** Audio uses `NoAudioCodecDuplex`; the MAX98357 amplifier is
  enabled by driving GPIO42 high from the board, because `NoAudioCodec` does not
  own an amplifier pin.
* **ST7796 320x480** instead of ST7789 240x320, with BGR element order and
  inverted colour.
* **Panel CS and RST are not GPIOs.** CS is hard-wired; RST is pulsed through the
  TCA9535 before the controller is initialised.
* **Volume keys are on the IO expander**, not an ADC resistor ladder, so they are
  polled by a small task with ~75 ms of debounce instead of using `AdcButton`.
* **Backlight is clamped.** GPIO46 drives an op-amp + MOS active filter into a
  DC constant-current sink, so duty cycle maps linearly to current and the
  panel's 160 mA limit is reached at 8.3% duty. The clamp holds 100% brightness
  at 7.9% duty (~152 mA) — just under the ceiling, since overshooting a
  constant-current limit is what actually destroys backlight strings.

## Power latch warning

The TCA9535 drives the board's power latch on **P03 (high = run)**. The
initialisation order in `guachat_board.cc` is deliberate:

1. write the output latch while the pins are still inputs,
2. only then switch the direction registers to outputs.

Doing it the other way round briefly drives the power-on default (low) and shuts
the device down. Do not "simplify" this sequence. See
`guachat:src/I2C0/TCA9535.cpp` for the original analysis.

## Build

Requires ESP-IDF v6.0.1 or newer (v6.1 recommended).

```bash
python scripts/build.py guaup/guachat --name guachat --language zh-CN
```

Output: `build/merged-binary.bin`, flash at `0x0`.

Verified build (ESP-IDF 6.0.3, `esp-15.2.0_20251204` toolchain):

```
xiaozhi.bin  2,737,024 bytes  (0x29C380)
app partition 4,128,768 bytes (0x3f0000) -> 34% free
```

The `type` field in `config.json` (`guachat`) is the board's OTA identity. Keep it
unique — reusing another board's identifier means a future OTA can overwrite this
firmware.

### Changes this port needed outside the board directory

Three fixes were required to build an ESP32-S3 target from this fork at all. They
are generic, not guachat-specific, and would apply to any non-C3 board:

1. **`main/Kconfig.projbuild`** — added `BOARD_TYPE_GUAUP_GUACHAT` to the
   `BOARD_TYPE` choice, defaulting for `IDF_TARGET_ESP32S3`.
2. **`main/CMakeLists.txt`** — the fork ships only the common hardware layer the
   AI Passport needs, but unconditionally appended `boards/common/esp_video.cc`,
   `rndis_board.cc` and `esp32_camera.cc` for S3 targets. Those files do not
   exist here, so CMake Generate failed. The appends are now guarded by `EXISTS`.
3. **`scripts/build_default_assets.py`** — the `finally` block removed
   `build/temp_build` with `shutil.rmtree`. In environments where `rmtree` is
   hooked to route deletions through the shell recycle bin it raises, which
   masked an already-successful `assets.bin`. Cleanup now goes through a
   `remove_tree()` helper that falls back to a walk-based delete.

One build quirk worth knowing: `BOARD_SOURCES` is a plain `file(GLOB)` because
`main/CMakeLists.txt` is also included by `idf_component_get_requirements()` in
CMake script mode, which rejects `CONFIGURE_DEPENDS`. Adding a new `.cc` to a
board directory therefore needs an explicit `idf.py reconfigure` before the next
build, or the file is silently left out and you get undefined references.

### Build environment notes

`idf_tools.py export` was not usable on the machine this port was developed on:
`tools.json` pins cmake 4.0.3 and openocd `20260424`, both only downloadable from
`github.com`, which the network blocked. Neither matters for compiling, so the
environment was assembled by hand there. That workaround is local to one
development setup and is not part of this repository — a normal
`idf_tools.py export` (or the ESP-IDF VS Code extension) is all a standard
install needs.

The one non-obvious flag worth knowing: `IDF_SKIP_CHECK_SUBMODULES=1`. Setting
it skips CMake's submodule consistency check, which otherwise shells out to git
and tries to fetch from `github.com` on a tree whose submodules report as
uninitialised even though the contents are present.

## Flashing note

`merged-binary.bin` written at `0x0` can overwrite the gap regions and may clear
the NVS partition, which holds Wi-Fi credentials and device pairing data. For an
already-provisioned device, prefer the segmented flash using `build/flash_args`.

This port does **not** share flash layout or NVS with the guachat
PlatformIO/Arduino firmware (`guachat_dev`). They are two separate products: flash
one or the other.

If the board ends up in a `TG0WDT_SYS_RST` boot loop after flashing, switch the
flash mode to DIO (`CONFIG_ESPTOOLPY_FLASHMODE_DIO=y` in `config.json`). The
guachat docs report that loop when a QIO header is forced onto a DIO bootloader.
ESP-IDF generates both bootloader and app from one config so they stay consistent,
but the warning is worth knowing about if the board refuses to boot.

## Status

Verified on real hardware (one board, `SKU=guachat`, UUID
`7076b46d-dbdd-463e-883a-f7a7dd0556b5`):

* Boots with no reset loop; TCA9535 power latch holds, 212 KB SRAM free.
* ST7796 320x480 renders correctly — BGR + inverted + **horizontal mirror**
  (`DISPLAY_MIRROR_X=true`); the panel scans in the opposite direction to what
  LVGL assumes, and mirroring on Y as well would flip it upside down again.
* MAX98357 + I2S microphone: Chinese TTS prompt is audible at boot.
* Volume keys on the TCA9535 work (the polled debounce task responds).
* Wi-Fi provisioning AP comes up, the device joins a network, pairs with a
  XiaoZhi agent, and holds a normal voice conversation.
* **DRV2605L haptics**: the LRA calibration already stored in NVS by the stock
  firmware is reused, the boot cue fires, and the volume keys tick.
* **MAX17055 fuel gauge**: SOC, cell voltage and current decode correctly and
  drive the status-bar battery icon and the percentage label beside it,
  including the charging bolt. The percentage is LVGL's built-in 12 px
  Montserrat at native size (enabled in `sdkconfig.defaults`; the
  `78/xiaozhi-fonts` bundle has no 12 px Noto Sans, and shrinking a larger
  bitmap down blurs it), and the battery glyph uses the 30 px Material Symbols
  font while the network and mute icons stay at 20 px — the 20 px battery
  outline was too small to read on this panel. The percentage is nudged 5 px
  towards the icon with `lv_obj_set_style_translate_x`; with the flex gap alone
  the small digits floated too far left of the larger glyph. That offset is
  deliberate, not leftover tuning.
* **FT6336U touch**: initialises at 0x38 and registers an LVGL pointer device;
  drags scroll the chat, taps toggle it.
* **Screen power**: a BOOT click blanks and restores the backlight, and the
  5-minute idle timeout blanks it on its own (measured: backlight 0 exactly
  300 s after the device reached `idle`). Waking by wake word, tap or server
  push lights the panel again; the wake-word path is `idle -> connecting`, which
  is what the policy task watches for.
* Magnetometer (QMC5883P) is **not populated on this PCB**, so nothing reads it.

This is a working firmware, not a demo: a user can provision it and talk to it.
It is **not** production-grade — see below.

## Note on display mirroring

`esp_lvgl_port` re-applies `swap_xy` / `mirror_x` / `mirror_y` from
`lvgl_port_display_cfg_t.rotation` when the LVGL display is registered, and
MADCTL bits are written absolutely rather than toggled. That second write
overwrites any `esp_lcd_panel_mirror()` call made earlier in `InitializeDisplay()`.
So the macros in `config.h` are the only knob that takes effect; the manual calls
in the board code are there purely so anything drawn before LVGL starts is
already oriented correctly.

## Note on MAX17055 byte order

The datasheet says MSB first. This part delivers **LSB first** on the wire, and
both guachat reference implementations already assume that (`bsp_power.cpp`
takes the low byte first; the vendored `MAX17055` library combines as
`(buf[1] << 8) | buf[0]`). A raw register dump settled it:

| Register | Bytes | LSB-first | MSB-first |
| --- | --- | --- | --- |
| DesignCap `0x18` | `A0 28` | 5200 mAh (matches the configured pack) | 20500 mAh |
| VCell `0x09` | `84 DB` | 4390 mV (matches RepSOC = 100%) | 2657 mV |
| Current `0x0A` | `D5 01` | +73 mA, trickle charging | -1719 mA |

Reading it the datasheet way describes a pack that is simultaneously
over-capacity and nearly dead. `ReadReg16()` decodes LSB first on purpose.

Related: VCell is 78.125 uV/LSB, i.e. exactly **5/64 mV**. Writing that as
`raw * 78125 / 1000000` overflows 32-bit arithmetic above ~54.9 k LSB (4.29 V),
which is inside this pack's range, so the driver uses the ratio form.

## Why device-side AEC is not implemented

XiaoZhi supports two ways to interrupt playback here: the on-screen tap (which
calls `AbortSpeaking`) and the wake word, which stays armed while the device
speaks. The BOOT key is the screen off/on key, not a playback control. What it
cannot do is full-duplex "talk over it" — that needs
`kListeningModeRealtime`, which needs AEC, which needs `CONFIG_USE_DEVICE_AEC`
or `CONFIG_USE_SERVER_AEC`.

`guachat_acoustic_probe.cc` measures whether the hardware could support it. It
plays a 100 ms linear chirp (500-3000 Hz), records the microphone at the same
time, and cross-correlates. Results on real hardware:

| Measurement | Value |
| --- | --- |
| Echo delay (I2S write -> I2S read) | **151.7 ms** (repeatable to 0.5 ms) |
| Amplifier linearity, 70% -> 30% volume | **15.1 dB** drop, expected 14.7 dB |
| Echo path gain, digital reference -> mic ADC | ~**+40 dB** |
| Microphone noise floor | ~-14 dBFS |
| Microphone DC offset | +4400 to +12800 counts, unstable |

The amplifier and speaker are linear, so the linear-filter assumption AEC needs
does hold. The problem is level: with the reference at just 4.6% of full scale
the microphone already rails at 32767 counts, so **any usable playback volume
clips the microphone**. AEC cannot subtract a clipped echo, and that is not a
tuning problem.

The coupling is mechanical, not acoustic — a 1 W speaker cannot produce +40 dB
of air-path gain at any sane distance. The stock firmware's power-tree document
says the same thing from the other direction: *"audio record and playback work
in time division"*. Software cannot fix this.

What would: move the microphone away from the speaker, add vibration isolation
between them, or cap playback volume hard in realtime mode (which costs sound
quality). Alternatively `CONFIG_USE_SERVER_AEC=y` moves the problem to the
server, at the cost of being marked unstable upstream.

The board ships with a 4 ohm 2 W speaker, not the 8 ohm 1 W one the stock
power-tree document specifies. That is +3 dB of acoustic output for the same
amplifier swing, and roughly double the peak current, so it makes the echo
problem worse rather than better. Changing the MAX98357 GAIN resistor from
100 k to 0 ohm only moves the gain from 6 dB to 3 dB - about 1/15th of the
~44 dB that would be needed, so it is not a fix either.

**Decision: device-side AEC is out of scope for this port.** Interrupting
playback still works through the on-screen tap and the wake word. Full-duplex
"talk over it" does not, and will not until the acoustics are reworked.

The probe is kept in the tree, disabled (`GUACHAT_ACOUSTIC_PROBE 0`). Setting it
to 1 makes it measure once and remember the numbers in NVS; 2 re-measures every
boot.

## Task priorities

The stock firmware publishes its task layout in
`guachat:docs/task-layout-reference.md`:

```
voice(6) > ui(5) > audio(4) > sensor(3) > background(1) > idle(0)
```

with the ordering justified in one line: *"`voice` must sit above `ui`, otherwise
dragging the UI steals the I2S feed and the DMA underruns."* Its `ui` task runs
at priority 5 with an 8 KB stack.

This port inherited the AI Passport settings instead, which pin the renderer to
priority 1. That is correct on a single-core C3 — keep the renderer out of the
audio path's way — but on the dual-core S3 it just hands core 1 to the audio
tasks for free. Measured layout before and after aligning with the reference:

| Task | before | after | CPU share |
| --- | --- | --- | --- |
| `audio_input` | 8, core 0 | 8, core 0 | 1.1% |
| `audio_afe` | 3 | **6** | 9.6% |
| `audio_output` | 4 | **6** | 0.0% |
| `taskLVGL` | **1**, 7168 B stack | **5**, **8192 B** stack | 0.6% |
| `wifi` | 23, core 0 | unchanged | 0.2% |

The stack change was not cosmetic: the old 7168 B left only **1240 B** of
headroom, i.e. 84% used. At 8192 B it is 2264 B.

Measured with `guachat_task_report.cc` (`GUACHAT_TASK_REPORT`, off by default),
which samples `uxTaskGetSystemState` twice five seconds apart so the boot storm
does not dominate the numbers.

Worth knowing: at idle, core 1 carries about 10% load, almost all of it
`audio_afe` running WakeNet continuously. The display stall was never a CPU
problem — it was SPI bandwidth, see *Display performance* below.

## Display performance

The stock guachat firmware runs this panel with two full-screen LVGL buffers in
PSRAM at 80 MHz SPI. This port originally inherited the AI Passport settings
instead — a single 12.8 KB internal-RAM strip at 40 MHz — which is a poor match
for a 320x480 panel: a full refresh needs 24 strip flushes, each waiting for its
DMA transfer to finish before LVGL can render the next one.

Now matched to the stock firmware:

| | before | after |
| --- | --- | --- |
| Buffers | 1 x 12.8 KB, internal SRAM | **2 x 300 KB, PSRAM** |
| SPI clock | 40 MHz | **80 MHz** |
| Full-frame flush | 24 strips, serialised | 1 transfer, overlapped |
| Measured full frame | ~60-70 ms | **31.2 ms (9.4 MB/s)** |

80 MHz SPI tops out at 10 MB/s, so 9.4 MB/s is 94% of the theoretical ceiling
and the measurement matches the arithmetic.

Three settings are needed together, and omitting any one of them breaks display
output rather than merely slowing it down:

1. `buffer_size = width * height` with `double_buffer = true`
2. `flags.buff_spiram = 1` so the buffers are allocated from PSRAM
3. **`esp_lcd_panel_io_spi_config_t.flags.psram_dma_direct = 1`**

The third one is the non-obvious one. The ESP32-S3 SPI DMA cannot read PSRAM
directly, so by default the driver allocates an internal-RAM staging buffer the
size of the transfer and memcpy's into it. For a 300 KB frame that allocation
fails and every `draw_bitmap` errors out with:

```
E spi_common: spicommon_dma_setup_priv_buffer(430): Failed to allocate priv TX buffer
```

`psram_dma_direct` sets `SPI_TRANS_DMA_USE_PSRAM`, letting the DMA read PSRAM in
place. It trades some peak bandwidth for not needing the staging buffer, which
is the only way a full frame buffer fits on this chip.

Also note `full_refresh` must stay **off**. Turning it on selects
`LV_DISPLAY_RENDER_MODE_FULL`, which re-renders the entire screen on every
change — including a status-bar clock tick. A full-size buffer in PARTIAL mode
still lets LVGL send only the dirty rectangles.

Boards without PSRAM fall back to the original internal-SRAM strip: the code
checks free PSRAM before committing to full-frame buffers.

## Chat display: bubbles, not a single scrolling line

This port enables `CONFIG_USE_WECHAT_MESSAGE_STYLE`, which is what turns the
conversation into the WeChat-style bubble list: user messages right-aligned in
green, assistant messages left-aligned in grey, system notices centred, bubbles
capped at 85% of the screen width, oldest dropped past 20 messages, scrolled to
the newest automatically.

The code for it already shipped in this fork — `LcdDisplay::SetupUI()` has two
implementations selected by that macro — so no UI had to be written. Without it
the UI is the default style: one centred emoji plus a single scrolling line of
text at the bottom.

Two fixes were needed to make it look right:

1. **`LvglDisplay::SnapshotToJpeg()` swapped colour bytes that must not be
   swapped.** LVGL 9 stores `LV_COLOR_FORMAT_RGB565` little-endian (blue in the
   low bits), which already matches what `V4L2_PIX_FMT_RGB565` expects. The
   extra `__builtin_bswap16` rotated the channels: a pure red swatch came out
   blue, pure green came out red. This also affected the MCP
   `self.screen.snapshot` tool, not just debug captures.
2. **The emotion image covered the first message.** It was a `TOP_MID` overlay
   on the screen, and the bubble list starts directly under the top bar. It is
   now an *idle face*: drawn large in the middle of the panel while there is no
   conversation, hidden the moment a real message arrives, and brought back by
   `ClearChatMessages()`. The face is the **animated** Otto set from
   `txp666/otto-emoji-gif-component` (`otto-gif` in `DEFAULT_EMOJI_COLLECTION`):
   21 GIFs, 240x240, ~1 MB in the 8 MB assets partition, rendered 1:1 at about
   12.5 fps. Each GIF is a black robot face plate with white eyes; the
   background is baked into the assets, there is no alpha channel to drop.
   Because of that the board boots with the **dark theme** (`0x000000`
   background, white status bar text and icons) so the plate blends into the
   panel and only the eyes are visible. Earlier firmware persisted the old
   light default into NVS, so `LcdDisplay` performs a one-time migration
   (`dark_migrated` key) that forces dark once; a later
   `self.screen.set_theme` is stored and honoured as before.
   `IdleFaceScale()` only upscales a collection smaller than 96 px (e.g. the
   earlier `noto-color-emoji_32` PNG set, which looked soft). The 30 px
   Material Symbols glyph used as a fallback is scaled through an LVGL object
   transform (`transform_scale`, 3.2x) because no larger symbol font is
   bundled. `SetEmotion()` will not reveal the face while messages are on
   screen, and the activation page hides it as well.

`guachat_ui_snapshot.cc` (off by default, `GUACHAT_UI_SNAPSHOT 0`) seeds a few
messages, snapshots the screen and dumps the JPEG over serial as base64; decode
the captured stream to get a viewable image. The panel is the one part of this
board that a log cannot verify, so being able to capture it in process is worth
the extra file.

## Note on touch gestures

XiaoZhi's UI is a status display with no widgets to press. The FT6336U is
registered as an ordinary LVGL pointer device, so dragging up or down scrolls
the chat bubbles with LVGL's own scrolling. On top of that, `guachat_touch.cc`
recognises one deliberate gesture and hands it to the application task:

| Input | Action |
| --- | --- |
| Tap (no travel, < 500 ms, LVGL never scrolled) | toggle chat |
| Drag up/down | scroll the chat bubbles (LVGL scrolling, no app involvement) |
| BOOT click | screen off / on; only the backlight is blanked, LVGL keeps running |
| BOOT long press while starting | enter Wi-Fi config |
| Wake word / tap / server push | wakes the screen automatically if it was off |
| Five idle minutes | screen blanks automatically |
| Volume up/down keys | volume ±10 with a haptic tick; the only volume control |

Tap recognition also asks LVGL whether it scrolled something during the press
(`lv_indev_get_scroll_obj`). Distance alone was not enough: a short scroll can
stay under the 24 px travel limit, and a drag that comes back near its start
passes the net-displacement test, so scrolling sometimes toggled the chat. A
gesture that made LVGL scroll is a drag, full stop.

Screen power is a small policy task (`guachat_screen`, 250 ms poll) rather than
a state-machine listener: it wakes the panel whenever the device leaves
`kDeviceStateIdle`, and blanks it after 300 s back in idle. Because only the
backlight is touched, the chat position, the idle face and the battery level are
exactly as they were when the screen comes back.

Taps are logged with their coordinates, so the horizontal flip
(`flags.mirror_x`, which must match `DISPLAY_MIRROR_X`) can be checked from the
serial log if the mapping ever feels wrong.

## Remaining work

Ordered by what a user would notice first, not by difficulty.

* **Device-side MCP tools for GPS / LoRa / IMU / barometer.** This is the reason
  to run XiaoZhi on this board instead of a generic ESP32-S3: the model could
  answer "where am I", "how high am I", "send a LoRa message". All of that
  hardware works but is currently unused by the firmware. The magnetometer is
  the one exception — the QMC5883P is not fitted.
* **Touch beyond the gesture layer.** Tap plus drag-to-scroll is the whole
  interaction model today. Real use needs either on-screen buttons or a settings
  page, which means UI work, not driver work.
* **Long-run and failure-path testing.** No soak test, no Wi-Fi reconnect test,
  no low-battery behaviour, no "what happens if the TCA9535 stops answering".
* **OTA identity.** `type: guachat` is a made-up board id. Pointing the device at
  the official XiaoZhi OTA server would need that id registered, or a self-hosted
  OTA endpoint. Until then `CONFIG_DISABLE_OTA_UPGRADE` (default on) keeps the
  check-in — which carries the activation code and server address — but ignores
  any offered firmware and does not register the `self.upgrade_firmware` tool.
  Firmware is flashed over USB.
* **Upstreaming.** The FoloToy repo ships only the AI Passport board. Merging this
  port upstream would need the three generic fixes listed under *Build* to be
  accepted first, since none of them are guachat-specific.

