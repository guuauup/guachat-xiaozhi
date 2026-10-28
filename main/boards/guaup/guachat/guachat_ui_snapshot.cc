#include "guachat_ui_snapshot.h"

#include "display.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>

#include <cstdio>
#include <string>

#define TAG "UiSnapshot"

namespace {

// Long enough that boot, Wi-Fi and asset loading are all done and the LVGL
// task has stopped touching the layout.
constexpr int kSettleMs = 12000;

// Lines are kept short so nothing gets dropped when other log output lands in
// the middle of the stream; the reader reassembles by index.
constexpr int kChunkChars = 120;

void PrintBase64(const std::string& jpeg) {
    size_t encoded_len = 0;
    if (mbedtls_base64_encode(nullptr, 0, &encoded_len,
                              reinterpret_cast<const unsigned char*>(jpeg.data()),
                              jpeg.size()) != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        std::printf("SNAP:FAIL encode-size\n");
        return;
    }

    std::string encoded(encoded_len, '\0');
    size_t written = 0;
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(&encoded[0]), encoded_len, &written,
                              reinterpret_cast<const unsigned char*>(jpeg.data()),
                              jpeg.size()) != 0) {
        std::printf("SNAP:FAIL encode\n");
        return;
    }
    encoded.resize(written);

    std::printf("SNAP:BEGIN %u\n", static_cast<unsigned>(encoded.size()));
    unsigned index = 0;
    for (size_t offset = 0; offset < encoded.size(); offset += kChunkChars) {
        const int len = static_cast<int>(
            encoded.size() - offset < static_cast<size_t>(kChunkChars)
                ? encoded.size() - offset
                : static_cast<size_t>(kChunkChars));
        std::printf("SNAP %04u:%.*s\n", index, len, encoded.data() + offset);
        ++index;
    }
    std::printf("SNAP:END %u\n", index);
}

void SnapshotTask(void* arg) {
    auto* display = static_cast<Display*>(arg);
    vTaskDelay(pdMS_TO_TICKS(kSettleMs));

    // Seed a conversation so the capture shows real bubble layout rather than
    // an empty chat area. Mirrors how the roles are used at runtime.
    display->SetChatMessage("system", "已连接到小智");
    vTaskDelay(pdMS_TO_TICKS(150));
    display->SetChatMessage("user", "今天天气怎么样？");
    vTaskDelay(pdMS_TO_TICKS(150));
    display->SetChatMessage("assistant",
                            "今天多云转晴，气温 18 到 26 度，风力不大，适合出门走走。");
    vTaskDelay(pdMS_TO_TICKS(1200));  // let LVGL lay out and flush

    std::string jpeg;
    if (!display->SnapshotToJpeg(jpeg, 90)) {
        std::printf("SNAP:FAIL snapshot\n");
        vTaskDelete(nullptr);
        return;
    }
    std::printf("SNAP:BYTES %u\n", static_cast<unsigned>(jpeg.size()));
    PrintBase64(jpeg);

    vTaskDelete(nullptr);
}

}  // namespace

void GuachatScheduleUiSnapshot(Display* display) {
    if (display == nullptr) {
        return;
    }
    xTaskCreate(SnapshotTask, "ui_snapshot", 8192, display, 2, nullptr);
}
