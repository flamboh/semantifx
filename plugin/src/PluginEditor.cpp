#include "PluginEditor.h"
#include "EqGraphMath.h"

#include <cmath>
#include <functional>

namespace semantifx
{
namespace palette
{
const auto background = juce::Colour { 0xff0b1116 };
const auto panel = juce::Colour { 0xff121b22 };
const auto border = juce::Colour { 0xff26343d };
const auto text = juce::Colour { 0xffeaf1f1 };
const auto muted = juce::Colour { 0xff8da1aa };
const auto primary = juce::Colour { 0xffb7ef78 };
const auto danger = juce::Colour { 0xffff7b72 };
const std::array<juce::Colour, 6> bands {
    juce::Colour { 0xff4dd6b8 }, juce::Colour { 0xff58cfe8 }, juce::Colour { 0xff6f91f2 },
    juce::Colour { 0xffaa7fea }, juce::Colour { 0xffff817a }, juce::Colour { 0xffffbd66 }
};
} // namespace palette

class SemanticLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SemanticLookAndFeel()
    {
        setColour (juce::TextButton::textColourOffId, palette::text);
        setColour (juce::TextButton::textColourOnId, palette::background);
        setColour (juce::TextEditor::backgroundColourId, palette::background);
        setColour (juce::TextEditor::outlineColourId, palette::border);
        setColour (juce::TextEditor::focusedOutlineColourId, palette::primary);
        setColour (juce::TextEditor::textColourId, palette::text);
        setColour (juce::TextEditor::highlightColourId, palette::primary.withAlpha (0.35f));
        setColour (juce::Slider::textBoxTextColourId, palette::text);
        setColour (juce::Slider::textBoxBackgroundColourId, palette::background);
        setColour (juce::Slider::textBoxOutlineColourId, palette::border);
        setColour (juce::TooltipWindow::backgroundColourId, palette::panel);
        setColour (juce::TooltipWindow::textColourId, palette::text);
        setColour (juce::TooltipWindow::outlineColourId, palette::border);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                               bool highlighted, bool down) override
    {
        auto fill = colour;
        if (highlighted) fill = fill.brighter (0.12f);
        if (down) fill = fill.darker (0.14f);
        g.setColour (fill);
        g.fillRoundedRectangle (button.getLocalBounds().toFloat(), 7.0f);
        if (button.hasKeyboardFocus (true))
        {
            g.setColour (palette::primary);
            g.drawRoundedRectangle (button.getLocalBounds().toFloat().reduced (1.0f), 7.0f, 2.0f);
        }
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                     static_cast<float> (width), static_cast<float> (height)).reduced (7.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle = startAngle + position * (endAngle - startAngle);
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (palette::border);
        g.strokePath (arc, juce::PathStrokeType { 5.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded });
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (valueArc, juce::PathStrokeType { 5.0f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded });
        const auto pointer = juce::Point<float> { centre.x + std::sin (angle) * radius,
                                                  centre.y - std::cos (angle) * radius };
        g.setColour (palette::text);
        g.fillEllipse (juce::Rectangle<float> { 7.0f, 7.0f }.withCentre (pointer));
    }
};

class EqResponseGraph final : public juce::Component
{
public:
    class Node final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        Node (EqResponseGraph& graph, int nodeIndex) : owner (graph), index (nodeIndex)
        {
            setWantsKeyboardFocus (true);
            setTitle ("EQ band " + juce::String { index + 1 });
            setDescription ("Drag to change frequency and gain; use arrow keys for fine adjustment");
            setTooltip ("Band " + juce::String { index + 1 } + ": drag frequency and gain");
        }

        void paint (juce::Graphics& g) override
        {
            auto circle = getLocalBounds().toFloat().reduced (owner.selectedBand == index ? 4.0f : 6.0f);
            if (owner.selectedBand == index || hasKeyboardFocus (true))
            {
                g.setColour (palette::text);
                g.drawEllipse (circle.expanded (3.0f), 2.0f);
            }
            g.setColour (palette::bands[static_cast<size_t> (index)]);
            g.fillEllipse (circle);
            g.setColour (palette::background);
            g.setFont (juce::FontOptions { 13.0f, juce::Font::bold });
            g.drawText (juce::String { index + 1 }, getLocalBounds(), juce::Justification::centred);
        }

