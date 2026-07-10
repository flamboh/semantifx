#include "PluginEditor.h"

namespace semantifx
{
PluginEditor::Knob::Knob (PluginEditor& editor, juce::AudioProcessorValueTreeState& state,
                          const char* parameterID, const juce::String& displayName,
                          const juce::String& suffix)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 88, 22);
    slider.setTextValueSuffix (suffix);
    slider.setName (displayName);
    slider.setTitle (displayName);
    slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour { 0xff73d2de });
    slider.setColour (juce::Slider::thumbColourId, juce::Colour { 0xffffc857 });

    label.setText (displayName, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setInterceptsMouseClicks (false, false);

    editor.addAndMakeVisible (slider);
    editor.addAndMakeVisible (label);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterID, slider);
}

PluginEditor::PluginEditor (PluginProcessor& owner)
    : AudioProcessorEditor (&owner), pluginProcessor (owner)
{
    auto add = [this] (const char* id, const juce::String& name, const juce::String& suffix)
    {
        knobs.push_back (std::make_unique<Knob> (*this, pluginProcessor.parameters, id, name, suffix));
    };

    const std::array<juce::String, 6> names { "Low Shelf", "Low Mid", "Mid", "High Mid", "Air", "High Shelf" };
    for (size_t index = 0; index < parameter::bands.size(); ++index)
    {
        add (parameter::bands[index].gain, names[index] + " Gain", " dB");
        add (parameter::bands[index].frequency, names[index] + " Freq", " Hz");
        add (parameter::bands[index].q, names[index] + " Q", "");
    }
    add (parameter::outputGain, "Output", " dB");
    add (parameter::mix, "Mix", " %");

    bypassButton.setColour (juce::ToggleButton::tickColourId, juce::Colour { 0xffffc857 });
    bypassButton.setTitle ("Smoothly bypass EQ processing");
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        pluginProcessor.parameters, parameter::bypass, bypassButton);

    importButton.setTitle ("Import a SemantiFX protocol-v1 analysis result");
    importButton.onClick = [safeThis = juce::Component::SafePointer<PluginEditor> { this }]
    {
        if (safeThis == nullptr)
            return;

        safeThis->fileChooser = std::make_unique<juce::FileChooser> (
            "Import SemantiFX analysis result", juce::File {}, "*.json");
        safeThis->importButton.setEnabled (false);
        safeThis->fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis] (const juce::FileChooser& chooser)
            {
                if (safeThis == nullptr)
                    return;

                const auto file = chooser.getResult();
                if (file == juce::File {})
                {
                    safeThis->importStatus.setText ("Import cancelled", juce::dontSendNotification);
                    safeThis->importStatus.setColour (juce::Label::textColourId, juce::Colour { 0xff8da6aa });
                }
                else
                {
                    const auto result = safeThis->pluginProcessor.applyAnalysisResultJson (file.loadFileAsString());
                    safeThis->importStatus.setText (result.wasOk() ? "Analysis imported" : result.getErrorMessage(),
                                                    juce::dontSendNotification);
                    safeThis->importStatus.setColour (juce::Label::textColourId,
                                                      result.wasOk() ? juce::Colour { 0xff73d2de }
                                                                     : juce::Colour { 0xffff7b72 });
                }
                safeThis->importButton.setEnabled (true);

                juce::MessageManager::callAsync ([safeThis]
                {
                    if (safeThis != nullptr)
                        safeThis->fileChooser.reset();
                });
            });
    };
    addAndMakeVisible (importButton);

    importStatus.setJustificationType (juce::Justification::centredRight);
    importStatus.setColour (juce::Label::textColourId, juce::Colour { 0xff8da6aa });
    addAndMakeVisible (importStatus);

    setResizable (true, true);
    setResizeLimits (720, 560, 1400, 1000);
    setSize (1080, 720);
}

void PluginEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (juce::Colour { 0xff111820 });
    graphics.setColour (juce::Colour { 0xffeef5f6 });
    graphics.setFont (juce::FontOptions { 28.0f, juce::Font::bold });
    graphics.drawText ("SEMANTIFX", 26, 16, 250, 38, juce::Justification::centredLeft);
    graphics.setColour (juce::Colour { 0xff8da6aa });
    graphics.setFont (juce::FontOptions { 14.0f });
    graphics.drawText ("A small, editable semantic-EQ foundation", 28, 50, 380, 24,
                       juce::Justification::centredLeft);

    graphics.setColour (juce::Colour { 0xff26343d });
    graphics.drawHorizontalLine (82, 24.0f, static_cast<float> (getWidth() - 24));
}

void PluginEditor::resized()
{
    bypassButton.setBounds (getWidth() - 120, 25, 94, 34);
    importButton.setBounds (getWidth() - 300, 25, 160, 34);
    importStatus.setBounds (getWidth() - 500, 55, 470, 24);

    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (78);
    constexpr int columns = 6;
    const auto cellWidth = area.getWidth() / columns;
    const auto rows = (static_cast<int> (knobs.size()) + columns - 1) / columns;
    const auto cellHeight = area.getHeight() / rows;

    for (size_t index = 0; index < knobs.size(); ++index)
    {
        const auto row = static_cast<int> (index) / columns;
        const auto column = static_cast<int> (index) % columns;
        auto cell = juce::Rectangle<int> { area.getX() + column * cellWidth,
                                          area.getY() + row * cellHeight,
                                          cellWidth, cellHeight }.reduced (6);
        knobs[index]->label.setBounds (cell.removeFromTop (25));
        knobs[index]->slider.setBounds (cell);
    }
}
} // namespace semantifx
