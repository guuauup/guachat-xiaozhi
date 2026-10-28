# XiaoZhi for GUACHAT 2G4（ESP32-S3）

[English](README.md) · 中文

一台握在手里的语音助手：离线唤醒词、语音识别、大模型、语音合成，用 320x480
触摸屏以聊天形式呈现。模型跑在服务器上，本固件负责设备端——音频输入输出、唤醒、
界面、联网，以及模型可以调用的设备工具。

本仓库是 [XiaoZhi](https://github.com/78/xiaozhi-esp32) 的第三方板级移植，以
[gitee.com/guaup/guachat](https://gitee.com/guaup/guachat) 为硬件依据，只包含一个
板级实现：[`main/boards/guaup/guachat`](main/boards/guaup/guachat)。上游快照与
署名见 [`NOTICE`](NOTICE)。

| 从这里开始 | |
| --- | --- |
| 硬件、引脚、实测数据 | [板级说明](main/boards/guaup/guachat/README.md) |
| 移植到别的板子 | [自定义板卡指南](docs/custom-board_zh.md) |
| 遇到问题 | [SUPPORT.md](.github/SUPPORT.md)（英文） |

## 能做什么

- **离线唤醒词**：ESP-SR WakeNet，本板默认 `wn9_nihaoxiaozhi_tts`（“你好小智”），
  持续运行；设备说话时仍然生效，因此可以直接用唤醒词打断播报。
- **对话**：流式 ASR → LLM → TTS，或实时语音大模型；音频以 Opus 编码，
  走 WebSocket 或 MQTT + UDP。
- **聊天界面**：`CONFIG_USE_WECHAT_MESSAGE_STYLE` 打开微信风格气泡——用户消息
  右侧绿色、助手左侧灰色、系统提示居中，保留 20 条，自动滚到最新。
- **动态待命表情**：没有对话时，当前表情以 240 px 动态 GIF（Otto 机器人脸）在屏幕
  正中播放，约 12.5 fps；一旦有消息出现立即隐藏，聊天清空后再回来，不会挡住气泡。
- **默认深色主题**：黑底、白色状态栏文字与图标，表情的黑色面板与背景融为一体；
  `self.screen.set_theme` 仍可切回浅色。
- **状态栏**：MAX17055 电量计驱动电池图标与剩余电量百分比（含充电闪电），另有音量与
  网络状态。
- **触摸**：上下滑动滚动对话气泡，单击切换聊天。界面本身没有可点的控件，面板只是
  状态显示。
- **按键**：音量只由 TCA9535 上的音量加减键控制。BOOT 键短按息屏/亮屏（只关背光），
  开机阶段长按直接进入配网。息屏时唤醒小智（唤醒词、点屏或服务器推送）会自动亮屏，
  待命状态 5 分钟没有活动则自动息屏。
- **MCP 设备工具**：音量、屏幕亮度与主题、屏幕截图、系统信息、重启、资源下载地址，
  模型可通过 `self.*` 工具调用。
- **配网**：开热点 AP，用手机打开 `192.168.4.1` 填 Wi-Fi；也支持 BluFi。
- **触感**：DRV2605L 在开机和按音量键时触发，复用刷机前固件存在 NVS 里的 LRA 校准。
- **语言**：构建时选择 `zh-CN`（默认）或 `en-US`。

**不支持**

- **全双工“边说边听”**：在任何可用音量下麦克风都会削顶，这块 PCB 上回声消除
  无从做起。播报仍可用点屏或唤醒词打断。实测数据见
  [板级说明](main/boards/guaup/guachat/README.md#why-device-side-aec-is-not-implemented)。
- **OTA 固件升级**：本板 id 未在任何 OTA 服务器注册，check-in 返回的固件会被
  记录后忽略，`self.upgrade_firmware` 工具也不会注册。固件只通过 USB 烧录，
  详见 [当前状态](#当前状态)。

## 硬件

| 项目 | 规格 |
| --- | --- |
| 主控 | ESP32-S3-WROOM-1U N16R8 —— 16 MB flash，8 MB OPI PSRAM |
| 屏幕 | ST7796，320x480 竖屏，SPI 80 MHz，BGR + 反色 |
| 触摸 | FT6336U，I2C1，0x38 |
| 触感 | DRV2605L LRA，I2C0，0x5A |
| 音频输出 | MAX98357，I2S0，功放使能 GPIO42 |
| 音频输入 | I2S 麦克风，I2S0 RX |
| 电量计 | MAX17055，I2C0，0x36 |
| IO 扩展 | TCA9535，I2C0，0x20 —— 屏幕复位、触摸复位、电源锁存、音量键 |
| 背光 | GPIO46，LEDC 驱动，限流到面板 160 mA 上限 |
| USB | ESP32-S3 原生 USB-Serial/JTAG（无串口桥） |

引脚表、电源锁存初始化顺序、屏幕与音频的实测数据都在
[板级说明](main/boards/guaup/guachat/README.md) 里。

## 快速开始

需要 ESP-IDF **v6.0.1 及以上**（推荐 **v6.1**，不支持 5.x），以及一块 **16 MB flash
的 ESP32-S3**。

```sh
# 1. 激活 ESP-IDF 环境（Windows 也可用 idf.cmd，见下文）
. $IDF_PATH/export.sh

# 2. 编译本仓库唯一的变体
python scripts/build.py guaup/guachat --name guachat

# 3. USB 烧录
idf.py -p COM9 flash
```

4. 屏幕会显示类似 `Xiaozhi-487D` 的热点，手机连上后打开 `http://192.168.4.1`
   填写 Wi-Fi（也可以用 BluFi）。
5. 联网后屏幕显示激活二维码，扫码并在 xiaozhi.me 绑定设备。这一步把设备与服务器
   配对；携带激活码的 check-in 同时携带服务器地址，所以固件升级被禁用后它仍然保留。

## 构建与烧录

```sh
python scripts/build.py --list-boards       # 板卡与变体
python scripts/build.py guaup/guachat --name guachat
python scripts/build.py guaup/guachat --name guachat --language en-US
python scripts/build.py guaup/guachat --name guachat --wake-word disabled
```

产物是 `build/merged-binary.bin`（写 `0x0`）和分段烧录用的 `build/flash_args`；
app 分区 4,128,768 B（0x3f0000），约剩三分之一。`--zip` 会额外生成
`releases/v<version>_guachat.zip`。

Windows 下仓库自带 `idf.cmd` / `idf.ps1`，会自动找到 ESP-IDF 6.x（`IDF_PATH`、
VS Code 扩展配置或 `esp_idf.json`），激活环境后转发给 `idf.py`：
`idf.cmd build`、`idf.cmd -p COM9 flash`、`idf.cmd reconfigure`。

**已配网的设备优先用分段烧录**：在 `0x0` 写入合并镜像会覆盖空白区域，可能清掉
NVS，也就是 Wi-Fi 凭据和配对信息。用 `build/flash_args` 分段写入：

```sh
python -m esptool --chip esp32s3 -b 460800 --before default-reset --after hard-reset \
  write-flash "@build/flash_args"
```

CI 在每次推送到 `main` 时用 `espressif/idf:v6.1` 容器构建变体并上传
`build/merged-binary.bin`；PR 只在改动影响变体时构建。CI 同时断言仓库只有一个
板卡（`guaup/guachat`），新增板卡需要一起改掉那条断言。

改了板级目录要注意两点：

- 板级源码用 `file(GLOB)` 收集（`main/CMakeLists.txt` 也会被 CMake script 模式
  引用，那里不接受 `CONFIGURE_DEPENDS`）。**新增 `.cc` 之后必须
  `idf.py reconfigure`**，否则文件被悄悄漏掉，链接时报未定义符号。
- `main/boards/guaup/guachat/config.json` 里的 `type`（当前 `guachat`）就是 OTA
  身份，不要复用别的板子的标识。

如果刷完进了 `TG0WDT_SYS_RST` 重启循环，在 `config.json` 里加
`CONFIG_ESPTOOLPY_FLASHMODE_DIO=y` 后重编。

本移植与 guachat 原厂 PlatformIO/Arduino 固件（`guachat_dev`）不共用 flash 布局和
NVS，二者只能二选一。

## 代码结构

| 路径 | 作用 |
| --- | --- |
| `main/application.cc` | 启动流程、主事件循环、协议生命周期、语音状态 |
| `main/device_state_machine.cc` | 合法的运行状态迁移（`starting` → `activating` → `idle` …） |
| `main/protocols/` | 与传输无关的 `Protocol` 接口，加 WebSocket 与 MQTT/UDP 实现 |
| `main/audio/` | 编解码、唤醒词、音频服务任务、AEC 模式、队列 |
| `main/display/`、`main/led/` | LVGL 界面与 LED 实现 |
| `main/mcp_server.cc` | 设备端 MCP 工具与分发 |
| `main/boards/` | `Board` 接口、公共辅助、本板实现 |

核心代码只依赖 `Board` 接口。运行状态只能通过 `Application::SetDeviceState()` 修改；
不在主任务里的回调要用 `Application::Schedule()` 或事件位把工作交回主任务，避免
阻塞音频路径和事件循环。

## 仓库结构

| 路径 | 内容 |
| --- | --- |
| `main/` | 固件源码：应用循环、状态机、音频、协议、显示、MCP 服务 |
| `main/boards/guaup/guachat/` | 本板实现：引脚、初始化、触摸、触感、电量计、IO 扩展 |
| `main/boards/common/` | `Board` 接口与公共辅助（Wi-Fi 板、按键、背光、BluFi） |
| `main/assets/` | 提示音、按语言划分的字体与字符串 |
| `partitions/` | 分区表；本板使用 `partitions/16m.csv` |
| `scripts/` | `build.py` 入口、语言与资源生成、`audio_debug_server.py`、主机侧测试 |
| `docs/` | 协议说明与开发指南 |
| `idf.cmd`、`idf.ps1` | Windows 下激活 ESP-IDF 并转发给 `idf.py` 的包装脚本 |
| `build/`、`releases/`、`managed_components/` | 生成物：构建输出、发布包、已解析组件 |

## 语言

固件包含 `zh-CN` 与 `en-US`。`en-US` 同时作为缺失词条的兜底，所以两个目录都必须
存在；用 `--language` 在构建时选择。

## 常见问题

| 现象 | 原因 / 处理 |
| --- | --- |
| 刷机后不断重启，日志有 `TG0WDT_SYS_RST` | DIO bootloader 被强加了 QIO 头 —— 在 `config.json` 设 `CONFIG_ESPTOOLPY_FLASHMODE_DIO=y` 重编 |
| 新增板级 `.cc` 后报 `undefined reference` | `file(GLOB)` 不感知新文件 —— 执行 `idf.py reconfigure` |
| 屏幕黑屏，日志有 `Failed to allocate priv TX buffer` | 全屏 PSRAM 缓冲需要 `psram_dma_direct = 1`，本仓库已设置 |
| 串口里中文乱码 | 日志本身是合法 UTF-8，把终端编码改成 UTF-8 |
| 重新烧录后卡在配网热点 | 合并镜像可能清掉了 NVS，Wi-Fi 凭据没了 —— 重新配网，下次改用 `build/flash_args` |
| `W SsidManager: NVS namespace wifi doesn't exist` | 全新 NVS 首次启动的正常提示，配网即可 |
| `W EmojiCollection: Emoji not found: robot_2` | 外观问题：启动表情不在内置表情字体里，不影响功能 |

## 当前状态

固件版本 2.5.0。已在真机验证：无重启循环、整屏刷新约 31 ms、提示音正常、Wi-Fi
配网、与智能体配对并完成正常语音对话；FT6336U 正常初始化并注册 LVGL 指针设备，
触感与电量计在真机上工作。

这是能用的固件，但不是量产级：没有长时间稳定性测试、没有断网重连测试、没有低电量
行为测试，`guachat` 这个板卡 id 也没有注册任何 OTA 服务器。

正因如此，固件拒绝服务器可能提供的任何升级：`CONFIG_DISABLE_OTA_UPGRADE`
（默认开启）会把 `firmware` 段记录下来并忽略，`self.upgrade_firmware` 工具不注册；
携带激活码的 check-in 照常执行，不影响 xiaozhi.me 绑定。固件只通过 USB 进入设备。
只有等这个板卡 id 有了提供本硬件镜像的端点，再关掉这个开关。

官方 check-in 端点对这个板卡 id **确实会返回 `firmware` 段**，只是目前版本不高于
2.5.0。开关与否决定了这块硬件会不会被刷上为别家板子构建的固件，除非你清楚服务器在
推什么，否则保持开启。

板上其它硬件（GPS、LoRa、IMU、气压计）可用但尚未作为 MCP 工具暴露，磁力计本 PCB
未贴片。手势之外的触摸交互（屏上按钮、设置页），以及把三处通用构建修复提交上游，
是其余的待办，见[板级说明的 Remaining work](main/boards/guaup/guachat/README.md#remaining-work)。

## 文档

- [板级说明](main/boards/guaup/guachat/README.md) — 硬件细节、实测数据、待办
- [自定义板卡指南](docs/custom-board_zh.md) / [Custom Board Guide](docs/custom-board.md)
- [WebSocket 协议](docs/websocket_zh.md)、[MQTT + UDP 协议](docs/mqtt-udp_zh.md)
- [MCP 协议流程](docs/mcp-protocol_zh.md)、[MCP 物联网控制用法](docs/mcp-usage_zh.md)
- [动态文字字形推送](docs/glyph-push_zh.md)
- [BluFi 配网](docs/blufi_zh.md)、[异步语音通知](docs/notify.md)
- [代码风格](docs/code_style_zh.md)
- [SUPPORT.md](.github/SUPPORT.md) — 报问题时该写什么，以及什么绝对不能贴

给智能体看的贡献规则在 [`AGENTS.md`](AGENTS.md)。

## 许可

MIT，见 [`LICENSE`](LICENSE)。上游署名见 [`NOTICE`](NOTICE)。
