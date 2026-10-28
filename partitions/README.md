# Partition Tables

The board ships with 16 MB of flash, so this repository carries one table,
`16m.csv`. It is selected through `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME` in
`sdkconfig.defaults`.

## `16m.csv`

| Partition | Type | SubType | Offset | Size |
| --- | --- | --- | --- | --- |
| `nvs` | data | nvs | `0x9000` | 16 KB |
| `otadata` | data | ota | `0xd000` | 8 KB |
| `phy_init` | data | phy | `0xf000` | 4 KB |
| `ota_0` | app | ota_0 | `0x20000` | 0x3f0000 (4,128,768 B) |
| `ota_1` | app | ota_1 | `0x410000` | 0x3f0000 |
| `assets` | data | spiffs | `0x800000` | 8 MB |

Two application slots of equal size give the device a working OTA rollback
path: an update is written to the inactive slot and `otadata` decides which one
boots.

`assets` holds content that is not compiled into the firmware — wake-word
models, fonts, prompt audio, images and language configuration. It uses the
`spiffs` subtype so it mounts as a filesystem, and the data can be replaced over
the network instead of reflashing. `idf.py flash` writes it from
`build/generated_assets.bin`.

## Adding another layout

A board with a different flash size needs its own CSV here, plus a matching
`CONFIG_ESPTOOLPY_FLASHSIZE_*` and `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME` in
that board's `sdkconfig_append`. Keep every partition 4 KB aligned and keep the
application partitions large enough for `xiaozhi.bin` with headroom for growth
(the build prints the remaining space at the end of every link).
