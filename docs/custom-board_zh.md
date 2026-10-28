# 自定义开发板指南

本仓库只包含一个板级实现：GUACHAT 2G4（`main/boards/guaup/guachat`）。本文说明
如何新增第二个。核心代码只依赖
[`main/boards/common/board.h`](../main/boards/common/board.h) 中的 `Board`
接口，不会引用具体的板级类或某个板的 `config.h`，因此新增一块板就是新建一个目录，
并在选择链的每一环各加一处配置。

> **CI 注意**：`.github/workflows/build.yml` 的 prepare 任务断言 `--list-boards`
> 只返回一个板卡 `guaup/guachat`。新增板卡时要在同一次改动里放宽这条断言，否则 CI
> 在还没开始编译时就失败。

## 重要

> **警告**：如果要支持的硬件 IO 与现有板不同，绝不要修改现有板的引脚。请新增一个
> 唯一命名的板或发布变体。板身份会影响 OTA 兼容性：已经发货的设备可能收到按其板型
> 构建的固件，覆盖现有板的配置可能把该硬件无法承受的引脚映射推给现场设备。

如果板 id 没有在设备所使用的服务端注册，请保持 `CONFIG_DISABLE_OTA_UPGRADE` 开启：
检查请求照常返回激活码与服务器地址，但服务端下发的固件只会被记录并忽略，而不会被
刷到并非为它构建的硬件上。

板的选择是一条耦合链，新板必须出现在每一环，否则会构建失败或悄悄选中错误的目录：

```
config.json  ->  scripts/build.py  ->  main/Kconfig.projbuild
             ->  main/CMakeLists.txt  ->  板级源码 + config.h
```

## 目录结构

```
main/boards/<manufacturer>/<board>/
├── <board>_board.cc   板级类、初始化、DECLARE_BOARD
├── config.h           引脚与板级常量
├── config.json        上报的 type、目标芯片、构建变体
├── README.md          硬件说明、实测数据、注意事项
└── *.cc               可选的额外驱动，由文件 GLOB 自动纳入
```

板也可以平铺在 `main/boards/<board>/`。厂商名有意义时使用
`main/boards/<manufacturer>/<board>/`；GUACHAT 端口使用 `guaup/guachat`，
`board.manufacturer` 上报为 `guaup`。

## 步骤

### 1. 创建板目录

```bash
mkdir -p main/boards/my-vendor/my-board
```

目录名使用小写加连字符。该名称会成为固件上报的板型，请在首次发布前定好并保持稳定。

### 2. 编写 `config.h`

所有硬件常量都放在这里。GUACHAT 的
[`config.h`](../main/boards/guaup/guachat/config.h) 是完整示例，最小版本如下：

