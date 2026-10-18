#include "audio_engine.hpp"

#define MA_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_FLAC
#include "../thirdparty/miniaudio/miniaudio.h"

#include <mutex>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "winmm.lib")

namespace audio {

namespace {

ma_engine g_engine{};
ma_sound g_sound{};
ma_decoder g_decoder{};
bool g_engineOk = false;
bool g_soundLive = false;
std::vector<unsigned char> g_keepAlive;
std::mutex g_mu;
float g_volume = 0.6f;

void unload_sound() {
    if (g_soundLive) {
        ma_sound_uninit(&g_sound);
        ma_decoder_uninit(&g_decoder);
        g_soundLive = false;
    }
}

} // namespace

bool init() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_engineOk) return true;

    // the rig's surround device rejects fixed stereo/rate configs - walk down to defaults
    struct attempt { int channels; int sampleRate; int periodMs; };
    static const attempt kTries[] = {
        { 2, 44100, 60 },
        { 2, 0,     60 },
        { 0, 0,     0  },
    };

    for (const attempt& t : kTries) {
        ma_engine_config cfg = ma_engine_config_init();
        cfg.channels = t.channels;
        cfg.sampleRate = t.sampleRate;
        if (t.periodMs > 0)
            cfg.periodSizeInMilliseconds = t.periodMs;
        if (ma_engine_init(&cfg, &g_engine) == MA_SUCCESS) {
            g_engineOk = true;
            ma_engine_set_volume(&g_engine, g_volume);
            return true;
        }
    }

    return false;
}

void shutdown() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_engineOk) return;
    unload_sound();
    ma_engine_uninit(&g_engine);
    g_engineOk = false;
    g_keepAlive.clear();
}

const char* play(const std::vector<unsigned char>& bytes) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_engineOk || bytes.empty()) return "engine not ready";
    unload_sound();
    g_keepAlive = bytes;
    if (ma_decoder_init_memory(g_keepAlive.data(), g_keepAlive.size(), nullptr, &g_decoder) !=
        MA_SUCCESS) {
        g_keepAlive.clear();
        return "decoder rejected the bytes";
    }
    if (ma_sound_init_from_data_source(&g_engine, &g_decoder, 0, nullptr, &g_sound) !=
        MA_SUCCESS) {
        ma_decoder_uninit(&g_decoder);
        g_keepAlive.clear();
        return "sound init failed";
    }
    g_soundLive = true;
    ma_sound_set_volume(&g_sound, g_volume);
    ma_sound_start(&g_sound);
    return nullptr;
}

void pause() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_engineOk && g_soundLive) ma_sound_stop(&g_sound);
}

void resume() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_engineOk && g_soundLive) ma_sound_start(&g_sound);
}

void stop() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_engineOk) return;
    unload_sound();
    g_keepAlive.clear();
}

bool playing() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_engineOk || !g_soundLive) return false;
    return ma_sound_is_playing(&g_sound) == MA_TRUE;
}

bool paused() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_engineOk || !g_soundLive) return false;
    return ma_sound_is_playing(&g_sound) != MA_TRUE;
}

double position_sec() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_engineOk || !g_soundLive) return 0.0;
    float cur = 0.f;
    if (ma_sound_get_cursor_in_seconds(&g_sound, &cur) != MA_SUCCESS) return 0.0;
    return (double)cur;
}

double duration_sec() {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_engineOk || !g_soundLive) return 0.0;
    float len = 0.f;
    if (ma_sound_get_length_in_seconds(&g_sound, &len) != MA_SUCCESS) return 0.0;
    return (double)len;
}

void seek_sec(double sec) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_engineOk || !g_soundLive) return;
    ma_uint32 srcRate = 0;
    if (ma_sound_get_data_format(&g_sound, NULL, NULL, &srcRate, NULL, 0) != MA_SUCCESS || srcRate == 0) {
        srcRate = ma_engine_get_sample_rate(&g_engine);
    }
    if (srcRate == 0) return;
    if (sec < 0.0) sec = 0.0;
    ma_sound_seek_to_pcm_frame(&g_sound, (ma_uint64)(sec * (double)srcRate));
}

void set_volume(float v) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    g_volume = v;
    if (g_engineOk) ma_engine_set_volume(&g_engine, v);
    if (g_engineOk && g_soundLive) ma_sound_set_volume(&g_sound, v);
}

} // namespace audio
