// Corridor Leveler - shared DSP core.
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Framework-free: the engine behind the VST3 and VST2 builds (JUCE). Works at whatever sample rate the host runs,
// with mono or stereo (any channel count; detection is linked across channels).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <vector>

namespace corridor {

constexpr double kLookaheadSeconds = 0.005;  // limiter look-ahead = plugin latency
constexpr double kBlockSeconds = 0.010;      // control-rate block
constexpr double kEaseDelaySeconds = 2.0;    // silence before "ease back" starts
constexpr int kHistorySize = 1024;           // UI history ring (one entry per 20 ms)
constexpr float kPeakLimitDb = 6.f;          // how far boosts may push short peaks into the limiter to reach the target

struct Params {
    float target = -18.f;        // dB RMS, or dBFS peak when peakDetect is on
    bool peakDetect = false;     // false: the target is an RMS level; true: it is the peak level
    float threshold = -48.f;     // dB RMS: below this, no adjusting
    float responseMs = 400.f;    // how long it listens before judging the level: 10 (very fast) .. 3000 ms (slow)
    float riseMs = 400.f;        // how fast the gain lifts quiet passages (time constant)
    float fallMs = 80.f;         // how fast the gain pulls loud passages down (time constant)
    float trimDb = 0.f;          // output trim after the leveler, before the ceiling limiter
    float range = 12.f;          // max boost and cut, dB
    float ceiling = -1.f;        // dBFS peak
    bool hold = false;           // false = Ride, true = Hold
    bool easeInSilence = true;   // glide gain back to 0 dB during long silences
    bool bypass = false;
};

enum class State : int {
    Waiting = 0,   // below threshold / just started: not adjusting
    OnTarget,      // within the dead band around the target
    Boosting,
    Cutting,
    Ceiling,       // boost held back by the ceiling
    Range,         // needs more than Range allows
    Returning,     // easing back to 0 dB during silence
    Held,          // Hold mode with a measured correction
    Analyzing,     // Hold mode, collecting
    Unset          // Hold mode, nothing analyzed yet
};

enum class HoldLimit : int { None = 0, Ceiling = 1, Range = 2 };

struct Snapshot {
    float inRms;      // dB RMS (300 ms)
    float outRms;     // dB RMS (300 ms)
    float gain;       // dB applied by the leveler
    float limiterDb;  // most gain reduction by the limiter in this slot (<= 0)
    float inPeak;     // dBFS, highest input sample in this slot (for the waveform view)
    float inPk;       // dBFS peak (300 ms), the peak-mode counterpart of inRms
    float outPk;      // dBFS peak (300 ms)
    bool analyzing;   // Hold mode was measuring during this slot
};

// Everything the UI shows. Written by the audio thread, read by any thread.
struct Telemetry {
    std::atomic<float> inPeak{-120.f}, inPeakHold{-120.f}, outPeak{-120.f}, outPeakHold{-120.f};
    std::atomic<float> inRms{-120.f}, outRms{-120.f};
    std::atomic<float> inPk{-120.f}, outPk{-120.f};  // highest peak over the last 300 ms (what Peak detection targets)
    // per channel (left, right; mono shows the same on both) and balance in dB, + = left louder
    std::atomic<float> inPeakL{-120.f}, inPeakR{-120.f}, outPeakL{-120.f}, outPeakR{-120.f};
    std::atomic<float> inBalance{0.f}, outBalance{0.f};
    std::atomic<float> gain{0.f}, level{-120.f}, limiterDb{0.f}, crest{10.f};
    std::atomic<int> state{0};
    std::atomic<bool> percussive{false};
    std::atomic<bool> levelValid{false};
    // Hold / Analyze
    std::atomic<bool> analyzing{false};
    std::atomic<float> analyzeSeconds{0.f}, analyzeActiveSeconds{0.f};
    std::atomic<bool> holdValid{false};
    std::atomic<float> holdGain{0.f}, holdMeasured{0.f}, holdSeconds{0.f};
    std::atomic<int> holdLimit{0};
    std::atomic<bool> analyzeFailed{false};
};

class Processor {
public:
    Processor();