```c
#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// 音频：裸 I2S 直连外部功放，无 codec 芯片
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

// 按键
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define BOOT_BUTTON_ACTIVE_HIGH false

// 显示
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

未连接的引脚写 `GPIO_NUM_NC`：硬接的片选、未使用的 MCLK、或由 IO 扩展芯片驱动的
复位脚。

### 3. 编写 `config.json`

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

**字段说明**：

- `manufacturer`：可选厂商名，上报为 `board.manufacturer`。
- `type`：对兼容性敏感的板型身份。
- `target`：真实芯片（`esp32s3`、`esp32p4`、`esp32c3` 等），决定目标默认值，
  也决定是否需要清空构建目录。
- `builds[]`：每个固件变体一项。`name` 是 OTA 上报的变体名，发布后必须稳定。
- `sdkconfig_append`：在项目与目标默认值之后合并的变体覆盖项。只写真正不同的
  值——重复项目默认值会被 `scripts/tests/test_build.py` 判为错误。

`manufacturer`、`type`、`name` 只能包含小写字母、数字、点和连字符
（`^[a-z0-9.-]+$`）；CMake 会校验，不符合时直接终止构建。

本仓库只有一张分区表 [`partitions/16m.csv`](../partitions/16m.csv)：16 MB flash、
两个 4 MB OTA 分区、一个 8 MB `assets` 分区。16 MB 与这张 CSV 已经是项目默认值
（`sdkconfig.defaults`），在这里重复写是最常见的错误。flash 大小不同的板需要先在
`partitions/` 下新增自己的 CSV。

不要在 `config.json` 里选择语言或唤醒词。它们属于用户构建选项，必须能从
`menuconfig`、命令行以及任何自动化构建中设置，因此 `scripts/build.py` 通过
`--language` / `--wake-word` 控制。

### 4. 实现板级类

继承 `WifiBoard`，它已实现 `GetNetwork()`、`StartNetwork()`、
`SetPowerSaveLevel()` 以及板型/状态 JSON。直接继承 `Board` 则必须自己实现这些纯
虚函数：

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

其余接口——`GetDisplay()`、`GetBacklight()`、`GetLed()`、`GetCamera()`、
`GetBatteryLevel()`、`GetTemperature()`——都有安全默认值（空指针或 `false`）。
显示、背光、LED、摄像头、电池都是可选能力：没有屏幕的板也必须能编译和启动。

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
        // 裸 I2S 全双工：DOUT 接功放，DIN 接 I2S 麦克风
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

`DECLARE_BOARD` 在所有参与编译的板级源码中必须恰好出现一次；它导出应用调用的唯一
板工厂。一个构建里出现两处注册是设计上的链接错误。

示例里 BOOT 单击映射到 `ToggleChatState()`，这是上游默认行为。本仓库的板卡对同一个
键另有安排：单击息屏/亮屏（只关背光），`kDeviceStateStarting` 状态下长按进入配网，
唤醒小智时自动亮屏。按键行为属于板级策略，按硬件需要选择。

### 5. 接入构建系统

#### Kconfig 条目

在 [`main/Kconfig.projbuild`](../main/Kconfig.projbuild) 的 `choice BOARD_TYPE`
里新增符号：

```kconfig
choice BOARD_TYPE
    prompt "Target Board"
    default BOARD_TYPE_GUAUP_GUACHAT

    # ... 现有条目 ...

    config BOARD_TYPE_MY_BOARD
        bool "My Board (ESP32-S3)"
        depends on IDF_TARGET_ESP32S3
endchoice
```

标识符为大写、下划线分隔。`depends on` 把条目的可见性限制到对应芯片；`depends on`
引用一个未定义的符号会让该条目永久不可见，而不只是被禁用。

#### CMake 分支

在 [`main/CMakeLists.txt`](../main/CMakeLists.txt) 中扩展板型链：

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

`BOARD_DIR` 是相对 `main/boards/` 的路径。源码通过该目录的文件 GLOB 收集，因此
给板目录新增 `.cc` 后需要先执行 `idf.py reconfigure`。

#### 字体与 emoji

字体来自 `78/xiaozhi-fonts` 组件。文本字体名由 CMake 正则
`^font_noto_sans_basic_<size>_<bpp>$` 校验，不符合时构建直接终止。按屏幕尺寸选择：

| 屏幕 | 文本字体 | 图标字体 |
| --- | --- | --- |
| 无屏或 128x64 OLED | 项目默认 `font_noto_sans_basic_14_1` | `font_material_symbols_14_1` |
| 约 240x240 | `font_noto_sans_basic_16_4` | `font_material_symbols_16_4` |
| 240x320 至 320x480 | `font_noto_sans_basic_20_4` | `font_material_symbols_20_4` |
| 480x320 及以上 | `font_noto_sans_basic_30_4` | `font_material_symbols_30_4` |

组件只提供四档正文字号——14_1、16_4、20_4、30_4——以及对应的图标字体，另有
`font_material_symbols_30_1` 与 `font_noto_emoji_30_1`。使用其他字号会链接失败。

`BUILTIN_TEXT_FONT` 必须始终设为合法名称——即使板没有屏幕 CMake 也会校验它，
项目级默认值为 `font_noto_sans_basic_14_1`。

emoji 集合为 `noto-color-emoji_32`（32 px）、`noto-color-emoji_64` 与更大屏幕用的
`noto-color-emoji_128`。本仓库的板卡改用 `otto-gif`——来自
`txp666/otto-emoji-gif-component` 的 240x240 动画奥托脸套装：待命时屏幕正中只有
这个表情，静态 32 px 集合放大到该尺寸会发糊。
`DEFAULT_EMOJI_COLLECTION` 可选，没有屏幕的板不要设置。

#### 组件清单

自定义驱动头文件必须来自 [`main/idf_component.yml`](../main/idf_component.yml)
中声明的组件。清单只列出固件直接包含的组件；其余组件通过它们的 manifest 间接引入。
例如使用 ST7789 而不是仓库自带的 ST7796 时新增：

```yaml
  espressif/esp_lcd_st7789: ~1.2.0
