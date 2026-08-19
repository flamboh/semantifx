#include "PluginEditor.h"
#include "EqGraphMath.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace semantifx
{
// Catppuccin Mocha (https://catppuccin.com/palette): Base background, Mantle
// footer, Surface0/1 chrome, Mauve as the single accent shared by selection,
// the response curve, and the primary action.
namespace palette
{
const auto background = juce::Colour { 0xff1e1e2e }; // base
const auto mantle = juce::Colour { 0xff181825 };
const auto surface = juce::Colour { 0xff313244 };    // surface0
const auto line = juce::Colour { 0xff45475a };       // surface1
const auto text = juce::Colour { 0xffcdd6f4 };
const auto muted = juce::Colour { 0xff7f849c };      // overlay1
const auto accent = juce::Colour { 0xffcba6f7 };     // mauve
const auto danger = juce::Colour { 0xfff38ba8 };     // red
} // namespace palette

class SemanticLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SemanticLookAndFeel()
    {
        setColour (juce::TextButton::textColourOffId, palette::text);
        setColour (juce::TextButton::textColourOnId, palette::background);
        setColour (juce::TextEditor::backgroundColourId, palette::background);
        setColour (juce::TextEditor::outlineColourId, palette::line);
        setColour (juce::TextEditor::focusedOutlineColourId, palette::accent);
        setColour (juce::TextEditor::textColourId, palette::text);
        setColour (juce::TextEditor::highlightColourId, palette::accent.withAlpha (0.35f));
        setColour (juce::Slider::textBoxTextColourId, palette::text);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TooltipWindow::backgroundColourId, palette::surface);
        setColour (juce::TooltipWindow::textColourId, palette::text);
        setColour (juce::TooltipWindow::outlineColourId, palette::line);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                               bool highlighted, bool down) override
    {
        auto fill = colour;
        if (highlighted) fill = fill.brighter (0.12f);
        if (down) fill = fill.darker (0.14f);
        g.setColour (fill);
        g.fillRect (button.getLocalBounds());
        if (button.hasKeyboardFocus (true))
        {
            g.setColour (palette::accent);
            g.drawRect (button.getLocalBounds().reduced (1), 2);
        }
    }

    void fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
        g.fillRect (0, 0, width, height);
    }

    void drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        const auto focused = editor.hasKeyboardFocus (true) && ! editor.isReadOnly();
        g.setColour (editor.findColour (focused ? juce::TextEditor::focusedOutlineColourId
                                                : juce::TextEditor::outlineColourId));
        g.drawRect (0, 0, width, height, focused ? 2 : 1);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                     static_cast<float> (width), static_cast<float> (height)).reduced (6.0f);
        const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto angle = startAngle + position * (endAngle - startAngle);
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (palette::line);
        g.strokePath (arc, juce::PathStrokeType { 3.5f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded });
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (valueArc, juce::PathStrokeType { 3.5f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded });
        const auto pointer = juce::Point<float> { centre.x + std::sin (angle) * radius,
                                                  centre.y - std::cos (angle) * radius };
        g.setColour (palette::text);
        g.fillEllipse (juce::Rectangle<float> { 6.0f, 6.0f }.withCentre (pointer));
    }
};

/** The effect chain as draggable modules on a signal-flow line. Drag a module
    across its neighbour to reorder the chain; select one to edit it. */
class ChainRack final : public juce::Component
{
public:
    static constexpr int eqModule = 0;
    static constexpr int distortionModule = 1;

    ChainRack (PluginProcessor& processor, std::function<void (int)> onSelectModule)
        : owner (processor), onSelect (std::move (onSelectModule))
    {
        chips[eqModule] = std::make_unique<Chip> (*this, eqModule, "EQ");
        chips[distortionModule] = std::make_unique<Chip> (*this, distortionModule, "DISTORTION");
        for (auto& chip : chips)
            addAndMakeVisible (*chip);
        setTitle ("Effect chain");
        setDescription ("Signal flows left to right; drag a module to reorder, select one to edit it");
    }

