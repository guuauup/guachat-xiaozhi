# Support

This repository contains firmware source for one board: the **GUACHAT 2G4**
(`main/boards/guaup/guachat`). It does not operate any account, cloud, or
activation service.

## Firmware

Build, flashing, runtime, and code questions belong in this repository's issue
tracker. Pick the closest template and include the complete output of the
commands you ran. A reproducer that starts from `python scripts/build.py
guaup/guachat --name guachat` is far easier to act on than a description.

Before reporting a build or flash problem, confirm the toolchain:

```bash
source /path/to/esp-idf/export.sh   # or export.bat on Windows
idf.py --version                    # v6.0.1 or newer
```

## Hardware

Pin maps, the schematic, mechanical drawings, and warranty or replacement
requests are handled by the hardware source of truth:
[gitee.com/guaup/guachat](https://gitee.com/guaup/guachat).

The pin map and the measured panel, audio, and power behaviour are also written
down in [the board README](../main/boards/guaup/guachat/README.md).

## Accounts and Cloud Services

Activation, agent configuration, voiceprint, voice cloning, and server-side
history are properties of the backend the device is pointed at, not of this
firmware. Ask the operator of that backend. The firmware's transport contract is
documented in [docs/websocket.md](../docs/websocket.md) and
[docs/mqtt-udp.md](../docs/mqtt-udp.md) if you are writing your own server.

## Privacy

Never post device identifiers in a public issue, discussion, screenshot, or log
file. That includes the device MAC address, board UUID, activation code, Wi-Fi
credentials, and any server token or URL private to you. Redact them from logs
before attaching them; the startup log in particular prints the Wi-Fi MAC
address (`wifi:mode : sta (aa:bb:cc:dd:ee:ff)`).
