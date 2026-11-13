// Corridor Leveler - JUCE plugin processor.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "PluginEditor.h"

CorridorAudioProcessor::CorridorAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, &undo, "CorridorLeveler", createLayout()) {
    pThreshold = apvts.getRawParameterValue(ids::threshold);
    pTarget = apvts.getRawParameterValue(ids::target);
    pResponse = apvts.getRawParameterValue(ids::response);
    pRange = apvts.getRawParameterValue(ids::range);
    pCeiling = apvts.getRawParameterValue(ids::ceiling);
    pMode = apvts.getRawParameterValue(ids::mode);
    pDetect = apvts.getRawParameterValue(ids::detect);
    pEase = apvts.getRawParameterValue(ids::ease);
    pBypass = apvts.getRawParameterValue(ids::bypass);
    pRise = apvts.getRawParameterValue(ids::rise);
    pFall = apvts.getRawParameterValue(ids::fall);
    pTrim = apvts.getRawParameterValue(ids::trim);
    pAnalyze = apvts.getRawParameterValue(ids::analyze);
    bypassParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(ids::bypass));
    setLatencySamples(dsp.latencySamples());
}

juce::AudioProcessorValueTreeState::ParameterLayout CorridorAudioProcessor::createLayout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto db = [](const char* suffix, int dec) {
        return AudioParameterFloatAttributes().withLabel(suffix).withStringFromValueFunction(
            [dec](float v, int) { return String(v, dec); });
    };
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::threshold, 1}, "Threshold", NormalisableRange<float>(-70.f, -20.f, 0.5f), -48.f, db("dB RMS", 1)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::target, 1}, "Target", NormalisableRange<float>(-40.f, -3.f, 0.5f), -18.f, db("dB", 1)));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ids::detect, 1}, "Target measures", StringArray{"RMS", "Peak"}, 0));
    // times run on a log taper, so the fast end of the dial has room
    auto logRange = [](float lo, float hi) {
        return NormalisableRange<float>(lo, hi,
            [](float a, float b, float n) { return a * std::pow(b / a, n); },
            [](float a, float b, float v) { return std::log(v / a) / std::log(b / a); },
            [](float a, float b, float v) { return jlimit(a, b, float(std::round(v))); });
    };
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::response, 2}, "Response", logRange(10.f, 3000.f), 400.f, db("ms", 0)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::rise, 2}, "Rise", logRange(20.f, 3000.f), 400.f, db("ms", 0)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::fall, 2}, "Fall", logRange(5.f, 2000.f), 80.f, db("ms", 0)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::trim, 2}, "Output trim", NormalisableRange<float>(-12.f, 12.f, 0.1f), 0.f, db("dB", 1)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::range, 1}, "Range", NormalisableRange<float>(0.f, 24.f, 0.5f), 12.f, db("dB", 1)));
    // the ceiling reaches down to -60 dBFS, but half the dial covers -12 to 0, where it is normally set
    NormalisableRange<float> ceilingRange(-60.f, 0.f, 0.1f);
    ceilingRange.setSkewForCentre(-12.f);
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::ceiling, 1}, "Ceiling", ceilingRange, -1.f, db("dBFS", 1)));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ids::mode, 1}, "Mode", StringArray{"Ride", "Hold"}, 0));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::ease, 1}, "Return to 0 dB in silence", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::analyze, 2}, "Analyze / Lock", false));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::bypass, 1}, "Bypass", false));
    return l;
}

void CorridorAudioProcessor::prepareToPlay(double sampleRate, int) {
    // Runs at whatever rate the host (DAW or OBS) is set to.
    dsp.prepare(sampleRate, juce::jmax(1, getTotalNumInputChannels()));
    setLatencySamples(dsp.latencySamples());
}

bool CorridorAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void CorridorAudioProcessor::run(juce::AudioBuffer<float>& buffer, bool forceBypass) {
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear(c, 0, buffer.getNumSamples());

    corridor::Params p;
    p.threshold = pThreshold->load();
    p.target = pTarget->load();
    p.peakDetect = pDetect->load() > 0.5f;
    p.responseMs = pResponse->load();
    p.riseMs = pRise->load();
    p.fallMs = pFall->load();
    p.trimDb = pTrim->load();
    p.range = pRange->load();
    p.ceiling = pCeiling->load();
    p.hold = pMode->load() > 0.5f;
    p.easeInSilence = pEase->load() > 0.5f;
    p.bypass = forceBypass || pBypass->load() > 0.5f;
    // Analyze / Lock as a host parameter: each press (off to on) analyzes, the next one locks
    const bool an = pAnalyze->load() > 0.5f;
    if (an && !lastAnalyze && p.hold) {
        if (dsp.telemetry().analyzing.load()) dsp.requestAnalyzeLock(); else dsp.requestAnalyzeStart();
    }
    lastAnalyze = an;
    dsp.setParams(p);
    dsp.process(buffer.getArrayOfWritePointers(), juce::jmin(buffer.getNumChannels(), getTotalNumInputChannels()), buffer.getNumSamples());
}

void CorridorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { run(buffer, false); }

// Host-side bypass: keep running so the latency stays the same and the switch is click-free.
void CorridorAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { run(buffer, true); }

