// Corridor Leveler - native OBS audio filter.
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Uses the same DSP core as the VST3/VST2 builds. OBS hands filters planar float audio at the
// sample rate and channel layout set in OBS's audio settings; the filter follows both.
#include <obs-module.h>
#include <media-io/audio-io.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>

#include "corridor/Processor.h"

OBS_DECLARE_MODULE()

MODULE_EXPORT const char* obs_module_name(void) { return "Corridor Leveler"; }
MODULE_EXPORT const char* obs_module_description(void) {
    return "Automatic level rider: keeps an audio source at a target level, never boosts silence, never passes the ceiling.";
}

namespace {

struct Filter {
    obs_source_t* context = nullptr;
    corridor::Processor proc;
    std::mutex paramLock;
    corridor::Params pending;
    std::atomic<bool> dirty{true};
    uint32_t sampleRate = 0;
    size_t channels = 0;
};

const char* kId = "corridor_leveler_filter";

void readSettings(Filter* f, obs_data_t* s) {
    corridor::Params p;
    p.hold = obs_data_get_int(s, "mode") == 1;
    p.target = float(obs_data_get_double(s, "target"));
    p.threshold = float(obs_data_get_double(s, "threshold"));
    p.response = float(obs_data_get_double(s, "response"));
    p.range = float(obs_data_get_double(s, "range"));
    p.ceiling = float(obs_data_get_double(s, "ceiling"));
    p.easeInSilence = obs_data_get_bool(s, "ease");
    p.bypass = obs_data_get_bool(s, "bypass");
    {
        std::lock_guard<std::mutex> lk(f->paramLock);
        f->pending = p;
    }
    f->dirty.store(true);
}

const char* getName(void*) { return "Corridor Leveler"; }

void* create(obs_data_t* settings, obs_source_t* source) {
    auto* f = new Filter();
    f->context = source;
    audio_t* a = obs_get_audio();
    f->sampleRate = audio_output_get_sample_rate(a);
    f->channels = audio_output_get_channels(a);
    f->proc.prepare(double(f->sampleRate), int(f->channels));
    readSettings(f, settings);
    if (obs_data_get_bool(settings, "hold_valid"))
        f->proc.setStoredHoldGain(true, float(obs_data_get_double(settings, "hold_gain")));
    return f;
}

void destroy(void* data) { delete static_cast<Filter*>(data); }

void update(void* data, obs_data_t* settings) { readSettings(static_cast<Filter*>(data), settings); }

void save(void* data, obs_data_t* settings) {
    auto* f = static_cast<Filter*>(data);
    const auto& t = f->proc.telemetry();
    obs_data_set_bool(settings, "hold_valid", t.holdValid.load());
    obs_data_set_double(settings, "hold_gain", t.holdGain.load());
}

void getDefaults(obs_data_t* s) {
    obs_data_set_default_int(s, "mode", 0);
    obs_data_set_default_double(s, "target", -20.0);
    obs_data_set_default_double(s, "threshold", -48.0);
    obs_data_set_default_double(s, "response", 50.0);
    obs_data_set_default_double(s, "range", 12.0);
    obs_data_set_default_double(s, "ceiling", -1.0);
    obs_data_set_default_bool(s, "ease", false);
    obs_data_set_default_bool(s, "bypass", false);
}

bool analyzeClicked(obs_properties_t*, obs_property_t*, void* data) {
    auto* f = static_cast<Filter*>(data);
    if (f->proc.telemetry().analyzing.load()) f->proc.requestAnalyzeLock();
    else f->proc.requestAnalyzeStart();
    return false;
}

obs_properties_t* getProperties(void* data) {
    auto* f = static_cast<Filter*>(data);
    obs_properties_t* props = obs_properties_create();

    obs_property_t* mode = obs_properties_add_list(props, "mode", "Mode", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(mode, "Ride (adjusts continuously)", 0);
    obs_property_list_add_int(mode, "Hold (one fixed correction)", 1);

    auto slider = [&](const char* id, const char* label, double lo, double hi, double step, const char* suffix, const char* help) {
        obs_property_t* p = obs_properties_add_float_slider(props, id, label, lo, hi, step);
        obs_property_float_set_suffix(p, suffix);
        obs_property_set_long_description(p, help);
    };
    slider("threshold", "Threshold", -70, -20, 0.5, " dB RMS", "Below this level it stops adjusting, so silence, breaths and noise never trigger a boost.");
    slider("target", "Target", -40, -6, 0.5, " dB RMS", "The level you want.");
    slider("response", "Response", 0, 100, 1, "", "Slow gives smooth, gradual changes. Fast reacts quickly to sudden jumps. On drums it follows the groove, not single hits.");
    slider("range", "Range", 0, 24, 0.5, " dB", "The most it may boost or cut.");
    slider("ceiling", "Ceiling", -12, 0, 0.1, " dBFS", "The highest peak the output may reach. A look-ahead limiter catches stray peaks.");

    obs_property_t* ease = obs_properties_add_bool(props, "ease", "Ease the gain back to 0 dB during long silences");
    obs_property_set_long_description(ease, "Off: the gain freezes below the threshold. On: after 2 s of silence it glides back to 0 dB.");
    obs_properties_add_bool(props, "bypass", "Bypass");

    obs_properties_add_button(props, "analyze", "Hold mode: Analyze / Lock", analyzeClicked);

    char info[256];
    const double ms = 1000.0 * f->proc.latencySamples() / f->proc.sampleRate();
    std::snprintf(info, sizeof info, "Latency: %.1f ms (%d samples at %.1f kHz)", ms, f->proc.latencySamples(), f->proc.sampleRate() / 1000.0);
    obs_properties_add_text(props, "latency", info, OBS_TEXT_INFO);
    return props;
}

obs_audio_data* filterAudio(void* data, obs_audio_data* audio) {
    auto* f = static_cast<Filter*>(data);
    audio_t* a = obs_get_audio();
    const uint32_t sr = audio_output_get_sample_rate(a);
    const size_t ch = audio_output_get_channels(a);
    if (sr != f->sampleRate || ch != f->channels) {
        f->sampleRate = sr; f->channels = ch;
        f->proc.prepare(double(sr), int(ch));
    }
    if (f->dirty.load()) {
        std::unique_lock<std::mutex> lk(f->paramLock, std::try_to_lock);
        if (lk.owns_lock()) { f->proc.setParams(f->pending); f->dirty.store(false); }
    }
    float* planes[MAX_AV_PLANES] = {};
    int n = 0;
    for (size_t c = 0; c < ch && c < MAX_AV_PLANES; ++c) {
        if (!audio->data[c]) break;
        planes[n++] = reinterpret_cast<float*>(audio->data[c]);
    }
    if (n > 0 && audio->frames > 0) f->proc.process(planes, n, int(audio->frames));
    return audio;
}

}  // namespace

bool obs_module_load(void) {
    obs_source_info info = {};
    info.id = kId;
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = getName;
    info.create = create;
    info.destroy = destroy;
    info.update = update;
    info.save = save;
    info.get_defaults = getDefaults;
    info.get_properties = getProperties;
    info.filter_audio = filterAudio;
    obs_register_source(&info);
    blog(LOG_INFO, "[corridor-leveler] loaded");
    return true;
}
