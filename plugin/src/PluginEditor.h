#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace semantifx
{
class PluginEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        Knob (PluginEditor&, juce::AudioProcessorValueTreeState&, const char* parameterID,
              const juce::String& displayName, const juce::String& suffix);

        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    PluginProcessor& pluginProcessor;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::ToggleButton bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    juce::TextButton importButton { "Import Analysis..." };
    juce::Label importStatus;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::TextEditor promptEditor;
    juce::TextButton captureButton { "Capture 10s" };
    juce::TextButton analyzeButton { "Analyze" };
    juce::TextButton cancelButton { "Cancel" };
    juce::Label developmentLabel;
    juce::Label developmentStatus;
    double captureProgressValue {};
    juce::ProgressBar captureProgress { captureProgressValue };

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
} // namespace semantifx
