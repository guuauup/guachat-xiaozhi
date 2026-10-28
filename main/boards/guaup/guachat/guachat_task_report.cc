#include "guachat_task_report.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>

#define TAG "TaskReport"

namespace {

// Let Wi-Fi connect, assets load and the UI settle before sampling. Sampling
// during boot measures the startup storm, not steady state.
constexpr int kWarmupMs = 15000;
constexpr int kSampleWindowMs = 5000;
constexpr size_t kMaxTasks = 40;

struct Sample {
    TaskHandle_t handle;
    char name[configMAX_TASK_NAME_LEN];
    UBaseType_t priority;
    uint32_t runtime;
    uint32_t stack_watermark;
};

size_t TakeSample(Sample* out, size_t capacity, uint32_t* total_out) {
    auto* status = static_cast<TaskStatus_t*>(
        heap_caps_malloc(capacity * sizeof(TaskStatus_t), MALLOC_CAP_INTERNAL));
    if (status == nullptr) {
        return 0;
    }

    uint32_t total = 0;
    const size_t count = uxTaskGetSystemState(status, capacity, &total);
    for (size_t i = 0; i < count; ++i) {
        out[i].handle = status[i].xHandle;
        strncpy(out[i].name, status[i].pcTaskName, sizeof(out[i].name) - 1);
        out[i].name[sizeof(out[i].name) - 1] = '\0';
        out[i].priority = status[i].uxCurrentPriority;
        out[i].runtime = status[i].ulRunTimeCounter;
        out[i].stack_watermark = status[i].usStackHighWaterMark;
    }
    heap_caps_free(status);
    *total_out = total;
    return count;
}

void TaskReportTask(void* arg) {
    vTaskDelay(pdMS_TO_TICKS(kWarmupMs));

    auto* first =
        static_cast<Sample*>(heap_caps_malloc(kMaxTasks * sizeof(Sample), MALLOC_CAP_INTERNAL));
    auto* second =
        static_cast<Sample*>(heap_caps_malloc(kMaxTasks * sizeof(Sample), MALLOC_CAP_INTERNAL));
    if (first == nullptr || second == nullptr) {
        ESP_LOGE(TAG, "no memory for samples");
        heap_caps_free(first);
        heap_caps_free(second);
        vTaskDelete(nullptr);
        return;
    }

    uint32_t total_first = 0;
    uint32_t total_second = 0;
    const size_t count_first = TakeSample(first, kMaxTasks, &total_first);
    vTaskDelay(pdMS_TO_TICKS(kSampleWindowMs));
    const size_t count_second = TakeSample(second, kMaxTasks, &total_second);

    if (count_first > 0 && count_second > 0 && total_second > total_first) {
        const double scale = 100.0 / static_cast<double>(total_second - total_first);
        std::printf("TASKS:BEGIN window=%ums tasks=%u\n", kSampleWindowMs,
                    static_cast<unsigned>(count_second));
        std::printf("TASKS: %-16s %4s %7s %8s\n", "name", "pri", "cpu%", "stack");

        for (size_t i = 0; i < count_second; ++i) {
            // Match by handle: the two enumerations cover the same set, but the
            // ordering between them is not guaranteed.
            uint32_t prior = second[i].runtime;
            for (size_t j = 0; j < count_first; ++j) {
                if (first[j].handle == second[i].handle) {
                    prior = first[j].runtime;
                    break;
                }
            }
            const double cpu_pct = static_cast<double>(second[i].runtime - prior) * scale;
            std::printf("TASKS: %-16s %4u %7.1f %8u\n", second[i].name,
                        static_cast<unsigned>(second[i].priority), cpu_pct,
                        static_cast<unsigned>(second[i].stack_watermark));
        }
        std::printf("TASKS:END\n");
    } else {
        ESP_LOGE(TAG, "task enumeration failed");
    }

    ESP_LOGI(TAG, "internal free=%u largest=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));

    heap_caps_free(first);
    heap_caps_free(second);
    vTaskDelete(nullptr);
}

}  // namespace

void GuachatScheduleTaskReport() {
    // Pinned to core 1 at the lowest priority so the sampling task itself does
    // not perturb what it is measuring.
    xTaskCreatePinnedToCore(TaskReportTask, "task_report", 6144, nullptr, 1, nullptr, 1);
}