        void mouseDown (const juce::MouseEvent& event) override
        {
            juce::ignoreUnused (event);
            owner.select (index);
            owner.beginDrag (index);
        }
        void mouseDrag (const juce::MouseEvent& event) override
        {
            owner.drag (index, event.getEventRelativeTo (&owner).position);
        }
        void mouseUp (const juce::MouseEvent&) override { owner.endDrag (index); }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::leftKey) return owner.nudge (index, -1, 0);
            if (key == juce::KeyPress::rightKey) return owner.nudge (index, 1, 0);
            if (key == juce::KeyPress::upKey) return owner.nudge (index, 0, 1);
            if (key == juce::KeyPress::downKey) return owner.nudge (index, 0, -1);
            return false;
        }

    private:
        EqResponseGraph& owner;
        int index;
    };

    EqResponseGraph (PluginProcessor& processor, std::function<void (int)> selectionChanged)
        : owner (processor), onSelectionChanged (std::move (selectionChanged))
    {
        setTitle ("Interactive EQ response");
        setDescription ("Six-band response from 20 hertz to 20 kilohertz and minus 24 to plus 24 decibels");
        for (size_t index = 0; index < nodes.size(); ++index)
        {
            nodes[index] = std::make_unique<Node> (*this, static_cast<int> (index));
            addAndMakeVisible (*nodes[index]);
        }
    }

    void setSelectedBand (int index)
    {
        selectedBand = juce::jlimit (0, 5, index);
        for (auto& node : nodes) node->repaint();
    }

    void refresh()
    {
        updateNodePositions();
        repaint();
    }

    void resized() override { updateNodePositions(); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (palette::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
        g.setColour (palette::border);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 10.0f, 1.0f);

        const auto plot = plotBounds();
        constexpr std::array<double, 10> frequencies { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
        g.setFont (juce::FontOptions { 10.5f });
        for (size_t frequencyIndex = 0; frequencyIndex < frequencies.size(); ++frequencyIndex)
        {
            const auto frequency = frequencies[frequencyIndex];
            const auto x = static_cast<float> (plot.getX() + eq::graph::frequencyToUnit (frequency) * plot.getWidth());
            g.setColour (palette::border.withAlpha (frequencyIndex == 0 || frequencyIndex == frequencies.size() - 1
                                                        ? 0.9f : 0.58f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (palette::muted);
            const auto label = frequency >= 1000.0 ? juce::String { frequency / 1000.0, frequency < 10000 ? 1 : 0 } + "k"
                                                   : juce::String { static_cast<int> (frequency) };
            g.drawText (label, juce::roundToInt (x) - 18, juce::roundToInt (plot.getBottom()) + 5, 36, 15,
                        juce::Justification::centred);
        }
        for (int db = -24; db <= 24; db += 6)
        {
            const auto y = static_cast<float> (plot.getY() + eq::graph::gainToUnit (db) * plot.getHeight());
            g.setColour (db == 0 ? palette::muted.withAlpha (0.7f) : palette::border.withAlpha (0.58f));
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (palette::muted);
            g.drawText ((db > 0 ? "+" : "") + juce::String { db }, 4, juce::roundToInt (y) - 8, 34, 16,
                        juce::Justification::centredRight);
        }

        const auto bands = readBands();
        const auto sampleRate = owner.getSampleRate() > 0.0 ? owner.getSampleRate() : 48000.0;
        juce::Path fill, curve;
        constexpr int points = 320;
        for (int point = 0; point < points; ++point)
        {
            const auto unit = static_cast<double> (point) / static_cast<double> (points - 1);
            const auto frequency = eq::graph::unitToFrequency (unit);
            const auto db = juce::jlimit (-24.0, 24.0,
                                          eq::graph::combinedMagnitudeDb (bands, frequency, sampleRate));
            const auto x = static_cast<float> (plot.getX() + unit * plot.getWidth());
            const auto y = static_cast<float> (plot.getY() + eq::graph::gainToUnit (db) * plot.getHeight());
            if (point == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        fill = curve;
        fill.lineTo (plot.getRight(), plot.getCentreY());
        fill.lineTo (plot.getX(), plot.getCentreY());
        fill.closeSubPath();
        g.setColour (palette::primary.withAlpha (0.08f));
        g.fillPath (fill);
        g.setColour (palette::primary);
        g.strokePath (curve, juce::PathStrokeType { 2.5f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded });

        g.setColour (palette::muted);
        g.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
        g.drawText ("COMBINED RESPONSE", juce::roundToInt (plot.getX()) + 9,
                    juce::roundToInt (plot.getY()) + 7, 170, 18,
                    juce::Justification::centredLeft);
    }

private:
    juce::Rectangle<float> plotBounds() const
    {
        return getLocalBounds().toFloat().reduced (12.0f).withTrimmedLeft (33.0f).withTrimmedBottom (24.0f);
    }

    std::array<eq::graph::Band, 6> readBands() const
    {
        std::array<eq::graph::Band, 6> result;
        for (size_t index = 0; index < result.size(); ++index)
        {
            const auto ids = parameter::bands[index];
            result[index] = { owner.parameters.getRawParameterValue (ids.gain)->load(),
                              owner.parameters.getRawParameterValue (ids.frequency)->load(),
                              owner.parameters.getRawParameterValue (ids.q)->load() };
        }
        return result;
    }

    void updateNodePositions()
    {
        const auto plot = plotBounds();
        if (plot.isEmpty()) return;
        const auto bands = readBands();
        for (size_t index = 0; index < nodes.size(); ++index)
        {
            const auto x = plot.getX() + static_cast<float> (eq::graph::frequencyToUnit (bands[index].frequency)) * plot.getWidth();
            const auto y = plot.getY() + static_cast<float> (eq::graph::gainToUnit (bands[index].gainDb)) * plot.getHeight();
            nodes[index]->setBounds (juce::roundToInt (x) - 17, juce::roundToInt (y) - 17, 34, 34);
            nodes[index]->setTitle ("Band " + juce::String { index + 1 } + ", "
                                     + juce::String { bands[index].frequency, 0 } + " hertz, "
                                     + juce::String { bands[index].gainDb, 1 } + " decibels");
        }
    }

    void select (int index)
    {
        setSelectedBand (index);
        onSelectionChanged (index);
    }

    juce::RangedAudioParameter* rangedParameter (const char* id) const
    {
        return owner.parameters.getParameter (id);
    }

    void beginDrag (int index)
    {
        rangedParameter (parameter::bands[static_cast<size_t> (index)].frequency)->beginChangeGesture();
        rangedParameter (parameter::bands[static_cast<size_t> (index)].gain)->beginChangeGesture();
    }

    void drag (int index, juce::Point<float> point)
    {
        const auto plot = plotBounds();
        const auto unitX = (point.x - plot.getX()) / plot.getWidth();
        const auto unitY = (point.y - plot.getY()) / plot.getHeight();
        auto* frequency = rangedParameter (parameter::bands[static_cast<size_t> (index)].frequency);
        auto* gain = rangedParameter (parameter::bands[static_cast<size_t> (index)].gain);
        frequency->setValueNotifyingHost (frequency->convertTo0to1 (static_cast<float> (eq::graph::unitToFrequency (unitX))));
        gain->setValueNotifyingHost (gain->convertTo0to1 (static_cast<float> (eq::graph::unitToGain (unitY))));
        refresh();
    }

    void endDrag (int index)
    {
        rangedParameter (parameter::bands[static_cast<size_t> (index)].frequency)->endChangeGesture();
        rangedParameter (parameter::bands[static_cast<size_t> (index)].gain)->endChangeGesture();
    }

    bool nudge (int index, int horizontal, int vertical)
    {
        const auto ids = parameter::bands[static_cast<size_t> (index)];
        auto* frequency = rangedParameter (ids.frequency);
        auto* gain = rangedParameter (ids.gain);
        if (horizontal != 0)
        {
            const auto current = owner.parameters.getRawParameterValue (ids.frequency)->load();
            frequency->beginChangeGesture();
            frequency->setValueNotifyingHost (frequency->convertTo0to1 (current * std::pow (2.0f, horizontal / 24.0f)));
            frequency->endChangeGesture();
        }
        if (vertical != 0)
        {
            const auto current = owner.parameters.getRawParameterValue (ids.gain)->load();
            gain->beginChangeGesture();
            gain->setValueNotifyingHost (gain->convertTo0to1 (current + static_cast<float> (vertical) * 0.5f));
            gain->endChangeGesture();
        }
        refresh();
        return true;
    }

    PluginProcessor& owner;
    std::function<void (int)> onSelectionChanged;
    std::array<std::unique_ptr<Node>, 6> nodes;
    int selectedBand {};
};

PluginEditor::ParameterControl::ParameterControl (PluginEditor& editor,
                                                  juce::AudioProcessorValueTreeState& state,
                                                  const char* parameterID,
                                                  const juce::String& displayName,
                                                  const juce::String& suffix)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 94, 24);
    slider.setTextValueSuffix (suffix);
    slider.setName (displayName);
    slider.setTitle (displayName);
    slider.setDescription ("Automatable " + displayName + " control; double-click the value to type");
    slider.setTooltip (displayName + ": drag or type an exact value");
    slider.setColour (juce::Slider::rotarySliderFillColourId, palette::primary);
    slider.setWantsKeyboardFocus (true);
    label.setText (displayName.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, palette::muted);
    label.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
    label.setInterceptsMouseClicks (false, false);
    editor.addChildComponent (slider);
    editor.addChildComponent (label);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterID, slider);
}

PluginEditor::PluginEditor (PluginProcessor& owner)
    : AudioProcessorEditor (&owner), pluginProcessor (owner),
      semanticLookAndFeel (std::make_unique<SemanticLookAndFeel>())
{
    setLookAndFeel (semanticLookAndFeel.get());
    setWantsKeyboardFocus (true);

    productLabel.setText ("SEMANTIFX", juce::dontSendNotification);
    productLabel.setFont (juce::FontOptions { 25.0f, juce::Font::bold });
    productLabel.setColour (juce::Label::textColourId, palette::text);
    addAndMakeVisible (productLabel);
    subtitleLabel.setText ("Semantic equalizer", juce::dontSendNotification);
    subtitleLabel.setColour (juce::Label::textColourId, palette::muted);
    addAndMakeVisible (subtitleLabel);
    engineBadge.setText ("LOCAL  |  MS-CLAP", juce::dontSendNotification);
    engineBadge.setJustificationType (juce::Justification::centred);
    engineBadge.setColour (juce::Label::backgroundColourId, palette::panel);
    engineBadge.setColour (juce::Label::outlineColourId, palette::border);
    engineBadge.setColour (juce::Label::textColourId, palette::primary);
    engineBadge.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
    addAndMakeVisible (engineBadge);

    bypassButton.setTitle ("Hear original signal");
    bypassButton.setDescription ("Bypass all semantic EQ processing while preserving its settings");
    bypassButton.setTooltip ("Compare the unprocessed original with the shaped sound");
    bypassButton.setColour (juce::ToggleButton::tickColourId, palette::primary);
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        pluginProcessor.parameters, parameter::bypass, bypassButton);

    importButton.setTitle ("Import analysis result");
    importButton.setDescription ("Import a Semantifx protocol version 1 JSON result");
    importButton.setTooltip ("Import an existing Semantifx analysis result");
    importButton.setColour (juce::TextButton::buttonColourId, palette::border);
    importButton.onClick = [safeThis = juce::Component::SafePointer<PluginEditor> { this }]
    {
        if (safeThis == nullptr) return;
        safeThis->fileChooser = std::make_unique<juce::FileChooser> (
            "Import Semantifx analysis result", juce::File {}, "*.json");
        safeThis->importButton.setEnabled (false);
        safeThis->fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis] (const juce::FileChooser& chooser)
            {
                if (safeThis == nullptr) return;
                const auto file = chooser.getResult();
                if (file == juce::File {})
                    safeThis->importStatus.setText ("Import cancelled", juce::dontSendNotification);
                else
                {
                    const auto result = safeThis->pluginProcessor.applyAnalysisResultJson (file.loadFileAsString());
                    safeThis->importStatus.setText (result.wasOk() ? "Imported" : result.getErrorMessage(),
                                                    juce::dontSendNotification);
                    safeThis->importStatus.setColour (juce::Label::textColourId,
                                                      result.wasOk() ? palette::primary : palette::danger);
                    if (result.wasOk()) safeThis->responseGraph->refresh();
                }
                safeThis->importButton.setEnabled (true);
                juce::MessageManager::callAsync ([safeThis]
                {
                    if (safeThis != nullptr) safeThis->fileChooser.reset();
                });
            });
    };
    addAndMakeVisible (importButton);
    importStatus.setColour (juce::Label::textColourId, palette::muted);
    importStatus.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (importStatus);

    workflowEyebrow.setText ("READY", juce::dontSendNotification);
    workflowEyebrow.setColour (juce::Label::textColourId, palette::primary);
    workflowEyebrow.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
    addAndMakeVisible (workflowEyebrow);
    promptEditor.setTextToShowWhenEmpty ("Describe a direction - \"brighter, but keep the body\"", palette::muted);
    promptEditor.setTitle ("Describe the sound direction");
    promptEditor.setDescription ("Natural-language target for the local semantic EQ analysis");
    promptEditor.setTooltip ("Describe how you want the captured audio to change");
    promptEditor.setMultiLine (false);
    promptEditor.onReturnKey = [this] { beginAnalysis(); };
    addAndMakeVisible (promptEditor);

    captureButton.setTitle ("Listen for ten seconds");
    captureButton.setDescription ("Capture ten seconds from the plugin input for analysis");
    captureButton.setTooltip ("Start a ten-second capture while audio is playing");
    captureButton.setColour (juce::TextButton::buttonColourId, palette::border);
    captureButton.onClick = [this] { beginCapture(); };
    addAndMakeVisible (captureButton);
    analyzeButton.setTitle ("Shape sound from description");
    analyzeButton.setDescription ("Run the local 50-iteration Text2FX analysis on the captured audio");
    analyzeButton.setTooltip ("Analyze the capture using the typed direction");
    analyzeButton.setColour (juce::TextButton::buttonColourId, palette::primary);
    analyzeButton.setColour (juce::TextButton::textColourOffId, palette::background);
    analyzeButton.onClick = [this] { beginAnalysis(); };
    addAndMakeVisible (analyzeButton);
    cancelButton.setTitle ("Cancel capture or analysis");
    cancelButton.setTooltip ("Stop the active listening or analysis task");
    cancelButton.setColour (juce::TextButton::buttonColourId, palette::danger.darker (0.3f));
    cancelButton.onClick = [this] { cancelWork(); };
    addAndMakeVisible (cancelButton);
    captureProgress.setPercentageDisplay (false);
    captureProgress.setTitle ("Listening progress");
    addAndMakeVisible (captureProgress);
    workflowStatus.setText ("Play audio, then start listening", juce::dontSendNotification);
    workflowStatus.setTitle ("Analysis status");
    workflowStatus.setColour (juce::Label::textColourId, palette::muted);
    addAndMakeVisible (workflowStatus);

    responseGraph = std::make_unique<EqResponseGraph> (pluginProcessor, [this] (int index) { selectBand (index); });
    addAndMakeVisible (*responseGraph);

    const std::array<juce::String, 6> names { "Low", "Low mid", "Mid", "High mid", "Air", "High" };
    size_t controlIndex {};
    for (size_t band = 0; band < parameter::bands.size(); ++band)
    {
        const auto ids = parameter::bands[band];
        bandControls[controlIndex++] = std::make_unique<ParameterControl> (*this, pluginProcessor.parameters, ids.gain,
                                                                           names[band] + " gain", " dB");
        bandControls[controlIndex++] = std::make_unique<ParameterControl> (*this, pluginProcessor.parameters, ids.frequency,
                                                                           names[band] + " frequency", " Hz");
        bandControls[controlIndex++] = std::make_unique<ParameterControl> (*this, pluginProcessor.parameters, ids.q,
                                                                           names[band] + " Q", "");
        auto& button = bandButtons[band];
        button.setButtonText (juce::String { static_cast<int> (band + 1) } + "  " + names[band].toUpperCase());
        button.setTitle ("Select " + names[band] + " band");
        button.setTooltip ("Show controls for the " + names[band] + " band");
        button.setClickingTogglesState (false);
        button.setColour (juce::TextButton::buttonColourId, palette::panel);
        button.onClick = [this, band] { selectBand (static_cast<int> (band)); };
        addAndMakeVisible (button);
    }
    mixControl = std::make_unique<ParameterControl> (*this, pluginProcessor.parameters, parameter::mix, "Mix", " %");
    outputControl = std::make_unique<ParameterControl> (*this, pluginProcessor.parameters, parameter::outputGain,
                                                        "Output", " dB");
    selectedBandLabel.setColour (juce::Label::textColourId, palette::text);
    selectedBandLabel.setFont (juce::FontOptions { 14.0f, juce::Font::bold });
    addAndMakeVisible (selectedBandLabel);
    lastDirectionLabel.setText ("Last direction: -", juce::dontSendNotification);
    lastDirectionLabel.setColour (juce::Label::textColourId, palette::muted);
    lastDirectionLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (lastDirectionLabel);
    selectBand (0);

    setResizable (true, true);
    setResizeLimits (820, 620, 1600, 1100);
    setSize (1100, 760);
    startTimerHz (15);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette::background);
    g.setColour (palette::border);
    g.drawHorizontalLine (78, 20.0f, static_cast<float> (getWidth() - 20));
    auto command = juce::Rectangle<float> { 20.0f, 92.0f, static_cast<float> (getWidth() - 40), 116.0f };
    g.setColour (palette::panel);
    g.fillRoundedRectangle (command, 10.0f);
    g.setColour (palette::border);
    g.drawRoundedRectangle (command.reduced (0.5f), 10.0f, 1.0f);
    auto inspector = juce::Rectangle<float> { 20.0f, static_cast<float> (getHeight() - 166),
                                              static_cast<float> (getWidth() - 40), 146.0f };
    g.setColour (palette::panel);
    g.fillRoundedRectangle (inspector, 10.0f);
    g.setColour (palette::border);
    g.drawRoundedRectangle (inspector.reduced (0.5f), 10.0f, 1.0f);
}

