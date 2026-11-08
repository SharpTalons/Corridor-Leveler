// Behaviour tests for the Corridor DSP core. Plain C++, no framework.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "corridor/Processor.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace corridor;

static int failures = 0;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            ++failures;                                                     \
            std::printf("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #cond);   \
            std::printf(__VA_ARGS__);                                       \
            std::printf("\n");                                              \
        }                                                                   \
    } while (0)

static float db(double x) { return x > 1e-12 ? float(20.0 * std::log10(x)) : -240.f; }
static float lin(float d) { return std::pow(10.f, d / 20.f); }

// Runs `seconds` of a generator through the processor in host-sized blocks (stereo).
// gen(t, channel) returns a sample. Returns the processed output.
struct Run {
    std::vector<float> l, r;
};
static Run run(Processor& p, double fs, double seconds, const std::function<float(double, int)>& gen, int block = 512) {
    const int n = int(seconds * fs);
    Run out; out.l.resize(size_t(n)); out.r.resize(size_t(n));
    std::vector<float> a(static_cast<size_t>(block)), b(static_cast<size_t>(block));
    for (int pos = 0; pos < n; pos += block) {
        const int m = std::min(block, n - pos);
        for (int k = 0; k < m; ++k) { const double t = (pos + k) / fs; a[size_t(k)] = gen(t, 0); b[size_t(k)] = gen(t, 1); }
        float* chans[2] = {a.data(), b.data()};
        p.process(chans, 2, m);
        for (int k = 0; k < m; ++k) { out.l[size_t(pos + k)] = a[size_t(k)]; out.r[size_t(pos + k)] = b[size_t(k)]; }
    }
    return out;
}
static float rmsDb(const std::vector<float>& v, size_t from, size_t to) {
    double s = 0; for (size_t i = from; i < to; ++i) s += double(v[i]) * v[i];
    return db(std::sqrt(s / double(to - from)));
}
static float peakDb(const std::vector<float>& v, size_t from, size_t to) {
    float m = 0; for (size_t i = from; i < to; ++i) m = std::max(m, std::fabs(v[i])); return db(m);
}
// sine whose RMS is `levelDb` dB
static std::function<float(double, int)> sine(float levelDb, double f = 220.0) {
    const float a = lin(levelDb) * std::sqrt(2.f);
    return [=](double t, int) { return a * float(std::sin(2 * M_PI * f * t)); };
}

static void testLatency() {
    std::printf("latency matches 5 ms at every sample rate\n");
    for (double fs : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
        Processor p; p.prepare(fs, 2);
        CHECK(p.latencySamples() == int(std::lround(fs * 0.005)), "fs=%g latency=%d", fs, p.latencySamples());
    }
}

static void testRideToTarget() {
    std::printf("ride brings a quiet and a loud signal to the target\n");
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        for (float in : {-34.f, -8.f}) {
            Processor p; p.prepare(fs, 2);
            Params pr; pr.target = -20.f; pr.range = 18.f; pr.ceiling = 0.f; p.setParams(pr);
            auto o = run(p, fs, 12.0, sine(in));
            const float outRms = rmsDb(o.l, size_t(10 * fs), size_t(12 * fs));
            CHECK(std::fabs(outRms - pr.target) < 1.0f, "fs=%g in=%.1f out=%.2f", fs, in, outRms);
        }
    }
}

static void testStereoLinked() {
    std::printf("stereo: both channels get the same gain\n");
    const double fs = 48000; Processor p; p.prepare(fs, 2);
    Params pr; pr.target = -20; p.setParams(pr);
    const float al = lin(-30) * std::sqrt(2.f), ar = lin(-40) * std::sqrt(2.f);
    auto o = run(p, fs, 8.0, [&](double t, int c) { return (c ? ar : al) * float(std::sin(2 * M_PI * 300 * t)); });
    const size_t a = size_t(7 * fs), b = size_t(8 * fs);
    const float diff = rmsDb(o.l, a, b) - rmsDb(o.r, a, b);
    CHECK(std::fabs(diff - 10.f) < 0.1f, "L-R difference %.3f dB (should stay 10)", diff);
}

