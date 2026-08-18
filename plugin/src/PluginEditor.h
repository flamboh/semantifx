#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace semantifx
{
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

    void selectBand (int);
    void selectDistortion();
    void beginCapture();
    void beginAnalysis();
    void cancelWork (bool acceptLatestPreview = true);
    void timerCallback() override;
    void updateAnalysisPreview();
    void updateWorkflowState();

    PluginProcessor& pluginProcessor;
    std::unique_ptr<SemanticLookAndFeel> semanticLookAndFeel;
    std::unique_ptr<EqResponseGraph> responseGraph;
    std::array<std::unique_ptr<ParameterControl>, 18> bandControls;
    std::array<std::unique_ptr<ParameterControl>, 4> distortionControls;
    std::unique_ptr<ParameterControl> mixControl;
    std::unique_ptr<ParameterControl> outputControl;
    std::array<juce::TextButton, 6> bandButtons;
    juce::TextButton distortionButton { "DISTORTION" };
    int selectedBand {};
    bool distortionSelected {};

    juce::Label productLabel;
    juce::Label subtitleLabel;
    juce::Label engineBadge;
    juce::ComboBox orderSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> orderAttachment;
    juce::ToggleButton bypassButton { "Hear original" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    juce::TextButton importButton { "Import..." };
    juce::Label importStatus;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Label workflowEyebrow;
    juce::TextEditor promptEditor;
    juce::TextButton captureButton { "Listen 10s" };
    juce::TextButton analyzeButton { "Shape sound" };
    juce::TextButton cancelButton { "Cancel" };
    juce::Label workflowStatus;
    double captureProgressValue {};
    juce::ProgressBar captureProgress { captureProgressValue };
    juce::Label selectedBandLabel;
    juce::Label lastDirectionLabel;
    juce::TooltipWindow tooltipWindow { this, 650 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
} // namespace semantifx