    static constexpr int chipWidth = 100;
    static constexpr int chipGap = 26;
    static constexpr int idealWidth = 2 * chipWidth + chipGap;

    void setSelected (int module)
    {
        selected = module;
        for (auto& chip : chips)
            chip->repaint();
    }

    void refresh()
    {
        if (! chips[0]->isDragging() && ! chips[1]->isDragging())
            layout();
    }

    void resized() override { layout(); }

    void paint (juce::Graphics& g) override
    {
        const auto y = static_cast<float> (getHeight()) * 0.5f;
        const auto right = static_cast<float> (getWidth());
        g.setColour (palette::line);
        g.drawLine (0.0f, y, right, y, 1.2f);
        juce::Path arrow;
        arrow.addTriangle (right - 6.0f, y - 4.0f, right - 6.0f, y + 4.0f, right, y);
        g.fillPath (arrow);
    }

private:
    class Chip final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        Chip (ChainRack& rack, int moduleIndex, juce::String text)
            : owner (rack), module (moduleIndex), label (std::move (text))
        {
            setWantsKeyboardFocus (true);
            setTitle (label + " module");
            setDescription ("Select to edit; drag or use the arrow keys to reorder the chain");
            setTooltip (label + ": click to edit, drag to reorder");
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        }

        bool isDragging() const noexcept { return dragging; }

        void paint (juce::Graphics& g) override
        {
            const auto isSelected = owner.selected == module;
            auto fill = palette::surface;
            if (dragging) fill = fill.brighter (0.25f);
            else if (hovered) fill = fill.brighter (0.12f);
            g.setColour (fill);
            g.fillRect (getLocalBounds());
            g.setColour (isSelected ? palette::accent : palette::line);
            g.drawRect (getLocalBounds(), isSelected ? 2 : 1);
            if (hasKeyboardFocus (true))
            {
                g.setColour (palette::accent);
                g.drawRect (getLocalBounds().reduced (3), 1);
            }
            g.setColour (isSelected ? palette::accent : palette::text);
            g.setFont (juce::FontOptions { 12.5f, juce::Font::bold });
            g.drawText (label, getLocalBounds(), juce::Justification::centred);
        }

        void mouseEnter (const juce::MouseEvent&) override { hovered = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hovered = false; repaint(); }

        void mouseDown (const juce::MouseEvent&) override
        {
            dragStartX = getX();
            dragging = false;
        }

        void mouseDrag (const juce::MouseEvent& event) override
        {
            const auto dx = event.getDistanceFromDragStartX();
            if (! dragging && std::abs (dx) < 5)
                return;
            if (! dragging)
            {
                dragging = true;
                toFront (false);
            }
            const auto x = juce::jlimit (0, owner.getWidth() - getWidth(), dragStartX + dx);
            setTopLeftPosition (x, getY());
            const auto shouldBeFirst = getBounds().getCentreX() < owner.getWidth() / 2;
            const auto isFirst = owner.firstModule() == module;
            if (shouldBeFirst != isFirst)
            {
                owner.toggleOrder();
                owner.layout();
            }
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            const auto wasDragging = dragging;
            dragging = false;
            owner.layout();
            repaint();
            if (! wasDragging)
                owner.onSelect (module);
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
            {
                owner.onSelect (module);
                return true;
            }
            const auto isFirst = owner.firstModule() == module;
            if ((key == juce::KeyPress::leftKey && ! isFirst) || (key == juce::KeyPress::rightKey && isFirst))
            {
                owner.toggleOrder();
                owner.layout();
                return true;
            }
            return false;
        }