void PluginEditor::resized()
{
    auto header = juce::Rectangle<int> { 20, 12, getWidth() - 40, 58 };
    productLabel.setBounds (header.removeFromLeft (175).removeFromTop (34));
    subtitleLabel.setBounds (22, 43, 190, 22);
    engineBadge.setBounds (214, 20, 128, 28);
    bypassButton.setBounds (getWidth() - 162, 22, 142, 34);
    importButton.setBounds (getWidth() - 276, 22, 104, 34);
    importStatus.setBounds (getWidth() - 470, 51, 290, 20);

    auto command = juce::Rectangle<int> { 32, 101, getWidth() - 64, 99 };
    workflowEyebrow.setBounds (command.removeFromTop (20).removeFromLeft (250));
    auto controls = command.removeFromTop (40);
    const auto buttonWidth = getWidth() < 950 ? 104 : 120;
    captureButton.setBounds (controls.removeFromLeft (buttonWidth));
    controls.removeFromLeft (8);
    analyzeButton.setBounds (controls.removeFromRight (buttonWidth));
    controls.removeFromRight (8);
    cancelButton.setBounds (controls.removeFromRight (82));
    controls.removeFromRight (8);
    promptEditor.setBounds (controls);
    auto status = command.removeFromTop (30);
    captureProgress.setBounds (status.removeFromLeft (std::min (250, getWidth() / 4)).reduced (0, 8));
    status.removeFromLeft (10);
    workflowStatus.setBounds (status);

    responseGraph->setBounds (20, 220, getWidth() - 40, getHeight() - 398);

    auto inspector = juce::Rectangle<int> { 32, getHeight() - 158, getWidth() - 64, 130 };
    auto rail = inspector.removeFromTop (31);
    const auto bandWidth = std::max (86, (rail.getWidth() - 230) / 6);
    for (auto& button : bandButtons)
    {
        button.setBounds (rail.removeFromLeft (bandWidth).reduced (2, 0));
    }
    lastDirectionLabel.setBounds (rail);
    auto controlArea = inspector;
    selectedBandLabel.setBounds (controlArea.removeFromLeft (130).removeFromTop (28));
    const auto knobWidth = std::min (125, std::max (94, (controlArea.getWidth() - 238) / 3));
    for (int index = 0; index < 3; ++index)
    {
        auto cell = controlArea.removeFromLeft (knobWidth).reduced (5, 1);
        auto& control = *bandControls[static_cast<size_t> (selectedBand * 3 + index)];
        control.label.setBounds (cell.removeFromTop (18));
        control.slider.setBounds (cell);
    }
    auto outputArea = controlArea.removeFromRight (228);
    auto mixCell = outputArea.removeFromLeft (114).reduced (5, 1);
    mixControl->label.setBounds (mixCell.removeFromTop (18));
    mixControl->slider.setBounds (mixCell);
    auto outputCell = outputArea.reduced (5, 1);
    outputControl->label.setBounds (outputCell.removeFromTop (18));
    outputControl->slider.setBounds (outputCell);
}