    // Call before processing and whenever the sample rate or channel count changes.
    void prepare(double sampleRate, int maxChannels);
    void reset();

    int latencySamples() const { return lookahead_; }
    double sampleRate() const { return fs_; }

    // Audio thread: apply the current parameters, then process in place.
    void setParams(const Params& p) { p_ = p; }
    void process(float* const* channels, int numChannels, int numSamples);

    // Any thread.
    void requestAnalyzeStart() { command_.store(1); }
    void requestAnalyzeLock() { command_.store(2); }
    void resetPeakHolds() { command_.store(3); }
    // Restore a saved Hold correction (e.g. from plugin state). Any thread.
    void setStoredHoldGain(bool valid, float db) { pendingHold_.store(valid ? db : kNoHold); pendingHoldSet_.store(true); }

    const Telemetry& telemetry() const { return t_; }
    // Copies up to maxCount most recent snapshots, oldest first. Returns the count.
    int copyHistory(Snapshot* out, int maxCount) const;
    int historyWritten() const { return histCount_.load(std::memory_order_acquire); }  // goes up by one per entry

    static constexpr float kNoHold = -1000.f;

private:
    struct Meter {
        int block = 480, n = 0;
        double accSq = 0.0;      // mean square accumulated (already averaged over channels)
        float peak = 0.f;
        std::array<double, 30> ring{};  // 300 ms of block mean squares
        std::array<float, 30> pring{};  // and of block peaks
        int ri = 0;
        // results of the last block
        double blockMs = 0.0;
        float blockPeak = 0.f;
        float rmsDb = -120.f, peakDb = -120.f, holdDb = -120.f, pk300Db = -120.f;
        int holdCount = 0;
        void reset(int blockLen);
        bool push(double meanSq, float absPeak);
    };

    void controlBlock();
    struct ChannelMeter {  // left / right peaks and balance for the meters
        double sq[2]{}; float pk[2]{}; float db[2]{-120.f, -120.f}; float bal = 0.f;
        void add(int c, float x) { sq[c] += double(x) * x; pk[c] = std::max(pk[c], std::fabs(x)); }
        void finish(int n, bool mono);
    };
    ChannelMeter inCh_, outCh_;
    void lockAnalysis();

    double fs_ = 48000.0;
    int channels_ = 2;
    int lookahead_ = 240;
    Params p_;

    Meter in_, out_;

    // leveler
    static constexpr int N = 300;
    std::array<double, N> ms_{};
    std::array<float, N> pk_{};
    std::array<uint8_t, N> act_{};
    int li_ = 0;
    float g_ = 0.f, gCur_ = 0.f, gInc_ = 0.f;
    float activeT_ = 0.f, silentT_ = 10.f, crest_ = 10.f, prevBlockDb_ = -120.f, onsetRate_ = 0.f;
    bool perc_ = false;
    bool correcting_ = true;  // moving towards the target; stops once it is reached, starts again past the dead band
    float holdG_ = kNoHold;
    struct Analysis { bool on = false; double t = 0, sum = 0; int count = 0; float pk = 0.f; } an_;

    // limiter (stereo-linked look-ahead)
    std::vector<std::vector<float>> wetDelay_, dryDelay_;
    int dw_ = 0;
    std::vector<float> qv_;
    std::vector<int64_t> qi_;
    int qh_ = 0, qt_ = 0, qsz_ = 0, qcap_ = 0;
    int64_t qn_ = 0;
    float env_ = 1.f, att_ = 0.f, rel_ = 0.f, minEnv_ = 1.f, slotPeak_ = 0.f;

    // bypass crossfade
    float bypassMix_ = 0.f, bypassStep_ = 0.f;

    // history
    std::array<Snapshot, kHistorySize> hist_{};
    std::atomic<int> histCount_{0};
    int blocks_ = 0;

    std::atomic<int> command_{0};
    std::atomic<float> pendingHold_{kNoHold};
    std::atomic<bool> pendingHoldSet_{false};

    Telemetry t_;
};

const char* stateText(State s);

}  // namespace corridor
