// Corridor Leveler - shared DSP core.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "corridor/Processor.h"

#include <algorithm>
#include <cmath>

namespace corridor {

namespace {
constexpr float kDbToLin = 0.11512925464970229f;  // ln(10)/20
inline float dbToLin(float db) { return std::exp(db * kDbToLin); }
inline float linToDb(float x) { return x > 1e-10f ? 20.f * std::log10(x) : -200.f; }
inline float msToDb(double ms) { return ms > 1e-13 ? float(10.0 * std::log10(ms)) : -120.f; }
template <typename T> inline T clampv(T v, T a, T b) { return v < a ? a : (v > b ? b : v); }
}  // namespace

const char* stateText(State s) {
    switch (s) {
        case State::Waiting: return "Below the threshold: not adjusting";
        case State::OnTarget: return "On target";
        case State::Boosting: return "Riding up";
        case State::Cutting: return "Riding down";
        case State::Ceiling: return "Boost held back by the ceiling";
        case State::Range: return "Needs more than the range allows";
        case State::Returning: return "Silence: easing back to 0 dB";
        case State::Held: return "Holding a fixed correction";
        case State::Analyzing: return "Analyzing";
        case State::Unset: return "Hold: press Analyze to measure";
    }
    return "";
}

// ---------------------------------------------------------------- meter
void Processor::Meter::reset(int blockLen) {
    block = std::max(1, blockLen);
    n = 0; accSq = 0.0; peak = 0.f; ring.fill(0.0); ri = 0;
    blockMs = 0.0; blockPeak = 0.f; rmsDb = peakDb = holdDb = -120.f; holdCount = 0;
}

bool Processor::Meter::push(double meanSq, float absPeak) {
    accSq += meanSq;
    if (absPeak > peak) peak = absPeak;
    if (++n < block) return false;
    blockMs = accSq / n; blockPeak = peak;
    accSq = 0.0; peak = 0.f; n = 0;
    ring[ri] = blockMs; ri = (ri + 1) % int(ring.size());
    double s = 0.0;
    for (double v : ring) s += v;
    rmsDb = msToDb(s / double(ring.size()));
    const float pd = linToDb(blockPeak);
    peakDb = pd > peakDb ? pd : std::max(-120.f, peakDb - 0.35f);  // ~35 dB/s fall
    if (pd > holdDb) { holdDb = pd; holdCount = 150; }              // 1.5 s hold
    else if (--holdCount < 0) holdDb = std::max(-120.f, holdDb - 0.25f);
    return true;
}

// ---------------------------------------------------------------- processor
Processor::Processor() { prepare(48000.0, 2); }

void Processor::prepare(double sampleRate, int maxChannels) {
    fs_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    channels_ = std::max(1, maxChannels);
    lookahead_ = std::max(1, int(std::lround(fs_ * kLookaheadSeconds)));
    const int block = std::max(1, int(std::lround(fs_ * kBlockSeconds)));
    in_.reset(block); out_.reset(block);

    wetDelay_.assign(size_t(channels_), std::vector<float>(size_t(lookahead_), 0.f));
    dryDelay_.assign(size_t(channels_), std::vector<float>(size_t(lookahead_), 0.f));
    dw_ = 0;
    qcap_ = lookahead_ + 4;
    qv_.assign(size_t(qcap_), 1.f);
    qi_.assign(size_t(qcap_), 0);
    att_ = 1.f - std::exp(-1.f / (float(lookahead_) * 0.25f));
    rel_ = 1.f - std::exp(-1.f / (0.080f * float(fs_)));  // fixed 80 ms release
    bypassStep_ = 1.f / float(std::max(1.0, fs_ * 0.010));  // 10 ms bypass crossfade
    reset();
}

void Processor::reset() {
    in_.reset(in_.block); out_.reset(out_.block);
    for (auto& d : wetDelay_) std::fill(d.begin(), d.end(), 0.f);
    for (auto& d : dryDelay_) std::fill(d.begin(), d.end(), 0.f);
    dw_ = 0; qh_ = qt_ = qsz_ = 0; qn_ = 0; env_ = 1.f; minEnv_ = 1.f; slotPeak_ = 0.f;
    ms_.fill(0.0); pk_.fill(0.f); act_.fill(0); li_ = 0;
    gCur_ = g_; gInc_ = 0.f;
    activeT_ = 0.f; silentT_ = 10.f; crest_ = 10.f; perc_ = false; prevBlockDb_ = -120.f; onsetRate_ = 0.f;
    an_ = Analysis{};
    bypassMix_ = p_.bypass ? 1.f : 0.f;
    blocks_ = 0;
}

int Processor::copyHistory(Snapshot* out, int maxCount) const {
    const int written = histCount_.load(std::memory_order_acquire);
    const int count = std::min({maxCount, written, kHistorySize});
    for (int k = 0; k < count; ++k) out[k] = hist_[size_t((written - count + k) % kHistorySize)];
    return count;
}

void Processor::process(float* const* ch, int numChannels, int numSamples) {
    const int nc = std::min(numChannels, channels_);
    if (nc <= 0 || numSamples <= 0) return;

    // commands from the UI thread
    switch (command_.exchange(0)) {
        case 1: an_ = Analysis{}; an_.on = true; t_.analyzeFailed.store(false); break;
        case 2: if (an_.on) lockAnalysis(); break;
        case 3: in_.holdDb = out_.holdDb = -120.f; break;
        default: break;
    }
    if (pendingHoldSet_.exchange(false)) {
        holdG_ = pendingHold_.load();
        t_.holdValid.store(holdG_ != kNoHold);
        t_.holdGain.store(holdG_ == kNoHold ? 0.f : holdG_);
        if (p_.hold && holdG_ != kNoHold) { g_ = gCur_ = holdG_; gInc_ = 0.f; }
    }

    const float ceil = dbToLin(p_.ceiling);
    const float bypTarget = p_.bypass ? 1.f : 0.f;
    const int dl = lookahead_;  // delay line length = reported latency
    const float invNc = 1.f / float(nc);

    for (int s = 0; s < numSamples; ++s) {
        // ---- input detection (linked across channels)
        double sq = 0.0; float pk = 0.f;
        for (int c = 0; c < nc; ++c) { const float x = ch[c][s]; sq += double(x) * x; pk = std::max(pk, std::fabs(x)); }
        if (in_.push(sq * invNc, pk)) controlBlock();

        gCur_ += gInc_;
        const float gl = dbToLin(gCur_);

        // ---- look-ahead limiter: smallest required gain over the look-ahead window
        float wmax = pk * gl;
        const float req = wmax > ceil ? ceil / wmax : 1.f;
        while (qsz_ > 0) {
            const int b = (qt_ - 1 + qcap_) % qcap_;
            if (qv_[size_t(b)] >= req) { qt_ = b; --qsz_; } else break;
        }
        qv_[size_t(qt_)] = req; qi_[size_t(qt_)] = qn_; qt_ = (qt_ + 1) % qcap_; ++qsz_;
        while (qi_[size_t(qh_)] < qn_ - dl) { qh_ = (qh_ + 1) % qcap_; --qsz_; }  // window: the delayed sample .. now
        ++qn_;
        const float tg = qv_[size_t(qh_)];
        env_ += (tg - env_) * (tg < env_ ? att_ : rel_);
        if (env_ < minEnv_) minEnv_ = env_;

        // ---- bypass crossfade (both paths delayed equally, so latency never changes)
        if (bypassMix_ != bypTarget)
            bypassMix_ = bypTarget > bypassMix_ ? std::min(bypTarget, bypassMix_ + bypassStep_) : std::max(bypTarget, bypassMix_ - bypassStep_);
        const float b = bypassMix_;

        double osq = 0.0; float opk = 0.f;
        for (int c = 0; c < nc; ++c) {
            const float x = ch[c][s];
            auto& wd = wetDelay_[size_t(c)];
            auto& dd = dryDelay_[size_t(c)];
            const float wet = wd[size_t(dw_)], dry = dd[size_t(dw_)];
            wd[size_t(dw_)] = x * gl; dd[size_t(dw_)] = x;
            float y = clampv(wet * env_, -ceil, ceil);
            if (b > 0.f) y = y * (1.f - b) + dry * b;
            ch[c][s] = y;
            osq += double(y) * y; opk = std::max(opk, std::fabs(y));
        }
        dw_ = (dw_ + 1) % dl;
        out_.push(osq * invNc, opk);
    }
    // channels beyond what was prepared are passed through untouched
}

void Processor::controlBlock() {
    const float dt = float(in_.block / fs_);
    const int i = li_;
    ms_[size_t(i)] = in_.blockMs; pk_[size_t(i)] = in_.blockPeak;
    const float sl = in_.rmsDb;
    const bool active = sl >= p_.threshold;
    const float blockDb = msToDb(in_.blockMs);
    // Only blocks that are themselves above the threshold count towards the level, so the fading
    // tail of a sentence (or a drum decay) never drags the reading down and causes a boost.
    act_[size_t(i)] = (active && blockDb >= p_.threshold) ? 1 : 0;
    const bool onset = active && blockDb - prevBlockDb_ > 15.f;  // sharp attack within 10 ms
    prevBlockDb_ = blockDb;
    onsetRate_ += ((onset ? 100.f : 0.f) - onsetRate_) * 0.005f;  // onsets per second, ~2 s smoothing

    float pmax = 0.f;
    for (int k = 0; k < 100; ++k) pmax = std::max(pmax, pk_[size_t((i - k + N) % N)]);
    const float pk1 = linToDb(pmax);

    // Percussive material (drums): high crest factor (peak over RMS) and frequent sharp attacks.
    if (active) crest_ += ((pk1 - sl) - crest_) * 0.01f;
    perc_ = crest_ > 12.f && onsetRate_ > 1.0f;

    // Response sets the RMS window: 1.5 s (slow) .. 0.3 s (fast).
    // Percussive material averages at least 0.8 s, so the gain follows the groove, not each hit.
    int win = int(std::lround(150.0 - 1.2 * double(p_.response)));
    win = clampv(win, 30, 150);
    if (perc_) win = std::max(win, 80);
    double sum = 0.0; int count = 0;
    for (int k = 0; k < win; ++k) {
        const int j = (i - k + N) % N;
        if (act_[size_t(j)]) { ++count; sum += ms_[size_t(j)]; }
    }
    const bool lValid = count >= std::min(10, win);
    const float L = lValid ? msToDb(sum / count) : -120.f;
    li_ = (i + 1) % N;

    // Analyze (Hold mode)
    if (an_.on) {
        an_.t += dt;
        if (active) { an_.sum += in_.blockMs; ++an_.count; an_.pk = std::max(an_.pk, in_.blockPeak); }
        if (an_.t >= 20.0) lockAnalysis();
    }

    const float sc = 0.15f * std::pow(40.f, p_.response / 100.f);
    const float g = g_;
    float target = g, rate = 0.f;
    State st;
    if (p_.hold) {
        if (holdG_ == kNoHold) holdG_ = clampv(g, -p_.range, p_.range);  // entering Hold keeps the current gain
        target = holdG_; rate = 12.f;
        st = an_.on ? State::Analyzing : (t_.holdValid.load() ? State::Held : State::Unset);
    } else if (!active) {
        activeT_ = 0.f; silentT_ += dt;
        if (p_.easeInSilence && silentT_ > float(kEaseDelaySeconds) && std::fabs(g) > 0.01f) {
            target = 0.f; rate = 3.f * std::max(0.5f, sc); st = State::Returning;
        } else {
            st = State::Waiting;
        }
    } else {
        activeT_ += dt; silentT_ = 0.f;
        if (!lValid || activeT_ < 0.15f) {
            st = State::Waiting;
        } else {
            const float outLvl = L + g, d = std::fabs(outLvl - p_.target);
            // within 0.75 dB of the target it holds still, so it never fidgets
            target = d < 0.75f ? g : p_.target - L;
            rate = clampv(1.5f + 0.8f * d, 1.5f, 12.f) * sc * (perc_ ? 0.7f : 1.f);
            st = d < 0.75f ? State::OnTarget : (outLvl < p_.target ? State::Boosting : State::Cutting);
        }
        // ceiling: never boost peaks past it
        const float gC = p_.ceiling - pk1;
        if (target > gC) {
            target = gC;
            if (st == State::Boosting || st == State::OnTarget || g > gC + 0.1f) st = State::Ceiling;
            if (g > gC) rate = std::max(rate, 30.f);
        }
        if ((target > p_.range || target < -p_.range) && (st == State::Boosting || st == State::Cutting)) st = State::Range;
    }
    target = clampv(target, -p_.range, p_.range);
    const float step = rate * dt;
    g_ = target > g ? std::min(target, g + step) : std::max(target, g - step);
    if (g_ > p_.range) g_ = std::max(p_.range, g - 12.f * dt);   // Range turned down mid-boost
    if (g_ < -p_.range) g_ = std::min(-p_.range, g + 12.f * dt);
    gInc_ = (g_ - gCur_) / float(in_.block);

    // telemetry
    t_.inRms.store(in_.rmsDb); t_.outRms.store(out_.rmsDb);
    t_.inPeak.store(in_.peakDb); t_.inPeakHold.store(in_.holdDb);
    t_.outPeak.store(out_.peakDb); t_.outPeakHold.store(out_.holdDb);
    t_.gain.store(p_.bypass ? 0.f : g_);
    t_.level.store(L); t_.levelValid.store(lValid);
    t_.crest.store(crest_); t_.percussive.store(perc_);
    t_.state.store(int(st));
    t_.analyzing.store(an_.on);
    t_.analyzeSeconds.store(float(an_.t)); t_.analyzeActiveSeconds.store(float(an_.count) * dt);
    t_.holdGain.store(holdG_ == kNoHold ? 0.f : holdG_);

    // history every 20 ms
    slotPeak_ = std::max(slotPeak_, in_.blockPeak);
    if (++blocks_ % 2 == 0) {
        const int w = histCount_.load(std::memory_order_relaxed);
        hist_[size_t(w % kHistorySize)] = Snapshot{in_.rmsDb, out_.rmsDb, p_.bypass ? 0.f : g_, p_.bypass ? 0.f : linToDb(minEnv_), linToDb(slotPeak_), an_.on};
        slotPeak_ = 0.f;
        histCount_.store(w + 1, std::memory_order_release);
        t_.limiterDb.store(linToDb(minEnv_));
        minEnv_ = 1.f;
    }
}

void Processor::lockAnalysis() {
    const Analysis a = an_;
    an_ = Analysis{};
    const float dt = float(in_.block / fs_);
    if (a.count < int(1.0f / dt)) {  // needs at least 1 s of sound above the threshold
        t_.analyzeFailed.store(true);
        return;
    }
    const float measured = msToDb(a.sum / a.count);
    float hg = p_.target - measured;
    HoldLimit lim = HoldLimit::None;
    const float room = p_.ceiling - linToDb(a.pk);
    if (hg > room) { hg = room; lim = HoldLimit::Ceiling; }
    if (std::fabs(hg) > p_.range) { hg = clampv(hg, -p_.range, p_.range); lim = HoldLimit::Range; }
    holdG_ = hg;
    t_.holdValid.store(true);
    t_.holdGain.store(hg);
    t_.holdMeasured.store(measured);
    t_.holdSeconds.store(float(a.count) * dt);
    t_.holdLimit.store(int(lim));
    t_.analyzeFailed.store(false);
}

}  // namespace corridor