    private:
        ChainRack& owner;
        int module;
        juce::String label;
        int dragStartX {};
        bool dragging {};
        bool hovered {};
    };

    int firstModule() const
    {
        return owner.parameters.getRawParameterValue (parameter::effectOrder)->load() >= 0.5f
                 ? distortionModule : eqModule;
    }

    void toggleOrder()
    {
        auto* order = owner.parameters.getParameter (parameter::effectOrder);
        const auto next = firstModule() == eqModule ? 1.0f : 0.0f;
        order->beginChangeGesture();
        order->setValueNotifyingHost (order->convertTo0to1 (next));
        order->endChangeGesture();
    }

    void layout()
    {
        const auto first = firstModule();
        auto x = 0;
        for (const auto module : { first, 1 - first })
        {
            auto& chip = *chips[static_cast<size_t> (module)];
            if (! chip.isDragging())
                chip.setBounds (x, 0, chipWidth, getHeight());
            x += chipWidth + chipGap;
        }
    }

    PluginProcessor& owner;
    std::function<void (int)> onSelect;
    std::array<std::unique_ptr<Chip>, 2> chips;
    int selected { eqModule };
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
            const auto isSelected = owner.selectedBand == index;
            auto circle = getLocalBounds().toFloat().reduced (isSelected ? 4.0f : 6.0f);
            if (hasKeyboardFocus (true))
            {
                g.setColour (palette::accent);
                g.drawEllipse (circle.expanded (3.0f), 1.5f);
            }
            g.setColour (isSelected ? palette::accent : palette::surface);
            g.fillEllipse (circle);
            g.setColour (isSelected ? palette::accent : palette::muted);
            g.drawEllipse (circle, 1.4f);
            g.setColour (isSelected ? palette::background : palette::text);
            g.setFont (juce::FontOptions { 12.5f, juce::Font::bold });
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
        selectedBand = juce::jlimit (-1, 5, index);
        for (auto& node : nodes) node->repaint();
    }

    void refresh()
    {
        updateNodePositions();
        repaint();
    }

    void setAnalysisPreview (std::optional<std::array<eq::graph::Band, 6>> bands,
                             int step = 0, int totalSteps = 0)
    {
        analysisPreview = std::move (bands);
        previewStep = step;
        previewTotalSteps = totalSteps;
    }

    void resized() override { updateNodePositions(); }

    void paint (juce::Graphics& g) override
    {
        const auto plot = plotBounds();
        constexpr std::array<double, 10> frequencies { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
        g.setFont (juce::FontOptions { 10.5f });
        for (const auto frequency : frequencies)
        {
            const auto x = static_cast<float> (plot.getX() + eq::graph::frequencyToUnit (frequency) * plot.getWidth());
            g.setColour (palette::line.withAlpha (0.55f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (palette::muted.withAlpha (0.8f));
            const auto label = frequency >= 1000.0 ? juce::String { frequency / 1000.0, frequency < 10000 ? 1 : 0 } + "k"
                                                   : juce::String { static_cast<int> (frequency) };
            g.drawText (label, juce::roundToInt (x) - 18, juce::roundToInt (plot.getBottom()) + 5, 36, 15,
                        juce::Justification::centred);
        }
        for (int db = -24; db <= 24; db += 12)
        {
            const auto y = static_cast<float> (plot.getY() + eq::graph::gainToUnit (db) * plot.getHeight());
            g.setColour (db == 0 ? palette::muted.withAlpha (0.6f) : palette::line.withAlpha (0.55f));
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            g.setColour (palette::muted.withAlpha (0.8f));
            g.drawText ((db > 0 ? "+" : "") + juce::String { db }, 4, juce::roundToInt (y) - 8, 34, 16,
                        juce::Justification::centredRight);
        }

        const auto sampleRate = owner.getSampleRate() > 0.0 ? owner.getSampleRate() : 48000.0;
        const auto drawResponse = [&] (const std::array<eq::graph::Band, 6>& bands,
                                       juce::Colour colour, float width, float fillAlpha)
        {
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
            if (fillAlpha > 0.0f)
            {
                fill = curve;
                fill.lineTo (plot.getRight(), plot.getCentreY());
                fill.lineTo (plot.getX(), plot.getCentreY());
                fill.closeSubPath();
                g.setColour (colour.withAlpha (fillAlpha));
                g.fillPath (fill);
            }
            g.setColour (colour);
            g.strokePath (curve, juce::PathStrokeType { width, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded });
        };

        const auto currentBands = readBands();
        if (analysisPreview.has_value())
        {
            drawResponse (currentBands, palette::muted.withAlpha (0.45f), 1.5f, 0.0f);
            drawResponse (*analysisPreview, palette::accent, 2.5f, 0.09f);
            g.setColour (palette::accent);
            g.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
            g.drawText ("Optimizing  " + juce::String { previewStep } + " / " + juce::String { previewTotalSteps },
                        juce::roundToInt (plot.getX()) + 9, juce::roundToInt (plot.getY()) + 7, 320, 18,
                        juce::Justification::centredLeft);
        }
        else
        {
            drawResponse (currentBands, palette::accent, 2.5f, 0.09f);
        }
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
            nodes[index]->setBounds (juce::roundToInt (x) - 16, juce::roundToInt (y) - 16, 32, 32);
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
    std::optional<std::array<eq::graph::Band, 6>> analysisPreview;
    int previewStep {};
    int previewTotalSteps {};
    int selectedBand {};
};

PluginEditor::ParameterControl::ParameterControl (PluginEditor& editor,
                                                  juce::AudioProcessorValueTreeState& state,
                                                  const char* parameterID,
                                                  const juce::String& displayName,
                                                  const juce::String& suffix)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
    slider.setTextValueSuffix (suffix);
    slider.setName (displayName);
    slider.setTitle (displayName);
    slider.setDescription ("Automatable " + displayName + " control; double-click the value to type");
    slider.setTooltip (displayName + ": drag or type an exact value");
    slider.setColour (juce::Slider::rotarySliderFillColourId, palette::accent);
    slider.setWantsKeyboardFocus (true);
    label.setText (displayName.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, palette::muted);
    label.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
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

    chainRack = std::make_unique<ChainRack> (pluginProcessor, [this] (int module)
    {
        showModule (module == ChainRack::distortionModule ? Module::distortion : Module::eq);
    });
    addAndMakeVisible (*chainRack);

    importButton.setTitle ("Import analysis result");
    importButton.setDescription ("Import a SemantiFX analysis result JSON file");
    importButton.setTooltip ("Import an existing SemantiFX analysis result");
    importButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    importButton.setColour (juce::TextButton::textColourOffId, palette::muted);
    importButton.onClick = [safeThis = juce::Component::SafePointer<PluginEditor> { this }]
    {
        if (safeThis == nullptr) return;
        safeThis->fileChooser = std::make_unique<juce::FileChooser> (
            "Import SemantiFX analysis result", juce::File {}, "*.json");
        safeThis->importButton.setEnabled (false);
        safeThis->fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis] (const juce::FileChooser& chooser)
            {
                if (safeThis == nullptr) return;
                const auto file = chooser.getResult();
                if (file != juce::File {})
                {
                    const auto result = safeThis->pluginProcessor.applyAnalysisResultJson (file.loadFileAsString());
                    safeThis->showTransientStatus (result.wasOk() ? "Imported" : result.getErrorMessage(),
                                                   ! result.wasOk());
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

    promptEditor.setTextToShowWhenEmpty ("Describe a direction - \"brighter, but keep the body\"", palette::muted);
    promptEditor.setTitle ("Describe the sound direction");
    promptEditor.setDescription ("Natural-language target for local effect-chain optimization");
    promptEditor.setTooltip ("Describe how you want the captured audio to change");
    promptEditor.setMultiLine (false);
    promptEditor.onReturnKey = [this] { beginAnalysis(); };
    addAndMakeVisible (promptEditor);

    captureButton.setTitle ("Listen for ten seconds");
    captureButton.setDescription ("Capture ten seconds from the plugin input for analysis");
    captureButton.setTooltip ("Start a ten-second capture while audio is playing");
    captureButton.setColour (juce::TextButton::buttonColourId, palette::surface);
    captureButton.onClick = [this] { beginCapture(); };
    addAndMakeVisible (captureButton);
    analyzeButton.setTitle ("Shape sound from description");
    analyzeButton.setDescription ("Run the local CLAP optimization on the captured audio");
    analyzeButton.setTooltip ("Analyze the capture using the typed direction");
    analyzeButton.setColour (juce::TextButton::buttonColourId, palette::accent);
    analyzeButton.setColour (juce::TextButton::textColourOffId, palette::background);
    analyzeButton.onClick = [this] { beginAnalysis(); };
    addAndMakeVisible (analyzeButton);
    cancelButton.setTitle ("Cancel capture or analysis");
    cancelButton.setTooltip ("Stop the active listening or analysis task");
    cancelButton.setColour (juce::TextButton::buttonColourId, palette::danger.darker (0.35f));
    cancelButton.onClick = [this] { cancelWork(); };
    addAndMakeVisible (cancelButton);
    workflowStatus.setText ("Play audio, then press Listen", juce::dontSendNotification);
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
    }
    distortionControls[0] = std::make_unique<ParameterControl> (
        *this, pluginProcessor.parameters, parameter::distortionDrive, "Drive", " dB");
    distortionControls[1] = std::make_unique<ParameterControl> (
        *this, pluginProcessor.parameters, parameter::distortionCharacter, "Character", "");
    distortionControls[2] = std::make_unique<ParameterControl> (
        *this, pluginProcessor.parameters, parameter::distortionTone, "Tone", " dB");
    distortionControls[3] = std::make_unique<ParameterControl> (
        *this, pluginProcessor.parameters, parameter::distortionMix, "Mix", "");
    mixControl = std::make_unique<ParameterControl> (*this, pluginProcessor.parameters, parameter::mix, "Mix", " %");
    outputControl = std::make_unique<ParameterControl> (*this, pluginProcessor.parameters, parameter::outputGain,
                                                        "Output", " dB");
    mixControl->label.setVisible (true);
    mixControl->slider.setVisible (true);
    outputControl->label.setVisible (true);
    outputControl->slider.setVisible (true);
    showModule (Module::eq);

    setResizable (true, true);
    setResizeLimits (780, 560, 1600, 1100);
    setSize (980, 680);
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

    // Wordmark: quiet text with the accent carrying "FX".
    g.setFont (juce::FontOptions { 19.0f, juce::Font::bold });
    const auto semantiWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), "Semanti");
    g.setColour (palette::text);
    g.drawText ("Semanti", 24, 0, semantiWidth, 56, juce::Justification::centredLeft);
    g.setColour (palette::accent);
    g.drawText ("FX", 24 + semantiWidth, 0, 40, 56, juce::Justification::centredLeft);

    g.setColour (palette::line);
    g.drawHorizontalLine (56, 0.0f, static_cast<float> (getWidth()));
    const auto footerTop = getHeight() - 118;
    g.setColour (palette::mantle);
    g.fillRect (0, footerTop, getWidth(), 118);
    g.setColour (palette::line);
    g.drawHorizontalLine (footerTop, 0.0f, static_cast<float> (getWidth()));
}

void PluginEditor::resized()
{
    // Header: wordmark (painted), centred chain rack, quiet utilities right.
    chainRack->setBounds ((getWidth() - ChainRack::idealWidth) / 2, 11, ChainRack::idealWidth, 34);
    importButton.setBounds (getWidth() - 90, 11, 66, 34);

    // Main area: the selected module fills it edge to edge.
    auto main = juce::Rectangle<int> { 0, 57, getWidth(), getHeight() - 57 - 118 };
    if (visibleModule == Module::eq)
    {
        auto knobStrip = main.removeFromBottom (96);
        responseGraph->setBounds (main.reduced (8, 4));
        auto knobs = knobStrip.withSizeKeepingCentre (3 * 108, knobStrip.getHeight());
        for (int index = 0; index < 3; ++index)
        {
            auto cell = knobs.removeFromLeft (108).reduced (6, 2);
            auto& control = *bandControls[static_cast<size_t> (selectedBand * 3 + index)];
            control.label.setBounds (cell.removeFromTop (16));
            control.slider.setBounds (cell);
        }
    }
    else
    {
        auto knobs = main.withSizeKeepingCentre (4 * 148, std::min (200, main.getHeight() - 24));
        for (auto& control : distortionControls)
        {
            auto cell = knobs.removeFromLeft (148).reduced (12, 4);
            control->label.setBounds (cell.removeFromTop (18));
            control->slider.setBounds (cell);
        }
    }

    // Footer: capture, prompt, and primary action on one line; status under
    // the prompt; mix and output on the right.
    auto footer = juce::Rectangle<int> { 24, getHeight() - 106, getWidth() - 48, 94 };
    auto knobArea = footer.removeFromRight (196);
    footer.removeFromRight (16);
    auto controls = footer.removeFromTop (40);
    captureButton.setBounds (controls.removeFromLeft (104));
    controls.removeFromLeft (10);
    const auto actionArea = controls.removeFromRight (118);
    analyzeButton.setBounds (actionArea);
    cancelButton.setBounds (actionArea);
    controls.removeFromRight (10);
    promptEditor.setBounds (controls);
    footer.removeFromTop (8);
    workflowStatus.setBounds (footer.removeFromTop (20));
    auto mixCell = knobArea.removeFromLeft (94).reduced (4, 0);
    mixControl->label.setBounds (mixCell.removeFromTop (16));
    mixControl->slider.setBounds (mixCell);
    auto outputCell = knobArea.reduced (4, 0);
    outputControl->label.setBounds (outputCell.removeFromTop (16));
    outputControl->slider.setBounds (outputCell);
}

bool PluginEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        cancelWork (false);
        return true;
    }
    return AudioProcessorEditor::keyPressed (key);
}

