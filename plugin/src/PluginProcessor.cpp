// Corridor Leveler - JUCE plugin processor.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "PluginEditor.h"

CorridorAudioProcessor::CorridorAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "CorridorLeveler", createLayout()) {
    pThreshold = apvts.getRawParameterValue(ids::threshold);
    pTarget = apvts.getRawParameterValue(ids::target);
    pResponse = apvts.getRawParameterValue(ids::response);
    pRange = apvts.getRawParameterValue(ids::range);
    pCeiling = apvts.getRawParameterValue(ids::ceiling);
    pMode = apvts.getRawParameterValue(ids::mode);
    pEase = apvts.getRawParameterValue(ids::ease);
    pBypass = apvts.getRawParameterValue(ids::bypass);
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
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::target, 1}, "Target", NormalisableRange<float>(-40.f, -6.f, 0.5f), -20.f, db("dB RMS", 1)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::response, 1}, "Response", NormalisableRange<float>(0.f, 100.f, 1.f), 50.f, db("", 0)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::range, 1}, "Range", NormalisableRange<float>(0.f, 24.f, 0.5f), 12.f, db("dB", 1)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{ids::ceiling, 1}, "Ceiling", NormalisableRange<float>(-12.f, 0.f, 0.1f), -1.f, db("dBFS", 1)));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ids::mode, 1}, "Mode", StringArray{"Ride", "Hold"}, 0));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{ids::ease, 1}, "Ease in silence", false));
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
    p.response = pResponse->load();
    p.range = pRange->load();
    p.ceiling = pCeiling->load();
    p.hold = pMode->load() > 0.5f;
    p.easeInSilence = pEase->load() > 0.5f;
    p.bypass = forceBypass || pBypass->load() > 0.5f;
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
    st.setProperty("darkTheme", darkTheme, nullptr);
    st.setProperty("editorWidth", editorWidth, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, destData);
}

void CorridorAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        auto st = juce::ValueTree::fromXml(*xml);
        if (!st.isValid() || !st.hasType(apvts.state.getType())) return;
        apvts.replaceState(st);
        darkTheme = st.getProperty("darkTheme", true);
        editorWidth = st.getProperty("editorWidth", 1120);
        if (st.getProperty("holdValid", false)) dsp.setStoredHoldGain(true, float(st.getProperty("holdGain", 0.0)));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CorridorAudioProcessor(); }
