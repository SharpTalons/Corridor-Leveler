// Corridor Leveler - plugin window.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

class CorridorContent;

// The window is designed at 1120 x 630 and scaled as a whole, so it can be resized freely
// (keeping the 16:9 shape) and still look identical at every size.
class CorridorEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit CorridorEditor(CorridorAudioProcessor&);
    ~CorridorEditor() override;

    void resized() override;
    void paint(juce::Graphics&) override;

    static constexpr int designW = 1120, designH = 630;
    void refreshNow() { timerCallback(); }  // used by the snapshot test

private:
    void timerCallback() override;

    CorridorAudioProcessor& proc;
    std::unique_ptr<CorridorContent> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CorridorEditor)
};
