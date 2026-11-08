// Loads the built OBS filter module with a minimal stand-in for libobs, then pushes audio
// through it the way OBS does (planar float, OBS's sample rate and channel count).
// Linux only; used in CI to check the filter end to end without installing OBS.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <obs-module.h>
#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------- libobs stand-in
struct obs_data { std::map<std::string, double> num; std::map<std::string, bool> flag; };
struct obs_properties { int dummy; };
struct obs_property { int dummy; };
struct audio_output { int dummy; };

static obs_source_info g_info{};
static bool g_registered = false;
static uint32_t g_rate = 48000;
static size_t g_channels = 2;
static audio_output g_audio{};
static obs_properties g_props{};
static obs_property g_prop{};
static std::string g_latencyText;

extern "C" {
void obs_register_source_s(const struct obs_source_info* info, size_t size) {
    std::memcpy(&g_info, info, std::min(size, sizeof g_info)); g_registered = true;
}
void blog(int, const char* fmt, ...) { va_list a; va_start(a, fmt); std::vprintf(fmt, a); va_end(a); std::printf("\n"); }
audio_t* obs_get_audio(void) { return &g_audio; }
uint32_t audio_output_get_sample_rate(const audio_t*) { return g_rate; }
size_t audio_output_get_channels(const audio_t*) { return g_channels; }
double obs_data_get_double(obs_data_t* d, const char* n) { return d->num[n]; }
long long obs_data_get_int(obs_data_t* d, const char* n) { return (long long)d->num[n]; }
bool obs_data_get_bool(obs_data_t* d, const char* n) { return d->flag[n]; }
void obs_data_set_double(obs_data_t* d, const char* n, double v) { d->num[n] = v; }
void obs_data_set_bool(obs_data_t* d, const char* n, bool v) { d->flag[n] = v; }
void obs_data_set_default_double(obs_data_t* d, const char* n, double v) { if (!d->num.count(n)) d->num[n] = v; }
void obs_data_set_default_int(obs_data_t* d, const char* n, long long v) { if (!d->num.count(n)) d->num[n] = double(v); }
void obs_data_set_default_bool(obs_data_t* d, const char* n, bool v) { if (!d->flag.count(n)) d->flag[n] = v; }
obs_properties_t* obs_properties_create(void) { return &g_props; }
obs_property_t* obs_properties_add_bool(obs_properties_t*, const char*, const char*) { return &g_prop; }
obs_property_t* obs_properties_add_button(obs_properties_t*, const char*, const char*, obs_property_clicked_t) { return &g_prop; }
obs_property_t* obs_properties_add_float_slider(obs_properties_t*, const char*, const char*, double, double, double) { return &g_prop; }
obs_property_t* obs_properties_add_list(obs_properties_t*, const char*, const char*, enum obs_combo_type, enum obs_combo_format) { return &g_prop; }
obs_property_t* obs_properties_add_text(obs_properties_t*, const char* id, const char* desc, enum obs_text_type) {
    if (std::string(id) == "latency") g_latencyText = desc; return &g_prop;
}
void obs_property_float_set_suffix(obs_property_t*, const char*) {}
size_t obs_property_list_add_int(obs_property_t*, const char*, long long) { return 0; }
void obs_property_set_long_description(obs_property_t*, const char*) {}
}

// ---------------------------------------------------------------- test
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("  FAIL %s  ", #c); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static float runSine(void* filter, float levelDb, double seconds, double& phase, float* outRms) {
    const int frames = 1024;  // OBS delivers audio in 1024-frame packets
    std::vector<std::vector<float>> buf(g_channels, std::vector<float>(frames));
    const float amp = std::pow(10.f, levelDb / 20.f) * std::sqrt(2.f);
    double sum = 0; long count = 0; float last = 0;
    const long total = long(seconds * g_rate);
    for (long done = 0; done < total; done += frames) {
        obs_audio_data a{};
        for (size_t c = 0; c < g_channels; ++c) {
            for (int k = 0; k < frames; ++k) buf[c][size_t(k)] = amp * float(std::sin(phase + 2 * M_PI * 220.0 * k / g_rate));
            a.data[c] = reinterpret_cast<uint8_t*>(buf[c].data());
        }
        phase += 2 * M_PI * 220.0 * frames / g_rate;
        a.frames = frames;
        obs_audio_data* r = g_info.filter_audio(filter, &a);
        if (done > total - long(g_rate)) {  // last second
            const float* o = reinterpret_cast<float*>(r->data[0]);
            for (int k = 0; k < frames; ++k) { sum += double(o[k]) * o[k]; ++count; }
        }
        last = 0;
    }
    *outRms = float(10 * std::log10(sum / double(count)));
    return last;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: obs_harness <corridor-leveler.so>\n"); return 2; }
    void* h = dlopen(argv[1], RTLD_NOW);
    if (!h) { std::printf("dlopen failed: %s\n", dlerror()); return 1; }
    auto load = reinterpret_cast<bool (*)()>(dlsym(h, "obs_module_load"));
    CHECK(load && load(), "obs_module_load failed");
    CHECK(g_registered, "filter not registered");
    CHECK(g_info.type == OBS_SOURCE_TYPE_FILTER && (g_info.output_flags & OBS_SOURCE_AUDIO), "wrong source type/flags");
    std::printf("registered: %s (%s)\n", g_info.id, g_info.get_name(nullptr));

    for (uint32_t rate : {44100u, 48000u}) {
        for (size_t ch : {size_t(1), size_t(2)}) {
            g_rate = rate; g_channels = ch;
            obs_data settings;
            g_info.get_defaults(&settings);
            void* f = g_info.create(&settings, nullptr);
            double phase = 0; float rms = 0;
            runSine(f, -32.f, 10.0, phase, &rms);
            std::printf("  %u Hz, %zu ch: -32 dB RMS in -> %.2f dB RMS out\n", rate, ch, rms);
            CHECK(std::fabs(rms + 20.f) < 1.0f, "rate %u ch %zu: %.2f", rate, ch, rms);
            g_info.get_properties(f);
            CHECK(g_latencyText.find("Latency: 5.0 ms") == 0, "latency text '%s'", g_latencyText.c_str());
            // bypass via settings update
            settings.flag["bypass"] = true; g_info.update(f, &settings);
            runSine(f, -32.f, 2.0, phase, &rms);
            CHECK(std::fabs(rms + 32.f) < 0.3f, "bypass: %.2f", rms);
            g_info.destroy(f);
        }
    }
    std::printf("latency info shown in OBS: \"%s\"\n", g_latencyText.c_str());
    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("OBS filter checks passed\n");
    return 0;
}
