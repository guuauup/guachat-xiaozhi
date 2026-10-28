#include "guachat_acoustic_probe.h"

#include "audio_codec.h"
#include "config.h"
#include "settings.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define TAG "AcousticProbe"

namespace {

constexpr int kSampleRate = 16000;

// 100 ms linear sweep. Long enough to put real energy into the room, short
// enough that the echo cannot be confused with room reverberation tail.
constexpr int kChirpMs = 100;
constexpr int kChirpSamples = kSampleRate * kChirpMs / 1000;  // 1600

// 900 ms of microphone, so even a pathological delay is still inside the
// window. Anything longer than that is not an echo, it is a reverb tail.
constexpr int kRecordMs = 900;
constexpr int kRecordSamples = kSampleRate * kRecordMs / 1000;  // 14400

// The chirp starts this far into the recording, leaving a quiet lead-in that
// doubles as the noise-floor reference.
constexpr int kPlayDelayMs = 150;

// 500-3000 Hz. Stays under the 4 kHz Nyquist of the decimated search below,
// and is inside the band an 8 ohm 1 W micro speaker actually reproduces.
constexpr double kChirpLowHz = 500.0;
constexpr double kChirpHighHz = 3000.0;

// 91% of full scale: loud enough to expose compression, short of digital
// clipping so any non-linearity we see belongs to the amplifier or speaker.
//
// The amplifier is deliberately NOT driven hard: a 1 W speaker a few
// centimetres from a MEMS microphone saturates it long before the digital
// signal clips, and a clipped microphone measures nothing useful.
// Deliberately quiet (4.6% of full scale). The echo path through this board
// measures roughly +17 dB, and the microphone sits on a ~11000-count DC
// offset, so anything louder clips inside NoAudioCodec::Read() before the
// probe ever sees it - and a clipped capture answers nothing.
// 1.5% of full scale, and it has to be this low. Measured on this board, the
// digital reference reaches the microphone ADC with about 44 dB of gain, so
// anything louder clips inside NoAudioCodec::Read() - at 4.6% of full scale
// the capture already rails. A clipped capture measures nothing, which is why
// the first attempts reported a nonsense +22 dB "gain" and negative correlation.
constexpr int kAmplitude = 50;

constexpr int kChunkSamples = 160;  // 10 ms per read
constexpr int kDecim = 2;
constexpr int kRefDec = kChirpSamples / kDecim;
constexpr int kRecDec = kRecordSamples / kDecim;

constexpr double kTwoPi = 6.283185307179586;
constexpr double kFullScale = 32768.0;

struct Measurement {
    double delay_ms = 0.0;
    double gain_db = 0.0;   // echo gain vs the reference; negative = attenuated
    double echo_dbfs = 0.0;
    double noise_dbfs = 0.0;
    double rho = 0.0;       // normalised correlation, 1.0 = perfect linear echo
};

struct PlayCtx {
    AudioCodec* codec;
    const int16_t* ref;
    volatile bool done;
};

void* AllocSamples(int count) {
    void* p = heap_caps_malloc(count * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == nullptr) {
        p = heap_caps_malloc(count * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return p;
}

void GenerateChirp(int16_t* out, int samples) {
    const double duration = static_cast<double>(samples) / kSampleRate;
    for (int n = 0; n < samples; ++n) {
        const double t = static_cast<double>(n) / kSampleRate;
        // Instantaneous frequency ramps linearly, so the phase is its integral.
        const double phase =
            kTwoPi * (kChirpLowHz * t + (kChirpHighHz - kChirpLowHz) * t * t / (2.0 * duration));
        out[n] = static_cast<int16_t>(kAmplitude * sinf(static_cast<float>(phase)));
    }
}

void PlayTaskEntry(void* arg) {
    auto* ctx = static_cast<PlayCtx*>(arg);
    vTaskDelay(pdMS_TO_TICKS(kPlayDelayMs));
    std::vector<int16_t> chunk(ctx->ref, ctx->ref + kChirpSamples);
    ctx->codec->OutputData(chunk);
    ctx->done = true;
    // Block here rather than self-deleting. The caller holds this task's handle
    // and deletes it after seeing `done`; a self-delete would leave that handle
    // dangling and the next vTaskDelete() faults inside uxListRemove().
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

// Records kRecordSamples from the microphone into `rec`, in 10 ms chunks, while
// the play task fires the chirp. Returns false when the capture path is dead.
bool RecordMicrophone(AudioCodec* codec, int16_t* rec) {
    // Zero first. A read that times out leaves the destination untouched, and
    // without this the buffer keeps whatever the previous pass left there -
    // which silently looks like a measurement instead of a failure.
    std::memset(rec, 0, kRecordSamples * sizeof(int16_t));

    std::vector<int16_t> chunk(kChunkSamples);
    int filled = 0;
    int failures = 0;
    while (filled < kRecordSamples) {
        std::fill(chunk.begin(), chunk.end(), 0);
        if (codec->InputData(chunk)) {
            std::memcpy(rec + filled, chunk.data(), kChunkSamples * sizeof(int16_t));
        } else {
            ++failures;
        }
        filled += kChunkSamples;
    }

    // Remove DC. The digital microphone sits on a large, slowly drifting
    // offset (thousands of counts), which speech recognition never notices
    // because the AFE high-passes the signal - but a correlation is dominated
    // by it, and once the offset plus the signal passes full scale the
    // capture clips and the measurement is worthless.
    int64_t sum = 0;
    for (int i = 0; i < kRecordSamples; ++i) {
        sum += rec[i];
    }
    const int32_t dc = static_cast<int32_t>(sum / kRecordSamples);
    for (int i = 0; i < kRecordSamples; ++i) {
        const int32_t v = static_cast<int32_t>(rec[i]) - dc;
        rec[i] = static_cast<int16_t>(v > INT16_MAX ? INT16_MAX : (v < -INT16_MAX ? -INT16_MAX : v));
    }

    int32_t peak = 0;
    for (int i = 0; i < kRecordSamples; ++i) {
        const int32_t v = std::abs(static_cast<int32_t>(rec[i]));
        if (v > peak) {
            peak = v;
        }
    }
    ESP_LOGI(TAG, "capture: %d samples, %d timeouts, dc=%ld, peak=%ld (%.1f dBFS)",
             filled, failures, static_cast<long>(dc), static_cast<long>(peak),
             peak > 0 ? 20.0 * std::log10(static_cast<double>(peak) / kFullScale) : -120.0);

    // Envelope in 100 ms blocks: shows at a glance whether the level is a
    // start-up transient or present throughout the capture, which are the
    // difference between "ignore the first block" and "the path is broken".
    constexpr int kBlocks = kRecordMs / 100;
    const int block_samples = kSampleRate / 10;
    char envelope[128];
    int written = 0;
    for (int b = 0; b < kBlocks; ++b) {
        int32_t block_peak = 0;
        const int start = b * block_samples;
        for (int i = 0; i < block_samples; ++i) {
            const int32_t v = std::abs(static_cast<int32_t>(rec[start + i]));
            if (v > block_peak) {
                block_peak = v;
            }
        }
        written += snprintf(envelope + written, sizeof(envelope) - written, " %ld",
                            static_cast<long>(block_peak));
        if (written >= static_cast<int>(sizeof(envelope)) - 8) {
            break;
        }
    }
    ESP_LOGI(TAG, "envelope(100ms):%s", envelope);

    if (peak >= INT16_MAX) {
        ESP_LOGW(TAG, "capture is clipped; the numbers below are not trustworthy");
    }
    return failures * 4 < 90 && peak > 0;
}

double RmsDbfs(const int16_t* data, int count) {
    if (count <= 0) {
        return -120.0;
    }
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        const double v = static_cast<double>(data[i]);
        sum += v * v;
    }
    const double rms = std::sqrt(sum / count);
    if (rms < 1.0) {
        return -120.0;
    }
    return 20.0 * std::log10(rms / kFullScale);
}

// Coarse search at half rate, then a full-rate refinement around the winner.
// Half rate keeps this at a few million multiply-accumulates, which is well
// under a second on the S3, instead of the ~15 M a full-rate sweep would need.
bool FindEcho(const int16_t* ref, const int16_t* rec, Measurement* out) {
    auto* ref_d = static_cast<int16_t*>(AllocSamples(kRefDec));
    auto* rec_d = static_cast<int16_t*>(AllocSamples(kRecDec));
    if (ref_d == nullptr || rec_d == nullptr) {
        heap_caps_free(ref_d);
        heap_caps_free(rec_d);
        return false;
    }

    // 2-tap average is a crude anti-alias filter; good enough because the chirp
    // tops out at 3 kHz against an 8 kHz decimated rate.
    for (int i = 0; i < kRefDec; ++i) {
        ref_d[i] = static_cast<int16_t>((ref[2 * i] + ref[2 * i + 1]) / 2);
    }
    for (int i = 0; i < kRecDec; ++i) {
        rec_d[i] = static_cast<int16_t>((rec[2 * i] + rec[2 * i + 1]) / 2);
    }

    int64_t ref_energy = 0;
    for (int i = 0; i < kRefDec; ++i) {
        ref_energy += static_cast<int64_t>(ref_d[i]) * ref_d[i];
    }
    if (ref_energy == 0) {
        heap_caps_free(ref_d);
        heap_caps_free(rec_d);
        return false;
    }

    int64_t window = 0;
    for (int i = 0; i < kRefDec; ++i) {
        window += static_cast<int64_t>(rec_d[i]) * rec_d[i];
    }

    const int max_delay = kRecDec - kRefDec;
    int best_d = -1;
    double best_rho = 0.0;
    for (int d = 0; d <= max_delay; ++d) {
        if (d > 0) {
            const int32_t entering = rec_d[d + kRefDec - 1];
            const int32_t leaving = rec_d[d - 1];
            window += static_cast<int64_t>(entering) * entering -
                      static_cast<int64_t>(leaving) * leaving;
        }
        if (window <= 0) {
            continue;
        }
        int64_t corr = 0;
        for (int n = 0; n < kRefDec; ++n) {
            corr += static_cast<int64_t>(ref_d[n]) * rec_d[d + n];
        }
        const double rho = static_cast<double>(corr) /
                           std::sqrt(static_cast<double>(ref_energy) * static_cast<double>(window));
        if (std::fabs(rho) > std::fabs(best_rho)) {
            best_rho = rho;
            best_d = d;
        }
    }

    heap_caps_free(ref_d);
    heap_caps_free(rec_d);

    if (best_d < 0) {
        return false;
    }

    // Refine at full rate. Decimation can shift the peak by up to a sample, and
    // a one-sample error at 16 kHz is 62 us - not enough to matter for AEC, but
    // the peak height (and therefore the gain) is much more accurate here.
    const int centre = best_d * kDecim;
    const int span = kDecim * 8;
    int64_t ref_energy_full = 0;
    for (int n = 0; n < kChirpSamples; ++n) {
        ref_energy_full += static_cast<int64_t>(ref[n]) * ref[n];
    }

    int best_full = centre;
    int64_t best_corr = 0;
    bool found = false;
    for (int d = centre - span; d <= centre + span; ++d) {
        if (d < 0 || d + kChirpSamples > kRecordSamples) {
            continue;
        }
        int64_t corr = 0;
        for (int n = 0; n < kChirpSamples; ++n) {
            corr += static_cast<int64_t>(ref[n]) * rec[d + n];
        }
        if (!found || std::llabs(corr) > std::llabs(best_corr)) {
            best_corr = corr;
            best_full = d;
            found = true;
        }
    }
    if (!found) {
        return false;
    }

    const double gain = static_cast<double>(best_corr) / static_cast<double>(ref_energy_full);
    out->delay_ms = 1000.0 * best_full / kSampleRate;
    out->gain_db = (std::fabs(gain) < 1e-9) ? -120.0 : 20.0 * std::log10(std::fabs(gain));
    out->rho = best_rho;
    out->echo_dbfs = RmsDbfs(rec + best_full, kChirpSamples);

    // Lead-in before the chirp is the noise floor: same electronics, no signal.
    const int quiet = kSampleRate * (kPlayDelayMs - 20) / 1000;
    out->noise_dbfs = RmsDbfs(rec, quiet);
    return true;
}

bool MeasureOnce(AudioCodec* codec, int16_t* ref, int16_t* rec, Measurement* out) {
    // Warm-up. Right after the RX channel is enabled it rails to full scale
    // for a fraction of a second while the clocks and DMA prime; those samples
    // would otherwise dominate both the peak detector and the noise floor, and
    // make a perfectly good capture look like a saturated one.
    std::vector<int16_t> warmup(kChunkSamples);
    for (int i = 0; i < 40; ++i) {  // ~400 ms
        std::fill(warmup.begin(), warmup.end(), 0);
        codec->InputData(warmup);
    }

    auto* ctx = new PlayCtx{codec, ref, false};
    TaskHandle_t play_task = nullptr;
    if (xTaskCreate(PlayTaskEntry, "probe_play", 4096, ctx, 5, &play_task) != pdPASS) {
        delete ctx;
        return false;
    }

    const bool captured = RecordMicrophone(codec, rec);

    // Wait for the playback task to finish before reusing the buffers, with a
    // bounded wait so a stuck write cannot hang the boot.
    for (int i = 0; i < 300 && !ctx->done; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (play_task != nullptr) {
        vTaskDelete(play_task);
    }
    delete ctx;

    if (!captured) {
        ESP_LOGE(TAG, "microphone produced no samples at all");
        return false;
    }
    return FindEcho(ref, rec, out);
}

}  // namespace

void GuachatRunAcousticProbe(AudioCodec* codec) {
    if (codec == nullptr) {
        ESP_LOGE(TAG, "no codec, skipping");
        return;
    }

    // Run-once guard, written BEFORE the measurement on purpose. If the probe
    // ever crashes hard enough to reboot the device, the flag is already set
    // and the next boot stays silent instead of looping on a chirp forever.
    Settings guard("diagnostics", true);
    // NVS keys are limited to 15 characters; going over aborts inside
    // Settings::SetBool() and turns boot into a reset loop. Keep these short.
    const std::string stored = guard.GetString("acoustic_res", "");
#if GUACHAT_ACOUSTIC_PROBE == 1
    // Once-only: a crash-reboot must never replay the chirp.
    if (guard.GetBool("probe_v3_done", false)) {
        if (!stored.empty()) {
            ESP_LOGI(TAG, "stored result: %s", stored.c_str());
        } else {
            ESP_LOGW(TAG, "probe already ran but left no result");
        }
        return;
    }
    guard.SetBool("probe_v3_done", true);
#else
    // Mode 2 is development only: always measure and overwrite the stored
    // result, so a retune takes effect on the next boot.
    (void)stored;
#endif

    // Wait before making noise. The board's USB-Serial-JTAG re-enumerates on
    // reset, so the first ~200 ms of the log never reaches the host; a delay
    // also gives anyone watching the serial port time to attach before the
    // chirp fires.
    vTaskDelay(pdMS_TO_TICKS(7000));

    auto* ref = static_cast<int16_t*>(AllocSamples(kChirpSamples));
    auto* rec = static_cast<int16_t*>(AllocSamples(kRecordSamples));
    if (ref == nullptr || rec == nullptr) {
        ESP_LOGE(TAG, "buffer allocation failed");
        heap_caps_free(ref);
        heap_caps_free(rec);
        return;
    }

    GenerateChirp(ref, kChirpSamples);

    // SetOutputVolume() persists to NVS, so restoring the in-RAM value is not
    // enough - the user's saved volume has to be read back and written again,
    // otherwise this diagnostic silently resets their volume to 70.
    const int saved_volume = Settings("audio", false).GetInt("output_volume", codec->output_volume());

    codec->EnableOutput(true);
    codec->EnableInput(true);
    vTaskDelay(pdMS_TO_TICKS(100));  // let the amplifier and mic settle

    ESP_LOGW(TAG, "measuring echo path, two chirps will sound (~5 s)");

    // Two passes: at the normal listening volume, then at 30% to check
    // linearity. The digital volume factor is squared, so 30% should sit
    // 14.7 dB below 70%. A smaller drop means the amplifier or speaker is
    // compressing, which is exactly the non-linearity AEC cannot remove.
    //
    // 70 rather than 100 on purpose: loud enough to be representative, quiet
    // enough not to be obnoxious, and it is the volume people actually use.
    Measurement loud;
    Measurement soft;
    const bool have_loud = [&]() {
        codec->SetOutputVolume(70);
        return MeasureOnce(codec, ref, rec, &loud);
    }();
    vTaskDelay(pdMS_TO_TICKS(200));
    const bool have_soft = [&]() {
        codec->SetOutputVolume(30);
        return MeasureOnce(codec, ref, rec, &soft);
    }();

    codec->SetOutputVolume(saved_volume);
    codec->EnableInput(false);
    codec->EnableOutput(false);
    heap_caps_free(ref);
    heap_caps_free(rec);

    if (!have_loud) {
        ESP_LOGE(TAG, "measurement failed");
        return;
    }

    ESP_LOGI(TAG, "--- echo path @ 70%% volume ---");
    ESP_LOGI(TAG, "  delay      : %.1f ms   (I2S write -> I2S read)", loud.delay_ms);
    ESP_LOGI(TAG, "  gain       : %.1f dB   (echo vs reference)", loud.gain_db);
    ESP_LOGI(TAG, "  echo level : %.1f dBFS", loud.echo_dbfs);
    ESP_LOGI(TAG, "  noise floor: %.1f dBFS", loud.noise_dbfs);
    ESP_LOGI(TAG, "  correlation: %.2f     (1.00 = perfectly linear echo)", loud.rho);

    if (have_soft) {
        const double drop = loud.gain_db - soft.gain_db;
        ESP_LOGI(TAG, "--- linearity @ 30%% volume ---");
        ESP_LOGI(TAG, "  gain       : %.1f dB", soft.gain_db);
        ESP_LOGI(TAG, "  drop vs 70%%: %.1f dB   (expect 14.7 dB when linear)", drop);
        if (drop < 11.0) {
            ESP_LOGW(TAG, "  -> amplifier or speaker is compressing");
        }
    }

    // Verdict, written out so the log answers the question on its own.
    const double attenuation = -loud.gain_db;
    const double snr = loud.echo_dbfs - loud.noise_dbfs;
    const char* verdict = "poor";
    if (attenuation >= 25.0 && loud.rho >= 0.5) {
        verdict = "good";
        ESP_LOGI(TAG, "VERDICT: good AEC candidate (attenuation %.1f dB, rho %.2f)",
                 attenuation, loud.rho);
    } else if (attenuation >= 15.0 && loud.rho >= 0.3) {
        verdict = "marginal";
        ESP_LOGI(TAG, "VERDICT: workable but marginal (attenuation %.1f dB, rho %.2f)",
                 attenuation, loud.rho);
    } else {
        ESP_LOGW(TAG, "VERDICT: poor AEC candidate (attenuation %.1f dB, rho %.2f, snr %.1f dB)",
                 attenuation, loud.rho, snr);
        ESP_LOGW(TAG, "         echo is short, loud or non-linear - device-side AEC will struggle");
    }

    // Persist the numbers so they survive this boot: see the skip branch above.
    char summary[256];
    snprintf(summary, sizeof(summary),
             "delay=%.1fms gain=%.1fdB echo=%.1fdBFS noise=%.1fdBFS rho=%.2f soft=%s %.1fdB verdict=%s",
             loud.delay_ms, loud.gain_db, loud.echo_dbfs, loud.noise_dbfs, loud.rho,
             have_soft ? "gain" : "n/a", have_soft ? soft.gain_db : 0.0, verdict);
    Settings("diagnostics", true).SetString("acoustic_res", summary);
}
