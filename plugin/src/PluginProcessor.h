// Corridor Leveler - JUCE plugin processor (VST3, VST2).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "corridor/Processor.h"

namespace ids {
inline constexpr const char* threshold = "threshold";
inline constexpr const char* target = "target";
inline constexpr const char* response = "responseMs";  // ms (10..3000); a new ID, because the old 0..100 scale meant something else
inline constexpr const char* rise = "riseMs";
inline constexpr const char* fall = "fallMs";
inline constexpr const char* trim = "trim";
inline constexpr const char* analyze = "analyze";      // momentary: each press analyzes, the next locks (map it to a hotkey, Stream Deck or MIDI)
inline constexpr const char* range = "range";
inline constexpr const char* ceiling = "ceiling";
inline constexpr const char* mode = "mode";
inline constexpr const char* detect = "detect";        // what the target measures: 0 = RMS, 1 = peak
inline constexpr const char* ease = "ease";
inline constexpr const char* bypass = "bypass";
}  // namespace ids

class CorridorAudioProcessor : public juce::AudioProcessor {
public:
    CorridorAudioProcessor();
    ~CorridorAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Corridor Leveler"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioParameterBool* getBypassParameter() const override { return bypassParam; }

    corridor::Processor& core() { return dsp; }
    juce::AudioProcessorValueTreeState& state() { return apvts; }

    // UI preferences kept with the session
    int editorWidth = 1120;
    bool tipDismissedThisRun = false;

    // undo / redo of every parameter change, presets, and two A / B slots
    juce::UndoManager undo;
    struct Preset { const char* name; float threshold, target, responseMs, range, ceiling, riseMs, fallMs, trim; bool ease; };
    static const std::vector<Preset>& presets();
    juce::String presetName = "Default";
    void applyPreset(int index);
    bool presetEdited() const;
    int slot = 0;                      // 0 = A, 1 = B
    void switchSlot(int to);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void run(juce::AudioBuffer<float>& buffer, bool forceBypass);
    juce::NamedValueSet captureValues() const;
    void applyValues(const juce::NamedValueSet& v);
    juce::NamedValueSet slots[2];
    bool lastAnalyze = false;

    juce::AudioProcessorValueTreeState apvts;
    corridor::Processor dsp;
    juce::AudioParameterBool* bypassParam = nullptr;
    std::atomic<float>* pThreshold = nullptr;
    std::atomic<float>* pTarget = nullptr;
    std::atomic<float>* pResponse = nullptr;
    std::atomic<float>* pRange = nullptr;
    std::atomic<float>* pCeiling = nullptr;
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pDetect = nullptr;
    std::atomic<float>* pEase = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pRise = nullptr;
    std::atomic<float>* pFall = nullptr;
    std::atomic<float>* pTrim = nullptr;
    std::atomic<float>* pAnalyze = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CorridorAudioProcessor)
};