```

#### 分区表

flash 大小或布局不同的板需要在 `partitions/` 下新增 CSV，并在该变体的
`sdkconfig_append` 中选中：

```json
"CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y",
"CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions/8m.csv\""
```

注意 `assets` 分区仍要放得下：`main/CMakeLists.txt` 会读取分区大小，生成的
`assets.bin` 超出时资产构建失败。

### 6. 构建与烧录

#### 方式 A —— `scripts/build.py`（推荐）

```bash
python scripts/build.py --list-boards
python scripts/build.py my-vendor/my-board --name my-board
```

脚本会：

- 无参数时打印帮助；`--list-boards` 列出所有板与变体，`--list-boards --json`
  还包含每个变体的构建选项；
- 板有多个变体时提示选择，非交互环境用 `--name <variant>` 指定；
- 从 `config.json` 读取 `target`，仅在目标不同时清空构建目录，然后用一次
  `idf.py reconfigure` 应用目标、板名、项目默认值与变体的 `sdkconfig_append`；
- 产出 `build/merged-binary.bin`（从 `0x0` 烧录）；加 `--zip` 还会生成
  `releases/v<version>_<name>.zip`。

语言与唤醒词属于用户选项，不属于板选项：

```bash
python scripts/build.py my-vendor/my-board --name my-board \
  --language en-US --wake-word nihaoxiaozhi

python scripts/build.py --list-languages
python scripts/build.py --list-wake-words
```

`--language` 接受 `main/assets/locales/` 下的目录名（默认 `zh-CN`，另含
`en-US`）。`--wake-word` 接受 `--list-wake-words` 报告的模型名、`nihaoxiaozhi`
或 `disabled`。唤醒词列表读取自已解析的 ESP-SR 组件，如果
`managed_components/` 为空，先执行一次 `idf.py reconfigure`。

#### 方式 B —— 直接使用 `idf.py`

```bash
idf.py set-target esp32s3
idf.py menuconfig     # Xiaozhi Assistant -> Target Board
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

切换目标时报 `idf.py set-target` 并随后 `idf.py fullclean`；残留其他芯片的构建
目录会产生难以理解的链接错误。

### 7. 编写板级 README

记录原理图上没有的信息：实测背光上限、功放增益与削顶阈值、探测后确认不存在的
外设、上电自锁时序，以及其他维护者否则只能用示波器重新发现的东西。GUACHAT 的
[板级 README](../main/boards/guaup/guachat/README.md) 就是范本。

## 厂商子目录

同一厂商有多块板时放在 `main/boards/<manufacturer>/<board>/`，并在各自的
`config.json` 里设置 `"manufacturer"`。固件会在 `GetBoardJson()` 中与
`board.type`、`board.name` 一起上报；未设置该字段的板上报空字符串。

`BOARD_DIR` 填写相对 `main/boards/` 的完整路径：

```cmake
elseif(CONFIG_BOARD_TYPE_MY_VENDOR_MY_BOARD)
    set(BOARD_DIR "my-vendor/my-board")
```

若 `config.json` 缺失或没有顶层 `type`，CMake 会退化为把 `BOARD_DIR` 中的 `/`
替换为 `-`，这虽然有效但并不好看——请显式声明。

## 通用板级组件

`main/boards/common/` 是面向板级的公共层：

