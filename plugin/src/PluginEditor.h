#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <optional>

namespace semantifx
{
class CardRack;
class EqResponseGraph;
class SemanticLookAndFeel;

class PluginEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct ParameterControl
    {
        ParameterControl (PluginEditor&, bool restartAnalysisOnDragEnd);
        void bind (const juce::String& parameterId, const juce::String& displayName,
                   const juce::String& suffix);
        void clear();

        PluginEditor& editor;
        bool restartAnalysisOnDragEnd {};
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void selectSlot (std::optional<slots::Slot>);
    void refreshSelectedControls();
    void beginCapture();
    void beginShape();
    void beginOptimize();
    void stopWork();
    void revertWork();
    void showTransientStatus (const juce::String&, bool isError);
    void timerCallback() override;
    void updateWorkflowState();

    PluginProcessor& pluginProcessor;
    std::unique_ptr<SemanticLookAndFeel> semanticLookAndFeel;
    std::unique_ptr<CardRack> cardRack;
    std::unique_ptr<EqResponseGraph> responseGraph;
    std::array<std::unique_ptr<ParameterControl>, PluginProcessor::maxParametersPerType> controls;
    std::unique_ptr<ParameterControl> mixControl;
    std::unique_ptr<ParameterControl> outputControl;
    std::optional<slots::Slot> selectedSlot;
    int selectedBand {};

    juce::TextButton importButton { "Import" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::TextEditor promptEditor;
    juce::TextButton captureButton { "Listen 10s" };
    juce::TextButton shapeButton { "Shape sound" };
    juce::TextButton optimizeButton { "Optimize chain" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton revertButton { "Revert" };
    juce::Label workflowStatus;
    juce::Label emptyHint;
    juce::String transientStatus;
    bool transientStatusIsError {};
    juce::int64 transientStatusExpiry {};
    juce::TooltipWindow tooltipWindow { this, 650 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
} // namespace semantifx