void PluginEditor::showModule (Module module)
{
    visibleModule = module;
    chainRack->setSelected (module == Module::distortion ? ChainRack::distortionModule : ChainRack::eqModule);
    const auto eqVisible = module == Module::eq;
    responseGraph->setVisible (eqVisible);
    responseGraph->setSelectedBand (eqVisible ? selectedBand : -1);
    for (auto& control : distortionControls)
    {
        control->label.setVisible (! eqVisible);
        control->slider.setVisible (! eqVisible);
    }
    selectBand (selectedBand);
}

void PluginEditor::selectBand (int index)
{
    selectedBand = juce::jlimit (0, 5, index);
    const auto eqVisible = visibleModule == Module::eq;
    if (eqVisible)
        responseGraph->setSelectedBand (selectedBand);
    for (size_t band = 0; band < parameter::bands.size(); ++band)
    {
        const auto shown = eqVisible && static_cast<int> (band) == selectedBand;
        for (int control = 0; control < 3; ++control)
        {
            auto& item = *bandControls[band * 3 + static_cast<size_t> (control)];
            item.label.setVisible (shown);
            item.slider.setVisible (shown);
        }
    }
    resized();
}

void PluginEditor::beginCapture()
{
    const auto result = pluginProcessor.startDevelopmentCapture();
    if (result.failed())
        showTransientStatus (result.getErrorMessage(), true);
}