bool PluginEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        cancelWork();
        return true;
    }
    return AudioProcessorEditor::keyPressed (key);
}

void PluginEditor::selectBand (int index)
{
    selectedBand = juce::jlimit (0, 5, index);
    responseGraph->setSelectedBand (selectedBand);
    static constexpr std::array<const char*, 6> names { "LOW SHELF", "LOW MID", "MID", "HIGH MID", "AIR", "HIGH SHELF" };
    selectedBandLabel.setText (juce::String { selectedBand + 1 } + "  " + names[static_cast<size_t> (selectedBand)],
                               juce::dontSendNotification);
    for (size_t band = 0; band < bandButtons.size(); ++band)
    {
        const auto selected = static_cast<int> (band) == selectedBand;
        bandButtons[band].setColour (juce::TextButton::buttonColourId,
                                     selected ? palette::bands[band].withAlpha (0.72f) : palette::panel);
        for (int control = 0; control < 3; ++control)
        {
            auto& item = *bandControls[band * 3 + static_cast<size_t> (control)];
            item.label.setVisible (selected);
            item.slider.setVisible (selected);
            item.slider.setColour (juce::Slider::rotarySliderFillColourId, palette::bands[band]);
        }
    }
    resized();
}

void PluginEditor::beginCapture()
{
    importStatus.setText ({}, juce::dontSendNotification);
    const auto result = pluginProcessor.startDevelopmentCapture();
    if (result.failed())
    {
        workflowEyebrow.setText ("ERROR", juce::dontSendNotification);
        workflowStatus.setText (result.getErrorMessage(), juce::dontSendNotification);
        workflowStatus.setColour (juce::Label::textColourId, palette::danger);
    }
}

