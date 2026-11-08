// Minimal host for the legacy 2.x plugin binary, written against FST only (kept out of the JUCE
// translation units on purpose). Loads the binary, runs audio through it, checks level and latency.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <fst.h>

#include <cmath>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#include <windows.h>
static void* openLib(const char* p) { return (void*)LoadLibraryA(p); }
static void* sym(void* h, const char* n) { return (void*)GetProcAddress((HMODULE)h, n); }
#else
#include <dlfcn.h>
static void* openLib(const char* p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void* sym(void* h, const char* n) { return dlsym(h, n); }
#endif

namespace {
// opcodes (numeric values documented in FST)
constexpr int kOpen = 0, kClose = 1, kSetSampleRate = 10, kSetBlockSize = 11, kMainsChanged = 12, kGetEffectName = 45;
constexpr int kHostVersion = 1;

VstIntPtr hostCallback(AEffect*, int opcode, int, VstIntPtr, void*, float) { return opcode == kHostVersion ? 2400 : 0; }
typedef AEffect* (*EntryProc)(AEffectDispatcherProc);
}  // namespace

int checkVst2Binary(const char* path) {
    int failures = 0;
    std::printf("VST 2.x (FST host): %s\n", path);
    void* lib = openLib(path);
    if (!lib) { std::printf("  FAIL could not load the binary\n"); return 1; }
    auto entry = (EntryProc)sym(lib, "VSTPluginMain");
    if (!entry) entry = (EntryProc)sym(lib, "main");
    if (!entry) { std::printf("  FAIL no entry point\n"); return 1; }

    for (double sr : {44100.0, 48000.0}) {
        AEffect* e = entry(hostCallback);
        if (!e || e->magic != 0x56737450) { std::printf("  FAIL bad effect magic\n"); return failures + 1; }
        e->dispatcher(e, kOpen, 0, 0, nullptr, 0.f);
        char name[256] = {};
        e->dispatcher(e, kGetEffectName, 0, 0, name, 0.f);
        e->dispatcher(e, kSetSampleRate, 0, 0, nullptr, float(sr));
        e->dispatcher(e, kSetBlockSize, 0, 512, nullptr, 0.f);
        e->dispatcher(e, kMainsChanged, 0, 1, nullptr, 0.f);
        const int expected = int(std::lround(sr * 0.005));
        if (e->numInputs != 2 || e->numOutputs != 2) { std::printf("  FAIL channels %d/%d\n", e->numInputs, e->numOutputs); ++failures; }
        if (e->initialDelay != expected) { std::printf("  FAIL latency %d (expected %d)\n", e->initialDelay, expected); ++failures; }

        std::vector<float> inL(512), inR(512), outL(512), outR(512);
        float* in[2] = {inL.data(), inR.data()};
        float* out[2] = {outL.data(), outR.data()};
        const float amp = std::pow(10.f, -32.f / 20.f) * std::sqrt(2.f);
        double ph = 0, sum = 0; long cnt = 0;
        const int blocks = int(10 * sr / 512);
        for (int b = 0; b < blocks; ++b) {
            for (int k = 0; k < 512; ++k) { inL[size_t(k)] = inR[size_t(k)] = amp * float(std::sin(ph)); ph += 2 * M_PI * 220 / sr; }
            e->processReplacing(e, in, out, 512);
            if (b >= blocks - int(sr / 512)) for (float v : outL) { sum += double(v) * v; ++cnt; }
        }
        const float rms = float(10 * std::log10(sum / double(cnt)));
        std::printf("  '%s' %5.1f kHz: -32 dB RMS in -> %.2f dB RMS out, latency %d samples\n", name, sr / 1000, rms, e->initialDelay);
        if (std::fabs(rms + 20.f) > 1.0f) { std::printf("  FAIL leveling\n"); ++failures; }
        e->dispatcher(e, kMainsChanged, 0, 0, nullptr, 0.f);
        e->dispatcher(e, kClose, 0, 0, nullptr, 0.f);
    }
    return failures;
}