static void testThresholdFreeze() {
    std::printf("below the threshold the gain freezes (no boost of noise)\n");
    const double fs = 48000; Processor p; p.prepare(fs, 2);
    Params pr; pr.target = -20; pr.threshold = -48; pr.range = 12; p.setParams(pr);
    run(p, fs, 8.0, sine(-28));                       // gets boosted ~ +8
    const float gBefore = p.telemetry().gain.load();
    run(p, fs, 6.0, sine(-60));                       // "room noise" below threshold
    const float gAfter = p.telemetry().gain.load();
    CHECK(gBefore > 6.f, "gain before %.2f", gBefore);
    CHECK(std::fabs(gAfter - gBefore) < 0.05f, "gain moved during silence: %.2f -> %.2f", gBefore, gAfter);
    CHECK(p.telemetry().state.load() == int(State::Waiting), "state %d", p.telemetry().state.load());
}

static void testEaseInSilence() {
    std::printf("ease option: gain glides back to 0 dB in long silence\n");
    const double fs = 48000; Processor p; p.prepare(fs, 2);
    Params pr; pr.target = -20; pr.threshold = -48; pr.range = 12; pr.easeInSilence = true; p.setParams(pr);
    run(p, fs, 8.0, sine(-28));
    const float g0 = p.telemetry().gain.load();
    run(p, fs, 1.5, sine(-60));
    const float gEarly = p.telemetry().gain.load();
    run(p, fs, 8.0, sine(-60));
    const float gLate = p.telemetry().gain.load();
    CHECK(std::fabs(gEarly - g0) < 0.05f, "should wait ~2 s before easing (%.2f -> %.2f)", g0, gEarly);
    CHECK(std::fabs(gLate) < 0.1f, "should reach 0 dB, got %.2f", gLate);
}

static void testRange() {
    std::printf("range caps the boost\n");
    const double fs = 48000; Processor p; p.prepare(fs, 2);
    Params pr; pr.target = -10; pr.threshold = -60; pr.range = 6; pr.ceiling = 0; p.setParams(pr);
    run(p, fs, 10.0, sine(-40));
    const float g = p.telemetry().gain.load();
    CHECK(g <= 6.001f && g > 5.5f, "gain %.2f should sit at the 6 dB range", g);
    CHECK(p.telemetry().state.load() == int(State::Range), "state %d", p.telemetry().state.load());
}

static void testCeiling() {
    std::printf("output peaks never pass the ceiling, at any sample rate\n");
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        Processor p; p.prepare(fs, 2);
        Params pr; pr.target = -6; pr.ceiling = -3; pr.range = 24; pr.response = 100; p.setParams(pr);
        std::mt19937 rng(1);
        std::uniform_real_distribution<float> u(-1, 1);
        // bursts of loud noise with sharp transients
        auto o = run(p, fs, 6.0, [&](double t, int) { return u(rng) * ((int(t * 4) % 2) ? 1.4f : 0.05f); });
        const float pk = std::max(peakDb(o.l, 0, o.l.size()), peakDb(o.r, 0, o.r.size()));
        CHECK(pk <= -3.f + 0.01f, "fs=%g peak %.3f dBFS above ceiling", fs, pk);
    }
}

static void testBypass() {
    std::printf("bypass: output is the input delayed by exactly the reported latency\n");
    const double fs = 48000; Processor p; p.prepare(fs, 2);
    Params pr; pr.bypass = true; p.setParams(pr); p.reset();
    std::vector<float> in(static_cast<size_t>(fs));
    std::mt19937 rng(7); std::uniform_real_distribution<float> u(-0.5f, 0.5f);
    for (auto& v : in) v = u(rng);
    size_t idx = 0;
    auto o = run(p, fs, 1.0, [&](double, int c) { if (c == 0) return in[idx]; return in[idx++]; });
    const int L = p.latencySamples();
    double err = 0;
    for (size_t i = size_t(L); i < in.size(); ++i) err = std::max(err, double(std::fabs(o.l[i] - in[i - size_t(L)])));
    CHECK(err < 1e-6, "max deviation %g", err);
}