void PluginEditor::beginAnalysis()
{
    const auto result = pluginProcessor.startDevelopmentAnalysis (promptEditor.getText(), 50);
    if (result.failed())
    {
        workflowEyebrow.setText ("ERROR", juce::dontSendNotification);
        workflowStatus.setText (result.getErrorMessage(), juce::dontSendNotification);
        workflowStatus.setColour (juce::Label::textColourId, palette::danger);
    }
}

void PluginEditor::cancelWork()
{
    pluginProcessor.cancelDevelopmentWork();
    workflowEyebrow.setText ("CANCELLED", juce::dontSendNotification);
    workflowStatus.setText ("Listening or analysis cancelled", juce::dontSendNotification);
}

void PluginEditor::timerCallback()
{
    captureProgressValue = pluginProcessor.getCaptureProgress();
    responseGraph->refresh();
    updateWorkflowState();
}

void PluginEditor::updateWorkflowState()
{
    const auto captureState = pluginProcessor.getCaptureState();
    const auto analysisState = pluginProcessor.getDevelopmentAnalysisState();
    const auto capturing = captureState == CaptureBuffer::State::capturing;
    const auto analysisActive = analysisState == DevelopmentAnalysisState::running
                             || analysisState == DevelopmentAnalysisState::cancelling
                             || analysisState == DevelopmentAnalysisState::applying;
    captureButton.setEnabled (! capturing && ! analysisActive);
    analyzeButton.setEnabled (captureState == CaptureBuffer::State::ready && ! analysisActive);
    cancelButton.setVisible (capturing || analysisActive);
    cancelButton.setEnabled (capturing || analysisActive);
    captureProgress.setVisible (capturing);
    workflowStatus.setColour (juce::Label::textColourId, palette::muted);

    if (capturing)
    {
        workflowEyebrow.setText ("LISTENING 0-10 S", juce::dontSendNotification);
        workflowStatus.setText ("Listening  |  " + juce::String { captureProgressValue * 10.0, 1 }
                                + " / 10.0 seconds", juce::dontSendNotification);
    }
    else if (analysisState == DevelopmentAnalysisState::running
             || analysisState == DevelopmentAnalysisState::cancelling)
    {
        workflowEyebrow.setText ("ANALYZING", juce::dontSendNotification);
        workflowStatus.setText (pluginProcessor.getDevelopmentStatus(), juce::dontSendNotification);
    }
    else if (analysisState == DevelopmentAnalysisState::applying)
    {
        workflowEyebrow.setText ("APPLYING", juce::dontSendNotification);
        workflowStatus.setText (pluginProcessor.getDevelopmentStatus(), juce::dontSendNotification);
    }
    else if (analysisState == DevelopmentAnalysisState::succeeded)
    {
        workflowEyebrow.setText ("APPLIED", juce::dontSendNotification);
        workflowStatus.setText ("Applied \"" + promptEditor.getText().trim() + "\" - edit any band below",
                                juce::dontSendNotification);
        if (previousAnalysisState != DevelopmentAnalysisState::succeeded)
            lastDirectionLabel.setText ("Last direction: " + promptEditor.getText().trim(),
                                        juce::dontSendNotification);
    }
    else if (analysisState == DevelopmentAnalysisState::failed)
    {
        workflowEyebrow.setText ("ERROR", juce::dontSendNotification);
        workflowStatus.setColour (juce::Label::textColourId, palette::danger);
        workflowStatus.setText (pluginProcessor.getDevelopmentStatus(), juce::dontSendNotification);
    }
    else if (analysisState == DevelopmentAnalysisState::cancelled)
    {
        workflowEyebrow.setText ("CANCELLED", juce::dontSendNotification);
        workflowStatus.setText (pluginProcessor.getDevelopmentStatus(), juce::dontSendNotification);
    }
    else if (captureState == CaptureBuffer::State::ready)
    {
        workflowEyebrow.setText ("CAPTURE READY", juce::dontSendNotification);
        workflowStatus.setText ("Add a direction, then choose Shape sound", juce::dontSendNotification);
    }
    else
    {
        workflowEyebrow.setText ("READY", juce::dontSendNotification);
        workflowStatus.setText ("Play audio, then start listening", juce::dontSendNotification);
    }
    previousAnalysisState = analysisState;
}
} // namespace semantifx
