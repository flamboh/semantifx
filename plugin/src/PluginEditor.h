#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace semantifx
{
class ChainRack;
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
        ParameterControl (PluginEditor&, juce::AudioProcessorValueTreeState&, const char* parameterID,
                          const juce::String& displayName, const juce::String& suffix);

        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    enum class Module { eq, distortion };

    void showModule (Module);
    void selectBand (int);
    void beginCapture();
    void beginAnalysis();
    void cancelWork (bool acceptLatestPreview = true);
    void showTransientStatus (const juce::String&, bool isError);
    void timerCallback() override;
    void updateAnalysisPreview();
    void updateWorkflowState();

    PluginProcessor& pluginProcessor;
    std::unique_ptr<SemanticLookAndFeel> semanticLookAndFeel;
    std::unique_ptr<ChainRack> chainRack;
    std::unique_ptr<EqResponseGraph> responseGraph;
    std::array<std::unique_ptr<ParameterControl>, 18> bandControls;
    std::array<std::unique_ptr<ParameterControl>, 4> distortionControls;
    std::unique_ptr<ParameterControl> mixControl;
    std::unique_ptr<ParameterControl> outputControl;
    Module visibleModule { Module::eq };
    int selectedBand {};

    juce::TextButton importButton { "Import" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::TextEditor promptEditor;
    juce::TextButton captureButton { "Listen 10s" };
    juce::TextButton analyzeButton { "Shape sound" };
    juce::TextButton cancelButton { "Cancel" };
    juce::Label workflowStatus;
    double captureProgressValue {};
    juce::String transientStatus;
    bool transientStatusIsError {};
    juce::int64 transientStatusExpiry {};
    juce::TooltipWindow tooltipWindow { this, 650 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
} // namespace semantifx