static void testHoldAnalyze() {
    std::printf("hold: analyze a section and lock one fixed correction\n");
    const double fs = 48000; Processor p; p.prepare(fs, 2);
    Params pr; pr.target = -20; pr.hold = true; pr.range = 18; pr.ceiling = 0; p.setParams(pr);
    run(p, fs, 0.5, sine(-30));
    p.requestAnalyzeStart();
    run(p, fs, 4.0, sine(-30));
    p.requestAnalyzeLock();
    run(p, fs, 3.0, sine(-30));
    const auto& t = p.telemetry();
    CHECK(t.holdValid.load(), "no hold value");
    CHECK(std::fabs(t.holdGain.load() - 10.f) < 0.3f, "hold gain %.2f (expected ~+10)", t.holdGain.load());
    CHECK(std::fabs(t.gain.load() - t.holdGain.load()) < 0.05f, "applied %.2f vs hold %.2f", t.gain.load(), t.holdGain.load());
    // the level changes, the gain must not
    run(p, fs, 4.0, sine(-40));
    CHECK(std::fabs(t.gain.load() - t.holdGain.load()) < 0.05f, "gain moved in Hold: %.2f", t.gain.load());
}

static void testDrums() {
    std::printf("drums: percussive material is detected and the gain stays steady\n");
    const double fs = 48000; Processor p; p.prepare(fs, 2);
    Params pr; pr.target = -20; pr.response = 70; pr.range = 12; pr.ceiling = 0; p.setParams(pr);
    // 100 BPM kick and snare on quarter notes: instant attack, short decay
    auto hits = [&](float level) {
        const double step = 60.0 / 100.0;
        return [=](double t, int) {
            const double ph = std::fmod(t, step);
            const bool snare = int(t / step) % 2;
            const float tone = snare ? float(std::sin(2 * M_PI * 190 * t) * 0.6 + std::sin(2 * M_PI * 3100 * t) * 0.4)
                                     : float(std::sin(2 * M_PI * (55 + 80 * std::exp(-ph / 0.03)) * ph));
            return level * float(std::exp(-ph / (snare ? 0.06 : 0.12))) * tone;
        };
    };
    run(p, fs, 6.0, hits(0.25f));
    CHECK(p.telemetry().percussive.load(), "crest %.1f not detected as percussive", p.telemetry().crest.load());
    // gain spread over 4 s of steady groove must be small (no pumping per hit)
    std::vector<Snapshot> h(kHistorySize);
    run(p, fs, 4.0, hits(0.25f));
    const int n = p.copyHistory(h.data(), 200);  // last 4 s
    float lo = 1e9f, hi = -1e9f;
    for (int k = 0; k < n; ++k) { lo = std::min(lo, h[size_t(k)].gain); hi = std::max(hi, h[size_t(k)].gain); }
    CHECK(hi - lo < 1.5f, "gain swung %.2f dB over a steady groove", hi - lo);
}

static void testVoiceNotPercussive() {
    std::printf("voice-like material is not mistaken for drums\n");
    const double fs = 48000; Processor p; p.prepare(fs, 2);
    Params pr; pr.target = -20; p.setParams(pr);
    // 4.5 syllables per second, ~30 ms attack, with a little pitch movement
    run(p, fs, 8.0, [](double t, int) {
        const double syl = std::fmod(t * 4.5, 1.0);
        const double env = syl < 0.7 ? std::min(1.0, syl / 0.13) * (0.6 + 0.4 * std::sin(t * 1.7)) : 0.0;
        return float(0.1 * env * std::sin(2 * M_PI * (130 + 20 * std::sin(t * 3)) * t));
    });
    CHECK(!p.telemetry().percussive.load(), "voice flagged as percussive (crest %.1f)", p.telemetry().crest.load());
}

static void testMono() {
    std::printf("mono input works\n");
    const double fs = 44100; Processor p; p.prepare(fs, 1);
    Params pr; pr.target = -20; p.setParams(pr);
    std::vector<float> a(512);
    double t = 0; float last = 0;
    for (int b = 0; b < int(fs * 10 / 512); ++b) {
        for (auto& v : a) { v = lin(-30) * std::sqrt(2.f) * float(std::sin(2 * M_PI * 220 * t)); t += 1 / fs; }
        float* c[1] = {a.data()};
        p.process(c, 1, 512);
        last = p.telemetry().outRms.load();
    }
    CHECK(std::fabs(last + 20.f) < 1.0f, "mono out %.2f", last);
}

int main() {
    testLatency();
    testRideToTarget();
    testStereoLinked();
    testThresholdFreeze();
    testEaseInSilence();
    testRange();
    testCeiling();
    testBypass();
    testHoldAnalyze();
    testDrums();
    testVoiceNotPercussive();
    testMono();
    if (failures) { std::printf("\n%d check(s) failed\n", failures); return 1; }
    std::printf("\nall checks passed\n");
    return 0;
}