void PluginEditor::beginAnalysis()
{
    const auto direction = promptEditor.getText().trim();
    const auto result = pluginProcessor.startDevelopmentAnalysis (direction);
    if (result.failed())
        showTransientStatus (result.getErrorMessage(), true);
}

void PluginEditor::cancelWork (bool acceptLatestPreview)
{
    const auto progress = pluginProcessor.getDevelopmentProgress();
    const auto canUsePreview = progress.has_value() && progress->hasParameters;
    pluginProcessor.stopDevelopmentWork (acceptLatestPreview && canUsePreview);
}

void PluginEditor::showTransientStatus (const juce::String& text, bool isError)
{
    transientStatus = text;
    transientStatusIsError = isError;
    transientStatusExpiry = juce::Time::currentTimeMillis() + 5000;
}

void PluginEditor::timerCallback()
{
    captureProgressValue = pluginProcessor.getCaptureProgress();
    chainRack->refresh();
    updateAnalysisPreview();
    responseGraph->refresh();
    updateWorkflowState();
}

void PluginEditor::updateAnalysisPreview()
{
    const auto state = pluginProcessor.getDevelopmentAnalysisState();
    const auto progress = pluginProcessor.getDevelopmentProgress();
    if ((state == DevelopmentAnalysisState::running || state == DevelopmentAnalysisState::cancelling)
        && progress.has_value() && progress->hasParameters)
    {
        std::array<eq::graph::Band, 6> bands;
        for (size_t index = 0; index < bands.size(); ++index)
        {
            const auto offset = index * 3;
            bands[index] = { progress->preview.values[offset],
                             progress->preview.values[offset + 1],
                             progress->preview.values[offset + 2] };
        }
        responseGraph->setAnalysisPreview (bands, progress->step, progress->totalSteps);
    }
    else
    {
        responseGraph->setAnalysisPreview (std::nullopt);
    }
}