juce::AudioProcessorEditor* CorridorAudioProcessor::createEditor() { return new CorridorEditor(*this); }

void CorridorAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto st = apvts.copyState();
    const auto& t = dsp.telemetry();
    st.setProperty("holdValid", t.holdValid.load(), nullptr);
    st.setProperty("holdGain", t.holdGain.load(), nullptr);
    st.setProperty("editorWidth", editorWidth, nullptr);
    st.setProperty("presetName", presetName, nullptr);
    st.setProperty("slot", slot, nullptr);
    for (int i = 0; i < 2; ++i) {  // the A / B slots travel with the session
        juce::ValueTree s("Slot" + juce::String(i));
        for (const auto& kv : slots[i]) s.setProperty(kv.name, kv.value, nullptr);
        st.appendChild(s, nullptr);
    }
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, destData);
}

void CorridorAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        auto st = juce::ValueTree::fromXml(*xml);
        if (!st.isValid() || !st.hasType(apvts.state.getType())) return;
        for (int i = 0; i < 2; ++i) {
            slots[i].clear();
            const auto s = st.getChildWithName("Slot" + juce::String(i));
            for (int k = 0; k < s.getNumProperties(); ++k) slots[i].set(s.getPropertyName(k), s.getProperty(s.getPropertyName(k)));
            st.removeChild(s, nullptr);
        }
        presetName = st.getProperty("presetName", "Default").toString();
        slot = st.getProperty("slot", 0);
        apvts.replaceState(st);
        undo.clearUndoHistory();
        editorWidth = st.getProperty("editorWidth", 1120);
        if (st.getProperty("holdValid", false)) dsp.setStoredHoldGain(true, float(st.getProperty("holdGain", 0.0)));
    }
}

// ---------------------------------------------------------------- presets, A / B
static const char* const kValueIds[] = {ids::threshold, ids::target, ids::response, ids::range, ids::ceiling, ids::rise, ids::fall, ids::trim, ids::ease};

const std::vector<CorridorAudioProcessor::Preset>& CorridorAudioProcessor::presets() {
    static const std::vector<Preset> list = {
        //  name            thr    tgt    resp    rng   ceil   rise   fall   trim  return-to-0
        {"Default",        -48.f, -18.f,  400.f, 12.f, -1.0f,  400.f,  80.f, 0.f, true},
        {"Stream voice",   -50.f, -18.f,  250.f, 12.f, -1.0f,  350.f,  50.f, 0.f, false},
        {"Podcast",        -52.f, -19.f,  600.f, 10.f, -1.5f,  700.f, 120.f, 0.f, true},
        {"Music bed",      -45.f, -24.f, 1500.f,  8.f, -1.0f, 1500.f, 600.f, 0.f, false},
        {"Drums",          -40.f, -16.f,  800.f,  6.f, -1.0f,  800.f, 300.f, 0.f, false},
        {"Quiet room",     -58.f, -20.f,  400.f, 18.f, -1.0f,  500.f,  80.f, 0.f, true},
    };
    return list;
}

juce::NamedValueSet CorridorAudioProcessor::captureValues() const {
    juce::NamedValueSet v;
    for (auto* id : kValueIds) v.set(id, apvts.getRawParameterValue(id)->load());
    return v;
}

// Every change goes through the parameters, so the host sees it and Undo can step back over it.
void CorridorAudioProcessor::applyValues(const juce::NamedValueSet& v) {
    for (auto* id : kValueIds)
        if (auto* prm = apvts.getParameter(id); prm != nullptr && v.contains(id)) {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost(prm->convertTo0to1(float(v[id])));
            prm->endChangeGesture();
        }
}

void CorridorAudioProcessor::applyPreset(int index) {
    if (index < 0 || index >= int(presets().size())) return;
    const auto& p = presets()[size_t(index)];
    juce::NamedValueSet v;
    v.set(ids::threshold, p.threshold); v.set(ids::target, p.target); v.set(ids::response, p.responseMs);
    v.set(ids::range, p.range); v.set(ids::ceiling, p.ceiling); v.set(ids::rise, p.riseMs); v.set(ids::fall, p.fallMs);
    v.set(ids::trim, p.trim); v.set(ids::ease, p.ease ? 1.f : 0.f);
    undo.beginNewTransaction("Preset");
    applyValues(v);
    presetName = p.name;
}

bool CorridorAudioProcessor::presetEdited() const {
    for (const auto& p : presets()) {
        if (presetName != p.name) continue;
        const float want[] = {p.threshold, p.target, p.responseMs, p.range, p.ceiling, p.riseMs, p.fallMs, p.trim, p.ease ? 1.f : 0.f};
        for (size_t i = 0; i < std::size(kValueIds); ++i)
            if (std::abs(apvts.getRawParameterValue(kValueIds[i])->load() - want[i]) > 0.051f) return true;
        return false;
    }
    return false;
}

// A / B: two complete settings. The first switch to B copies A; after that each slot keeps its own.
void CorridorAudioProcessor::switchSlot(int to) {
    if (to == slot || to < 0 || to > 1) return;
    undo.beginNewTransaction("A / B");
    slots[slot] = captureValues();
    if (slots[to].isEmpty()) slots[to] = slots[slot];
    else applyValues(slots[to]);
    slot = to;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CorridorAudioProcessor(); }