| 文件 | 内容 |
| --- | --- |
| `board.h` | `Board` 接口、`DECLARE_BOARD`、网络事件、`PowerSaveLevel` |
| `wifi_board.h` | `WifiBoard` 基类，含 `EnterWifiConfigMode()` |
| `backlight.h` | `Backlight` 接口与 `PwmBacklight(pin, invert, freq_hz)` |
| `button.h` | `Button`，支持单击、长按、多击回调 |
| `blufi.h` | BluFi 配网通道，仅在 `CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING` 时编译 |
| `camera.h` | 可选摄像头的 `Camera` 接口 |

该目录之外的可复用部分：

- **音频** —— `main/audio/codecs/no_audio_codec.h` 提供 `NoAudioCodecDuplex`、
  `NoAudioCodecSimplex`、`NoAudioCodecSimplexPdm`，用于没有 codec 芯片的裸 I2S
  链路。使用 ES8311 之类的板需要在自己的板目录下实现 `AudioCodec` 子类；公共代码
  刻意不再内置任何外部 codec 驱动。
- **显示** —— `main/display/lcd_display.h` 提供 `SpiLcdDisplay` 与
  `RgbLcdDisplay`；屏幕驱动本身来自 ESP-IDF 组件。
- **MCP 工具** —— `main/mcp_server.h` 与 [MCP IoT 用法](./mcp-usage_zh.md)。
  板级工具在板级类里注册，不要写进核心代码。

## 板级类层次

```
Board
└── WifiBoard
```

`WifiBoard` 是唯一随仓库提供的基类。使用蜂窝模块或以太网的板自行实现
`NetworkInterface` 并直接覆盖 `GetNetwork()`、`StartNetwork()`、
`GetNetworkStateIcon()` 与 `SetPowerSaveLevel()`。

## 提示

1. **复制最接近的板**，改掉身份信息，删掉硬件没有的部分。本仓库只提供 GUACHAT
   实现，所以是"删减"而不是"假设"。
2. **逐步点亮** —— 启动日志、背光、显示、音频，最后才是完整链路。
3. **引脚对照原理图核对两遍**，包括 strapping 引脚和复位时为输入的引脚。
4. **验证可选能力路径** —— 把显示、触摸、电池代码关掉后再编译启动一次，就像
   无屏板那样。
5. **在软件里限制背光**。标称 160 mA 的屏幕承受不了 100% LEDC 占空比；GUACHAT
   用 `GuachatBacklight` 包住 `PwmBacklight` 做了限幅，见 `guachat_board.cc`。

## 故障排查

| 现象 | 可能原因 |
| --- | --- |
| 画面镜像、颜色错乱或有偏移 | `DISPLAY_MIRROR_*`、`DISPLAY_SWAP_XY`、`rgb_ele_order`、`esp_lcd_panel_invert_color`、`DISPLAY_OFFSET_*` |
| 无声 | I2S BCLK/WS/DOUT、功放使能脚、采样率不匹配 |
| 麦克风无输入 | I2S DIN 引脚、时钟源，或选错了 `NoAudioCodec*` 变体 |
| 录音断续或丢帧 | I2S DMA 大小与 CPU 负载；检查同时占用 SPI/I2S 的其他模块 |
| 上电自锁导致反复重启 | 自锁脚在复位时被拉低；初始化第一步就要拉高 |
| 无法连接 Wi-Fi | 凭据、配网方式、天线 |
| 构建选中了错误的板 | 缺少 Kconfig 条目或 CMake 分支；执行 `idf.py reconfigure` |
| 新增的 `.cc` 没被编译 | 板级源码 GLOB 不是 `CONFIGURE_DEPENDS`；执行 `idf.py reconfigure` |

## 参考

- [ESP-IDF 文档](https://docs.espressif.com/projects/esp-idf/)
- [LVGL 文档](https://docs.lvgl.io/)
- [ESP-SR 文档](https://github.com/espressif/esp-sr)
- [随仓库发布的板级 README](../main/boards/guaup/guachat/README.md)
