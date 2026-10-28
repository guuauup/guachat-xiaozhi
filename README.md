# XiaoZhi for GUACHAT 2G4 (ESP32-S3)

![ESP-IDF](https://img.shields.io/badge/ESP--IDF-6.0.1%2B-blue)
![Target](https://img.shields.io/badge/target-ESP32--S3-informational)
![Board](https://img.shields.io/badge/board-GUACHAT%202G4-critical)
![License](https://img.shields.io/badge/license-MIT-green)

A voice assistant on a handheld: offline wake word, speech recognition, an LLM, and
text-to-speech, rendered as a chat on a 320x480 touch panel. The models run on a
server; this firmware runs the device — audio I/O, wake word, UI, networking, and
the device-side tools the model can call.

This repository is a third-party board port of
[XiaoZhi](https://github.com/78/xiaozhi-esp32), written against
[gitee.com/guaup/guachat](https://gitee.com/guaup/guachat) as the hardware source of
truth. It ships exactly one board implementation:
[`main/boards/guaup/guachat`](main/boards/guaup/guachat). The upstream snapshot and
attribution are in [`NOTICE`](NOTICE).

| Start here | |
| --- | --- |
| Hardware, pins, measured behaviour | [Board README](main/boards/guaup/guachat/README.md) |
| Building your own board | [Custom board guide](docs/custom-board.md) |
| Something is broken | [SUPPORT.md](.github/SUPPORT.md) |
| 中文简介 | [README_zh.md](README_zh.md) |

## What it does

- **Wake word, offline.** ESP-SR WakeNet, `wn9_nihaoxiaozhi_tts` ("你好小智") on
  this target, runs continuously and stays armed while the device is speaking, so
  saying the wake phrase interrupts playback.
- **Conversation.** Streaming ASR → LLM → TTS, or a realtime speech-to-speech
  model, carried as Opus over WebSocket or MQTT + UDP.
- **Chat UI.** `CONFIG_USE_WECHAT_MESSAGE_STYLE` turns the conversation into
  WeChat-style bubbles: user right in green, assistant left in grey, system
  notices centred, 20 messages kept, auto-scrolled.
- **Animated idle face.** While the panel has no conversation the current
  emotion plays as a 240 px animated GIF (the Otto robot face set) in the
  middle of the screen; it disappears as soon as a message arrives and returns
  when the chat is cleared, so it never covers a bubble.
- **Dark theme by default.** Black background with white status bar text and
  icons, so the black face plate blends into the panel. `self.screen.set_theme`
  still switches to light.
- **Status bar.** Battery icon and percentage driven by the MAX17055 fuel gauge,
  including the charging bolt, plus volume and network state.
- **Touch.** Dragging up/down scrolls the chat bubbles; a tap toggles the chat.
  There are no widgets to press — the panel is a status display.
- **Keys.** Volume up/down on the expander change the volume (nothing else does).
  A BOOT click blanks the backlight — screen off, press again to wake — and a
  long press enters Wi-Fi config straight from the starting state. Waking
  XiaoZhi (wake word, screen tap or a server message) lights the panel back up,
  and five idle minutes blank it automatically.
- **Device tools via MCP.** Volume, screen brightness and theme, a screen
  snapshot, system info, reboot, and an asset download URL — all callable by the
  model through `self.*` tools.
- **Provisioning.** A hotspot AP with a web page at `192.168.4.1`, or BluFi.
- **Haptics.** DRV2605L fires on boot and on volume key presses, reusing the LRA
  calibration the stock firmware stored in NVS.
- **Languages.** `zh-CN` (default) and `en-US`, selected at build time.

**Not supported**

- **Full-duplex "talk over playback".** With any usable volume the microphone
  clips, so echo cancellation cannot work on this PCB. Playback can still be
  interrupted by a screen tap or the wake word. The measurements
  are in the [board README](main/boards/guaup/guachat/README.md#why-device-side-aec-is-not-implemented).
- **OTA firmware installs.** The board id is not registered with an OTA server, so
  upgrades offered by the check-in response are logged and ignored, and the
  `self.upgrade_firmware` tool is not registered. Firmware arrives over USB. See
  [Status](#status).

## Hardware

| Item | Value |
| --- | --- |
| MCU | ESP32-S3-WROOM-1U N16R8 — 16 MB flash, 8 MB OPI PSRAM |
| Display | ST7796, 320x480 portrait, SPI at 80 MHz, BGR + inverted |
| Touch | FT6336U on I2C1, 0x38 |
| Haptics | DRV2605L LRA on I2C0, 0x5A |
| Audio out | MAX98357 over I2S0, amplifier enable on GPIO42 |
| Audio in | I2S microphone on I2S0 RX |
| Fuel gauge | MAX17055 on I2C0, 0x36 |
| IO expander | TCA9535 on I2C0, 0x20 — display reset, touch reset, power latch, volume keys |
| Backlight | GPIO46 via LEDC, current-clamped to the panel's 160 mA limit |
| USB | ESP32-S3 native USB-Serial/JTAG (no UART bridge) |

The pin map, the power-latch initialisation order, and the measured panel and audio
behaviour are documented in the
[board README](main/boards/guaup/guachat/README.md).

## Quick start

Requires ESP-IDF **v6.0.1 or newer** (**v6.1** recommended; 5.x is not supported)
and an ESP32-S3 with 16 MB flash.

```sh
# 1. ESP-IDF environment (or use idf.cmd on Windows, see Build)
. $IDF_PATH/export.sh

# 2. Build the only variant in this tree
python scripts/build.py guaup/guachat --name guachat

# 3. Flash over USB
idf.py -p COM9 flash
```

4. The panel shows a hotspot name such as `Xiaozhi-487D`. Join it from a phone and
   open `http://192.168.4.1` to enter Wi-Fi credentials (BluFi is the alternative).
5. Once online, the device shows an activation QR code. Scan it and bind the device
   in xiaozhi.me — this is what pairs it with a server. The check-in that carries
   the activation code also carries the server address, which is why it stays
   enabled even though firmware upgrades are refused.

## Build

Source the IDF environment first:

```sh
. $IDF_PATH/export.sh        # or export.bat on Windows
idf.py --version
```

On Windows this repository also ships `idf.cmd` / `idf.ps1`, which do the activation
for you: they locate an ESP-IDF 6.x install (via `IDF_PATH`, the VS Code extension
settings, or `esp_idf.json`), source its `export.ps1`, and forward the arguments to
`idf.py` — `idf.cmd build`, `idf.cmd -p COM9 flash`, `idf.cmd reconfigure`. Pass
`-IdfPath C:\path\to\esp-idf` to override the discovery.

```sh
# Discover the board and its variants
python scripts/build.py --list-boards

# Canonical build
python scripts/build.py guaup/guachat --name guachat
```

Output is `build/merged-binary.bin` (flash at `0x0`) plus the segmented
`build/flash_args`. The app partition is 4,128,768 B (0x3f0000), roughly a third
free. `--zip` additionally produces `releases/v<version>_guachat.zip`.

Useful switches:

| Option | Effect |
| --- | --- |
| `--language LOCALE` | UI/prompt locale; `--list-languages` shows the values |
| `--wake-word MODEL` | ESP-SR model, a logical name such as `nihaoxiaozhi`, or `disabled`; `--list-wake-words` lists what the component provides |
| `--zip` | Also build a release zip under `releases/` |

Two things to know when changing the board directory:

- Board sources are collected with a plain `file(GLOB)`, because
  `main/CMakeLists.txt` is also included in CMake script mode, which rejects
  `CONFIGURE_DEPENDS`. **A newly added `.cc` needs an explicit
  `idf.py reconfigure`**, or it is silently left out and you get undefined
  references.
- The board identity (`type` in `main/boards/guaup/guachat/config.json`, currently
  `guachat`) is the OTA identity. Do not reuse another board's identifier.

CI builds the variant in the `espressif/idf:v6.1` container on every push to `main`
and uploads `build/merged-binary.bin`; pull requests build only when the changed
files affect a variant. The prepare job also asserts that `--list-boards` returns
exactly one board, `guaup/guachat` — adding a board means updating that assertion
in the same change.

## Flash

```sh
idf.py -p <PORT> flash
```

or write the images produced by `scripts/build.py` with esptool using
`build/flash_args`:

```sh
python -m esptool --chip esp32s3 -b 460800 --before default-reset --after hard-reset \
  write-flash "@build/flash_args"
```

On an already-provisioned device prefer the segmented layout. A merged image
written at `0x0` overwrites the gap regions and can clear the NVS partition, which
holds Wi-Fi credentials and pairing data.

If the board ends up in a `TG0WDT_SYS_RST` boot loop, set
`CONFIG_ESPTOOLPY_FLASHMODE_DIO=y` in the board's `config.json`.

This port does not share flash layout or NVS with the stock guachat
PlatformIO/Arduino firmware: flash one or the other.

## How it works

| Path | Role |
| --- | --- |
| `main/application.cc` | Boot sequence, main event loop, protocol lifecycle, voice state |
| `main/device_state_machine.cc` | Legal runtime transitions (`starting` → `activating` → `idle` …) |
| `main/protocols/` | Transport-neutral `Protocol` API plus WebSocket and MQTT/UDP implementations |
| `main/audio/` | Codec, wake word, audio service tasks, AEC modes, queues |
| `main/display/`, `main/led/` | LVGL UI and LED implementations |
| `main/mcp_server.cc` | Device-side MCP tools and dispatch |
| `main/boards/` | `Board` interface, common helpers, and this board's implementation |

Core code depends only on the `Board` interface. Runtime state changes go through
`Application::SetDeviceState()`; callbacks that fire outside the main task schedule
their work with `Application::Schedule()` or event bits. Both rules keep the audio
path and event loop free of blocking work.

## Repository layout

| Path | Contents |
| --- | --- |
| `main/` | Firmware sources: application loop, state machine, audio, protocols, display, MCP server |
| `main/boards/guaup/guachat/` | The board: pins, initialisation, touch, haptics, fuel gauge, expander |
| `main/boards/common/` | Board interface and shared helpers (Wi-Fi board, buttons, backlight, BluFi) |
| `main/assets/` | Prompt audio and per-language fonts and strings |
| `partitions/` | Partition tables; this board uses `partitions/16m.csv` |
| `scripts/` | `build.py` entry point, language and asset generation, `audio_debug_server.py`, host-side tests |
| `docs/` | Protocol specifications and developer guides |
| `idf.cmd`, `idf.ps1` | Windows wrappers that activate ESP-IDF and forward to `idf.py` |
| `build/`, `releases/`, `managed_components/` | Generated: build output, release zips, resolved components |

## Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| Boot loop, `TG0WDT_SYS_RST` after flashing | QIO header on a DIO bootloader — set `CONFIG_ESPTOOLPY_FLASHMODE_DIO=y` in `config.json` and rebuild |
| `undefined reference` after adding a board `.cc` | `file(GLOB)` does not notice new files — run `idf.py reconfigure` |
| Blank display, `Failed to allocate priv TX buffer` | Full-frame PSRAM buffers need `psram_dma_direct = 1`; already set in this tree (see board README) |
| Chinese text garbled in the serial monitor | The log is valid UTF-8; set the terminal to UTF-8 |
| Stuck in the config hotspot after reflashing | A merged image at `0x0` may have cleared NVS, so credentials are gone — re-provision, or flash via `build/flash_args` next time |
| `W SsidManager: NVS namespace wifi doesn't exist` | First boot on a fresh NVS; expected, provision the device |
| `W EmojiCollection: Emoji not found: robot_2` | Cosmetic: the startup expression is not in the bundled emoji font |

## Languages

The firmware ships `zh-CN` and `en-US`. `en-US` also serves as the fallback for any
prompt missing from the selected locale, so both directories are required.

## Documentation

- [Board README](main/boards/guaup/guachat/README.md) — hardware notes, measured behaviour, open work
- [Custom Board Guide](docs/custom-board.md) — how a board implementation is structured
- [WebSocket Protocol](docs/websocket.md) and [MQTT + UDP Protocol](docs/mqtt-udp.md)
- [MCP Protocol Flow](docs/mcp-protocol.md) and [MCP IoT Control Usage](docs/mcp-usage.md)
- [Dynamic Text Glyph Push](docs/glyph-push.md)
- [BluFi Provisioning](docs/blufi.md) and [Asynchronous Voice Notifications](docs/notify.md)
- [Code Style Guide](docs/code_style.md)
- [SUPPORT.md](.github/SUPPORT.md) — what to include in a report, and what never to post

Agent-facing contribution rules live in [`AGENTS.md`](AGENTS.md).

## Status

Firmware version 2.5.0. Verified on real hardware: boots without a reset loop,
renders the panel correctly at ~31 ms per full frame, plays prompts, provisions
over Wi-Fi, pairs with an agent, and holds a voice conversation. The FT6336U
initialises and registers an LVGL pointer device; haptics and the fuel gauge work
on the physical board.

It is working firmware, not production-grade. There is no soak testing, no Wi-Fi
reconnect testing, no low-battery behaviour test, and no OTA server registered for
the `guachat` board id.

Because that id is unregistered, the firmware refuses every upgrade the OTA server
might offer: `CONFIG_DISABLE_OTA_UPGRADE` (default on) logs and ignores a
`firmware` block, and the `self.upgrade_firmware` MCP tool is not registered. The
activation check-in still runs, so xiaozhi.me binding is unaffected. Firmware
reaches this device over USB only. Turn the switch off only once the board id has
an endpoint serving images built for this hardware.

The official check-in endpoint does return a `firmware` block for this board id —
it is simply not newer than 2.5.0 today. The switch is what stands between this
hardware and a firmware built for a different board, so keep it on unless you know
what the endpoint is serving.

Other on-board hardware (GPS, LoRa, IMU, barometer) works but is not yet exposed as
MCP tools; the magnetometer is not fitted on this PCB. Touch beyond tap and
drag-to-scroll, real settings UI, and upstreaming the three generic build fixes
are the other open
items — see the [board README](main/boards/guaup/guachat/README.md#remaining-work).

## License

MIT — see [`LICENSE`](LICENSE). Upstream attribution is in [`NOTICE`](NOTICE).