void PluginEditor::updateWorkflowState()
{
    const auto captureState = pluginProcessor.getCaptureState();
    const auto analysisState = pluginProcessor.getDevelopmentAnalysisState();
    const auto capturing = captureState == CaptureBuffer::State::capturing;
    const auto analysisActive = analysisState == DevelopmentAnalysisState::running
                             || analysisState == DevelopmentAnalysisState::cancelling
                             || analysisState == DevelopmentAnalysisState::applying;
    const auto interruptible = capturing || analysisState == DevelopmentAnalysisState::running;
    captureButton.setEnabled (! capturing && ! analysisActive);
    captureButton.setButtonText (capturing ? juce::String { captureProgressValue * 10.0, 1 } + " s"
                                           : juce::String { "Listen 10s" });
    analyzeButton.setEnabled (captureState == CaptureBuffer::State::ready && ! analysisActive);
    analyzeButton.setVisible (! interruptible);
    cancelButton.setVisible (interruptible);
    cancelButton.setEnabled (interruptible);
    const auto latestProgress = pluginProcessor.getDevelopmentProgress();
    const auto canUsePreview = ! capturing && latestProgress.has_value() && latestProgress->hasParameters;
    cancelButton.setButtonText (canUsePreview ? "Use current" : "Cancel");
    cancelButton.setTitle (canUsePreview ? "Stop and use the latest preview" : "Cancel capture or analysis");
    cancelButton.setTooltip (canUsePreview ? "Stop optimizing and apply the latest effect-chain preview"
                                           : "Stop the active listening or analysis task");

    if (transientStatus.isNotEmpty() && juce::Time::currentTimeMillis() < transientStatusExpiry)
    {
        workflowStatus.setColour (juce::Label::textColourId,
                                  transientStatusIsError ? palette::danger : palette::accent);
        workflowStatus.setText (transientStatus, juce::dontSendNotification);
        return;
    }
    transientStatus.clear();

    workflowStatus.setColour (juce::Label::textColourId, palette::muted);
    if (capturing)
        workflowStatus.setText ("Listening...", juce::dontSendNotification);
    else if (analysisState == DevelopmentAnalysisState::running
             || analysisState == DevelopmentAnalysisState::cancelling
             || analysisState == DevelopmentAnalysisState::applying
             || analysisState == DevelopmentAnalysisState::cancelled)
        workflowStatus.setText (pluginProcessor.getDevelopmentStatus(), juce::dontSendNotification);
    else if (analysisState == DevelopmentAnalysisState::succeeded)
        workflowStatus.setText (pluginProcessor.getDevelopmentStatus() + " - edit any module above",
                                juce::dontSendNotification);
    else if (analysisState == DevelopmentAnalysisState::failed)
    {
        workflowStatus.setColour (juce::Label::textColourId, palette::danger);
        workflowStatus.setText (pluginProcessor.getDevelopmentStatus(), juce::dontSendNotification);
    }
    else if (captureState == CaptureBuffer::State::ready)
        workflowStatus.setText ("Captured. Describe the change, then press Shape sound", juce::dontSendNotification);
    else
        workflowStatus.setText ("Play audio, then press Listen", juce::dontSendNotification);
}
} // namespace semantifx
