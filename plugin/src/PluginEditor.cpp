#include "PluginEditor.h"
#include "EqGraphMath.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace semantifx
{
namespace palette
{
const auto background = juce::Colour { 0xff1e1e2e };
const auto mantle = juce::Colour { 0xff181825 };
const auto surface = juce::Colour { 0xff313244 };
const auto line = juce::Colour { 0xff45475a };
const auto text = juce::Colour { 0xffcdd6f4 };
const auto muted = juce::Colour { 0xff7f849c };
const auto accent = juce::Colour { 0xffcba6f7 };
const auto danger = juce::Colour { 0xfff38ba8 };
} // namespace palette

class SemanticLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SemanticLookAndFeel()
    {
        setColour (juce::TextButton::textColourOffId, palette::text);
        setColour (juce::TextButton::textColourOnId, palette::background);
        setColour (juce::ToggleButton::textColourId, palette::text);
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
        g.fillRoundedRectangle (button.getLocalBounds().toFloat(), 4.0f);
        if (button.hasKeyboardFocus (true))
        {
            g.setColour (palette::accent);
            g.drawRoundedRectangle (button.getLocalBounds().toFloat().reduced (1.0f), 4.0f, 2.0f);
        }
    }

    void fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                   juce::TextEditor& editor) override
    {
        g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
        g.fillRect (0, 0, width, height);
    }

    void drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                juce::TextEditor& editor) override
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

class CardRack final : public juce::Component
{
public:
    CardRack (PluginProcessor& processor, std::function<void (std::optional<slots::Slot>)> selectCallback)
        : owner (processor), onSelect (std::move (selectCallback))
    {
        plusButton.setButtonText ("+");
        plusButton.setTooltip ("Add an effect to the chain");
        plusButton.setColour (juce::TextButton::buttonColourId, palette::surface);
        plusButton.onClick = [this] { showAddMenu(); };
        addAndMakeVisible (plusButton);
        setTitle ("Effect chain card rack");
        setDescription ("Drag cards to reorder the effect chain, or use the arrow keys");
    }

    void refresh()
    {
        const auto next = owner.getComposition();
        if (next == composition)
        {
            for (auto& card : cards)
                card->refresh();
            return;
        }
        composition = next;
        cards.clear();
        for (const auto slot : composition.items)
        {
            cards.push_back (std::make_unique<Card> (*this, slot));
            addAndMakeVisible (*cards.back());
        }
        resized();
    }

    void setSelected (std::optional<slots::Slot> slot)
    {
        selected = slot;
        for (auto& card : cards)
            card->repaint();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (2, 2);
        int x = area.getX();
        for (auto& card : cards)
        {
            card->setBounds (x, area.getY(), cardWidth, area.getHeight());
            x += cardWidth + gap;
        }
        plusButton.setBounds (x, area.getY(), plusWidth, area.getHeight());
    }

private:
    static constexpr int cardWidth = 188;
    static constexpr int gap = 8;
    static constexpr int plusWidth = 42;

    class Card final : public juce::Component
    {
    public:
        Card (CardRack& rack, slots::Slot slotValue)
            : owner (rack), slot (slotValue)
        {
            selectButton.setButtonText (juce::String { slots::slotTitle (slot) });
            selectButton.setColour (juce::TextButton::buttonColourId, palette::surface);
            selectButton.onClick = [this] { owner.select (slot); };
            bypassButton.setButtonText ("Bypass");
            bypassButton.setClickingTogglesState (true);
            bypassButton.setToggleState (owner.owner.isSlotBypassed (slot), juce::dontSendNotification);
            bypassButton.onClick = [this] { owner.owner.setSlotBypassed (slot, bypassButton.getToggleState()); };
            soloButton.setButtonText ("Solo");
            soloButton.setClickingTogglesState (true);
            soloButton.setToggleState (owner.owner.getSoloSlot() == slots::slotIndex (slot),
                                       juce::dontSendNotification);
            soloButton.onClick = [this]
            {
                owner.owner.setSoloSlot (soloButton.getToggleState() ? slots::slotIndex (slot) : -1);
                owner.refresh();
            };
            removeButton.setButtonText ("×");
            removeButton.setTooltip ("Remove this effect from the chain");
            removeButton.onClick = [this] { owner.remove (slot); };
            dragHandle.setText ("⋮⋮", juce::dontSendNotification);
            dragHandle.setJustificationType (juce::Justification::centred);
            dragHandle.setColour (juce::Label::textColourId, palette::muted);
            dragHandle.setInterceptsMouseClicks (false, false);
            setWantsKeyboardFocus (true);
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            addAndMakeVisible (selectButton);
            addAndMakeVisible (bypassButton);
            addAndMakeVisible (soloButton);
            addAndMakeVisible (removeButton);
            addAndMakeVisible (dragHandle);
        }

        void refresh()
        {
            bypassButton.setToggleState (owner.owner.isSlotBypassed (slot), juce::dontSendNotification);
            soloButton.setToggleState (owner.owner.getSoloSlot() == slots::slotIndex (slot),
                                       juce::dontSendNotification);
        }

        void paint (juce::Graphics& g) override
        {
            const auto selected = owner.selected.has_value() && *owner.selected == slot;
            g.setColour (selected ? palette::surface.brighter (0.08f) : palette::surface);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
            g.setColour (selected ? palette::accent : palette::line);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f,
                                    selected ? 2.0f : 1.0f);
            if (hasKeyboardFocus (true))
            {
                g.setColour (palette::accent);
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 3.0f, 1.0f);
            }
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (7, 5);
            removeButton.setBounds (area.removeFromRight (22));
            dragHandle.setBounds (area.removeFromRight (20));
            auto buttons = area.removeFromBottom (19);
            bypassButton.setBounds (buttons.removeFromLeft (75));
            buttons.removeFromLeft (5);
            soloButton.setBounds (buttons.removeFromLeft (55));
            selectButton.setBounds (area);
        }

        void mouseDown (const juce::MouseEvent& event) override
        {
            grabKeyboardFocus();
            dragStartX = event.getPosition().x;
            dragging = false;
        }

        void mouseDrag (const juce::MouseEvent& event) override
        {
            if (! dragging && std::abs (event.getDistanceFromDragStartX()) < 5)
                return;
            dragging = true;
            const auto x = getX() + event.getPosition().x - dragStartX;
            const auto target = juce::jlimit (0, owner.getWidth(), x + getWidth() / 2);
            owner.reorder (slot, target);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (! dragging)
                owner.select (slot);
            dragging = false;
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
            {
                owner.select (slot);
                return true;
            }
            if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
            {
                owner.remove (slot);
                return true;
            }
            const auto index = owner.indexOf (slot);
            if (key == juce::KeyPress::leftKey && index > 0)
            {
                owner.owner.moveSlot (index, index - 1);
                return true;
            }
            if (key == juce::KeyPress::rightKey && index >= 0
                && index + 1 < static_cast<int> (owner.composition.items.size()))
            {
                owner.owner.moveSlot (index, index + 1);
                return true;
            }
            return false;
        }

    private:
        CardRack& owner;
        slots::Slot slot;
        juce::TextButton selectButton, bypassButton, soloButton, removeButton;
        juce::Label dragHandle;
        int dragStartX {};
        bool dragging {};
    };

    int indexOf (slots::Slot slot) const
    {
        const auto found = std::find (composition.items.begin(), composition.items.end(), slot);
        return found == composition.items.end() ? -1 : static_cast<int> (found - composition.items.begin());
    }

    void select (slots::Slot slot) { onSelect (slot); }

    void remove (slots::Slot slot)
    {
        owner.removeSlot (slot);
        const auto next = owner.getComposition();
        onSelect (next.items.empty() ? std::optional<slots::Slot> {} : next.items.front());
    }

    void reorder (slots::Slot slot, int centerX)
    {
        const auto from = indexOf (slot);
        if (from < 0)
            return;
        auto to = 0;
        while (to < static_cast<int> (composition.items.size())
               && centerX > to * (cardWidth + gap) + cardWidth / 2)
            ++to;
        to = juce::jlimit (0, static_cast<int> (composition.items.size()) - 1, to);
        if (to != from)
        {
            owner.moveSlot (from, to);
            onSelect (slot);
        }
    }

    void showAddMenu()
    {
        juce::PopupMenu menu;
        const auto current = owner.getComposition();
        for (int typeIndex = 0; typeIndex < protocol::kEffectTypeCount; ++typeIndex)
        {
            const auto free = slots::freeSlotFor (typeIndex, current);
            const auto title = juce::String { slots::slotTitle ({ typeIndex, 1 }) }.upToLastOccurrenceOf (" ", false, false);
            menu.addItem (title, free.has_value(), false,
                          [this, typeIndex]
                          {
                              if (owner.addSlot (typeIndex))
                              {
                                  const auto next = owner.getComposition();
                                  if (! next.items.empty())
                                      onSelect (next.items.back());
                              }
                          });
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&plusButton));
    }

    PluginProcessor& owner;
    std::function<void (std::optional<slots::Slot>)> onSelect;
    slots::Composition composition;
    std::optional<slots::Slot> selected;
    std::vector<std::unique_ptr<Card>> cards;
    juce::TextButton plusButton;
};

class EqResponseGraph final : public juce::Component
{
public:
    class Node final : public juce::Component
    {
    public:
        Node (EqResponseGraph& graph, int nodeIndex) : owner (graph), index (nodeIndex)
        {
            setWantsKeyboardFocus (true);
            setTitle ("EQ band " + juce::String { index + 1 });
            setDescription ("Drag to change frequency and gain; use arrow keys for fine adjustment");
        }

        void paint (juce::Graphics& g) override
        {
            const auto selected = owner.selectedBand == index;
            auto circle = getLocalBounds().toFloat().reduced (selected ? 4.0f : 6.0f);
            g.setColour (selected ? palette::accent : palette::surface);
            g.fillEllipse (circle);
            g.setColour (selected ? palette::accent : palette::muted);
            g.drawEllipse (circle, 1.4f);
            g.setColour (selected ? palette::background : palette::text);
            g.setFont (juce::FontOptions { 12.5f, juce::Font::bold });
            g.drawText (juce::String { index + 1 }, getLocalBounds(), juce::Justification::centred);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
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

    EqResponseGraph (PluginProcessor& processor, std::function<void (int)> bandCallback)
        : owner (processor), onSelectionChanged (std::move (bandCallback))
    {
        for (size_t index = 0; index < nodes.size(); ++index)
        {
            nodes[index] = std::make_unique<Node> (*this, static_cast<int> (index));
            addAndMakeVisible (*nodes[index]);
        }
    }

    void setSlot (std::optional<slots::Slot> next)
    {
        selectedSlot = next;
        refresh();
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
        const auto plot = plotBounds();
        constexpr std::array<double, 10> frequencies { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
        g.setFont (juce::FontOptions { 10.5f });
        for (const auto frequency : frequencies)
        {
            const auto x = static_cast<float> (plot.getX() + eq::graph::frequencyToUnit (frequency) * plot.getWidth());
            g.setColour (palette::line.withAlpha (0.55f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            g.setColour (palette::muted.withAlpha (0.8f));
            const auto label = frequency >= 1000.0
                ? juce::String { frequency / 1000.0, frequency < 10000 ? 1 : 0 } + "k"
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

        const auto bands = readBands();
        juce::Path curve, fill;
        constexpr int points = 320;
        for (int point = 0; point < points; ++point)
        {
            const auto unit = static_cast<double> (point) / static_cast<double> (points - 1);
            const auto frequency = eq::graph::unitToFrequency (unit);
            const auto db = juce::jlimit (-24.0, 24.0,
                                          eq::graph::combinedMagnitudeDb (bands, frequency,
                                                                          owner.getSampleRate()));
            const auto x = static_cast<float> (plot.getX() + unit * plot.getWidth());
            const auto y = static_cast<float> (plot.getY() + eq::graph::gainToUnit (db) * plot.getHeight());
            if (point == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        fill = curve;
        fill.lineTo (plot.getRight(), plot.getCentreY());
        fill.lineTo (plot.getX(), plot.getCentreY());
        fill.closeSubPath();
        g.setColour (palette::accent.withAlpha (0.09f));
        g.fillPath (fill);
        g.setColour (palette::accent);
        g.strokePath (curve, juce::PathStrokeType { 2.5f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded });
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
            result[index] = { 0.0, 1000.0, 0.7071 };
            if (! selectedSlot.has_value())
                continue;
            result[index].gainDb = owner.getSlotParameterValue (*selectedSlot, static_cast<int> (index * 3));
            result[index].frequency = owner.getSlotParameterValue (*selectedSlot, static_cast<int> (index * 3 + 1));
            result[index].q = owner.getSlotParameterValue (*selectedSlot, static_cast<int> (index * 3 + 2));
        }
        return result;
    }

    juce::RangedAudioParameter* parameter (int band, int offset) const
    {
        return selectedSlot.has_value() ? owner.getSlotParameter (*selectedSlot, band * 3 + offset) : nullptr;
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

    void beginDrag (int index)
    {
        if (auto* frequency = parameter (index, 1)) frequency->beginChangeGesture();
        if (auto* gain = parameter (index, 0)) gain->beginChangeGesture();
    }

    void drag (int index, juce::Point<float> point)
    {
        const auto plot = plotBounds();
        if (plot.getWidth() <= 0.0f || plot.getHeight() <= 0.0f)
            return;
        if (auto* frequency = parameter (index, 1))
            frequency->setValueNotifyingHost (frequency->convertTo0to1 (
                static_cast<float> (eq::graph::unitToFrequency ((point.x - plot.getX()) / plot.getWidth()))));
        if (auto* gain = parameter (index, 0))
            gain->setValueNotifyingHost (gain->convertTo0to1 (
                static_cast<float> (eq::graph::unitToGain ((point.y - plot.getY()) / plot.getHeight()))));
        refresh();
    }

    void endDrag (int index)
    {
        if (auto* frequency = parameter (index, 1)) frequency->endChangeGesture();
        if (auto* gain = parameter (index, 0)) gain->endChangeGesture();
        owner.scheduleActiveAnalysisRestart();
    }

    bool nudge (int index, int horizontal, int vertical)
    {
        if (auto* frequency = parameter (index, 1))
        {
            const auto value = owner.getSlotParameterValue (*selectedSlot, index * 3 + 1);
            frequency->setValueNotifyingHost (frequency->convertTo0to1 (
                static_cast<float> (value * std::pow (2.0, horizontal / 24.0))));
        }
        if (auto* gain = parameter (index, 0))
        {
            const auto value = owner.getSlotParameterValue (*selectedSlot, index * 3);
            gain->setValueNotifyingHost (gain->convertTo0to1 (
                static_cast<float> (value + vertical * 0.5)));
        }
        owner.scheduleActiveAnalysisRestart();
        refresh();
        return true;
    }

    PluginProcessor& owner;
    std::function<void (int)> onSelectionChanged;
    std::array<std::unique_ptr<Node>, 6> nodes;
    std::optional<slots::Slot> selectedSlot;
    int selectedBand {};
};

PluginEditor::ParameterControl::ParameterControl (PluginEditor& editorValue,
                                                  bool restartAnalysis)
    : editor (editorValue), restartAnalysisOnDragEnd (restartAnalysis)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
    slider.setColour (juce::Slider::rotarySliderFillColourId, palette::accent);
    slider.setWantsKeyboardFocus (true);
    slider.onDragEnd = [this]
    {
        if (restartAnalysisOnDragEnd)
            editor.pluginProcessor.scheduleActiveAnalysisRestart();
    };
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, palette::muted);
    label.setFont (juce::FontOptions { 10.5f, juce::Font::bold });
    label.setInterceptsMouseClicks (false, false);
    editor.addChildComponent (slider);
    editor.addChildComponent (label);
}

void PluginEditor::ParameterControl::bind (const juce::String& parameterId,
                                           const juce::String& displayName,
                                           const juce::String& suffix)
{
    attachment.reset();
    slider.setTextValueSuffix (suffix);
    slider.setName (displayName);
    slider.setTitle (displayName);
    slider.setDescription ("Adjust " + displayName);
    label.setText (displayName.toUpperCase(), juce::dontSendNotification);
    if (parameterId.isEmpty())
    {
        clear();
        return;
    }
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        editor.pluginProcessor.parameters, parameterId, slider);
    slider.setVisible (true);
    label.setVisible (true);
}

void PluginEditor::ParameterControl::clear()
{
    attachment.reset();
    slider.setVisible (false);
    label.setVisible (false);
}

PluginEditor::PluginEditor (PluginProcessor& owner)
    : AudioProcessorEditor (&owner), pluginProcessor (owner),
      semanticLookAndFeel (std::make_unique<SemanticLookAndFeel>())
{
    setLookAndFeel (semanticLookAndFeel.get());
    setWantsKeyboardFocus (true);

    cardRack = std::make_unique<CardRack> (pluginProcessor,
        [this] (std::optional<slots::Slot> slot) { selectSlot (slot); });
    addAndMakeVisible (*cardRack);
    responseGraph = std::make_unique<EqResponseGraph> (pluginProcessor,
        [this] (int band) { selectedBand = band; refreshSelectedControls(); });
    addAndMakeVisible (*responseGraph);
    for (auto& control : controls)
        control = std::make_unique<ParameterControl> (*this, true);
    mixControl = std::make_unique<ParameterControl> (*this, false);
    mixControl->bind (parameter::mix, "Mix", " %");
    outputControl = std::make_unique<ParameterControl> (*this, false);
    outputControl->bind (parameter::outputGain, "Output", " dB");

    importButton.setTooltip ("Import an analysis_result JSON for the current chain");
    importButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    importButton.setColour (juce::TextButton::textColourOffId, palette::muted);
    importButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Import SemantiFX analysis result", juce::File {}, "*.json");
        importButton.setEnabled (false);
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
            [safeThis = juce::Component::SafePointer<PluginEditor> { this }] (const juce::FileChooser& chooser)
            {
                if (safeThis == nullptr) return;
                const auto file = chooser.getResult();
                if (file != juce::File {})
                {
                    const auto result = safeThis->pluginProcessor.applyAnalysisResultJson (file.loadFileAsString());
                    safeThis->showTransientStatus (result.wasOk() ? "Applied" : result.getErrorMessage(),
                                                   result.failed());
                }
                safeThis->importButton.setEnabled (true);
                safeThis->fileChooser.reset();
            });
    };
    addAndMakeVisible (importButton);

    promptEditor.setTextToShowWhenEmpty ("Describe how the sound should change", palette::muted);
    promptEditor.setMultiLine (false);
    promptEditor.setReturnKeyStartsNewLine (false);
    promptEditor.onReturnKey = [this] { beginShape(); };
    addAndMakeVisible (promptEditor);

    captureButton.setTooltip ("Capture ten seconds from the plugin input");
    captureButton.setColour (juce::TextButton::buttonColourId, palette::surface);
    captureButton.onClick = [this] { beginCapture(); };
    addAndMakeVisible (captureButton);
    shapeButton.setColour (juce::TextButton::buttonColourId, palette::accent);
    shapeButton.setColour (juce::TextButton::textColourOffId, palette::background);
    shapeButton.onClick = [this] { beginShape(); };
    addAndMakeVisible (shapeButton);
    optimizeButton.setColour (juce::TextButton::buttonColourId, palette::surface);
    optimizeButton.onClick = [this] { beginOptimize(); };
    addAndMakeVisible (optimizeButton);
    stopButton.setColour (juce::TextButton::buttonColourId, palette::danger.darker (0.35f));
    stopButton.onClick = [this] { stopWork(); };
    addAndMakeVisible (stopButton);
    revertButton.setColour (juce::TextButton::buttonColourId, palette::surface);
    revertButton.onClick = [this] { revertWork(); };
    addAndMakeVisible (revertButton);

    workflowStatus.setColour (juce::Label::textColourId, palette::muted);
    addAndMakeVisible (workflowStatus);
    emptyHint.setText ("Press Shape sound, or + to add an effect", juce::dontSendNotification);
    emptyHint.setColour (juce::Label::textColourId, palette::muted);
    emptyHint.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (emptyHint);

    cardRack->refresh();
    selectSlot (pluginProcessor.getComposition().items.empty()
                    ? std::optional<slots::Slot> {}
                    : std::optional<slots::Slot> { pluginProcessor.getComposition().items.front() });
    setResizable (true, true);
    setResizeLimits (900, 600, 1800, 1100);
    setSize (1120, 720);
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
    g.setFont (juce::FontOptions { 19.0f, juce::Font::bold });
    const auto width = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), "Semanti");
    g.setColour (palette::text);
    g.drawText ("Semanti", 24, 0, width, 56, juce::Justification::centredLeft);
    g.setColour (palette::accent);
    g.drawText ("FX", 24 + width, 0, 40, 56, juce::Justification::centredLeft);
    g.setColour (palette::line);
    g.drawHorizontalLine (56, 0.0f, static_cast<float> (getWidth()));
    const auto footerTop = getHeight() - 150;
    g.setColour (palette::mantle);
    g.fillRect (0, footerTop, getWidth(), 150);
    g.setColour (palette::line);
    g.drawHorizontalLine (footerTop, 0.0f, static_cast<float> (getWidth()));
}

void PluginEditor::resized()
{
    cardRack->setBounds (24, 10, getWidth() - 140, 43);
    importButton.setBounds (getWidth() - 94, 10, 70, 34);
    const auto footerTop = getHeight() - 150;
    auto main = juce::Rectangle<int> { 0, 61, getWidth(), footerTop - 61 };
    if (selectedSlot.has_value() && selectedSlot->typeIndex == 0)
    {
        auto knobStrip = main.removeFromBottom (102);
        responseGraph->setBounds (main.reduced (8, 4));
        auto knobs = knobStrip.withSizeKeepingCentre (3 * 122, knobStrip.getHeight());
        for (int index = 0; index < 3; ++index)
        {
            auto cell = knobs.removeFromLeft (122).reduced (6, 2);
            controls[static_cast<size_t> (index)]->label.setBounds (cell.removeFromTop (17));
            controls[static_cast<size_t> (index)]->slider.setBounds (cell);
        }
    }
    else
    {
        responseGraph->setVisible (false);
        const auto count = selectedSlot.has_value()
            ? static_cast<int> (protocol::effectTypes[static_cast<size_t> (selectedSlot->typeIndex)].parameterCount) : 0;
        auto knobs = main.withSizeKeepingCentre (juce::jmax (1, count) * 128,
                                                 std::min (210, main.getHeight() - 16));
        for (int index = 0; index < count; ++index)
        {
            auto cell = knobs.removeFromLeft (128).reduced (8, 3);
            controls[static_cast<size_t> (index)]->label.setBounds (cell.removeFromTop (18));
            controls[static_cast<size_t> (index)]->slider.setBounds (cell);
        }
    }

    auto footer = juce::Rectangle<int> { 24, footerTop + 14, getWidth() - 48, 122 };
    auto knobs = footer.removeFromRight (190);
    footer.removeFromRight (12);
    auto buttons = footer.removeFromBottom (34);
    captureButton.setBounds (buttons.removeFromLeft (108));
    buttons.removeFromLeft (7);
    shapeButton.setBounds (buttons.removeFromLeft (116));
    buttons.removeFromLeft (7);
    optimizeButton.setBounds (buttons.removeFromLeft (132));
    buttons.removeFromLeft (7);
    stopButton.setBounds (buttons.removeFromLeft (64));
    buttons.removeFromLeft (7);
    revertButton.setBounds (buttons.removeFromLeft (70));
    promptEditor.setBounds (footer.removeFromTop (37));
    footer.removeFromTop (7);
    workflowStatus.setBounds (footer);
    auto mixCell = knobs.removeFromLeft (92).reduced (4, 0);
    mixControl->label.setBounds (mixCell.removeFromTop (17));
    mixControl->slider.setBounds (mixCell);
    auto outputCell = knobs.reduced (4, 0);
    outputControl->label.setBounds (outputCell.removeFromTop (17));
    outputControl->slider.setBounds (outputCell);
    emptyHint.setBounds (main.reduced (30));
}

bool PluginEditor::keyPressed (const juce::KeyPress& key)
{
    const auto state = pluginProcessor.getDevelopmentAnalysisState();
    const auto active = state == DevelopmentAnalysisState::assembling
                     || state == DevelopmentAnalysisState::running
                     || state == DevelopmentAnalysisState::cancelling
                     || state == DevelopmentAnalysisState::applying;
    if (key == juce::KeyPress::escapeKey && active)
    {
        revertWork();
        return true;
    }
    return AudioProcessorEditor::keyPressed (key);
}

void PluginEditor::selectSlot (std::optional<slots::Slot> slot)
{
    selectedSlot = slot;
    cardRack->setSelected (selectedSlot);
    responseGraph->setSlot (selectedSlot.has_value() && selectedSlot->typeIndex == 0
                                ? selectedSlot : std::optional<slots::Slot> {});
    refreshSelectedControls();
}

void PluginEditor::refreshSelectedControls()
{
    for (auto& control : controls)
        control->clear();
    const auto visible = selectedSlot.has_value();
    emptyHint.setVisible (! visible);
    responseGraph->setVisible (visible && selectedSlot->typeIndex == 0);
    if (visible)
    {
        const auto& type = protocol::effectTypes[static_cast<size_t> (selectedSlot->typeIndex)];
        const auto first = type.firstParameter;
        if (selectedSlot->typeIndex == 0)
        {
            for (int offset = 0; offset < 3; ++offset)
            {
                const auto index = selectedBand * 3 + offset;
                const auto& spec = protocol::parameters[first + static_cast<size_t> (index)];
                controls[static_cast<size_t> (offset)]->bind (
                    parameter::slotParameterId (*selectedSlot, index),
                    juce::String { slots::slotTitle (*selectedSlot) } + " " + spec.displayName,
                    " " + juce::String { spec.unit });
            }
        }
        else
        {
            for (size_t index = 0; index < type.parameterCount; ++index)
            {
                const auto& spec = protocol::parameters[first + index];
                controls[index]->bind (
                    parameter::slotParameterId (*selectedSlot, static_cast<int> (index)),
                    juce::String { slots::slotTitle (*selectedSlot) } + " " + spec.displayName,
                    " " + juce::String { spec.unit });
            }
        }
    }
    resized();
}

void PluginEditor::beginCapture()
{
    const auto result = pluginProcessor.startDevelopmentCapture();
    if (result.failed()) showTransientStatus (result.getErrorMessage(), true);
}

void PluginEditor::beginShape()
{
    const auto result = pluginProcessor.startDevelopmentShape (promptEditor.getText().trim());
    if (result.failed()) showTransientStatus (result.getErrorMessage(), true);
}

void PluginEditor::beginOptimize()
{
    const auto result = pluginProcessor.startDevelopmentOptimization (promptEditor.getText().trim());
    if (result.failed()) showTransientStatus (result.getErrorMessage(), true);
}

void PluginEditor::stopWork()
{
    pluginProcessor.stopDevelopmentWork (false);
}

void PluginEditor::revertWork()
{
    pluginProcessor.stopDevelopmentWork (true);
}

void PluginEditor::showTransientStatus (const juce::String& text, bool isError)
{
    transientStatus = text;
    transientStatusIsError = isError;
    transientStatusExpiry = juce::Time::currentTimeMillis() + 5000;
}

void PluginEditor::timerCallback()
{
    cardRack->refresh();
    const auto composition = pluginProcessor.getComposition();
    if (! selectedSlot.has_value() || std::find (composition.items.begin(), composition.items.end(), *selectedSlot)
                                      == composition.items.end())
        selectSlot (composition.items.empty() ? std::optional<slots::Slot> {} : composition.items.front());
    else
    {
        responseGraph->refresh();
    }
    updateWorkflowState();
}

void PluginEditor::updateWorkflowState()
{
    const auto captureState = pluginProcessor.getCaptureState();
    const auto state = pluginProcessor.getDevelopmentAnalysisState();
    const auto capturing = captureState == CaptureBuffer::State::capturing;
    const auto active = state == DevelopmentAnalysisState::assembling
                     || state == DevelopmentAnalysisState::running
                     || state == DevelopmentAnalysisState::cancelling
                     || state == DevelopmentAnalysisState::applying;
    const auto promptReady = promptEditor.getText().trim().isNotEmpty();
    const auto chainReady = ! pluginProcessor.getComposition().items.empty();
    captureButton.setEnabled (! capturing && ! active);
    shapeButton.setEnabled (! active && captureState == CaptureBuffer::State::ready && promptReady);
    optimizeButton.setEnabled (! active && captureState == CaptureBuffer::State::ready
                               && promptReady && chainReady);
    stopButton.setVisible (active);
    revertButton.setVisible (active);
    stopButton.setEnabled (active);
    revertButton.setEnabled (active);
    importButton.setEnabled (! active);

    if (transientStatus.isNotEmpty() && juce::Time::currentTimeMillis() < transientStatusExpiry)
    {
        workflowStatus.setColour (juce::Label::textColourId,
                                  transientStatusIsError ? palette::danger : palette::accent);
        workflowStatus.setText (transientStatus, juce::dontSendNotification);
        return;
    }
    transientStatus.clear();
    workflowStatus.setColour (juce::Label::textColourId,
                              state == DevelopmentAnalysisState::failed ? palette::danger : palette::muted);
    if (capturing)
        workflowStatus.setText ("Listening...", juce::dontSendNotification);
    else if (active || state == DevelopmentAnalysisState::cancelled
             || state == DevelopmentAnalysisState::failed)
        workflowStatus.setText (pluginProcessor.getDevelopmentStatus(), juce::dontSendNotification);
    else if (state == DevelopmentAnalysisState::succeeded)
        workflowStatus.setText (pluginProcessor.getDevelopmentStatus(), juce::dontSendNotification);
    else if (captureState == CaptureBuffer::State::ready)
        workflowStatus.setText ("Captured. Describe the sound, then choose an action.", juce::dontSendNotification);
    else
        workflowStatus.setText ("Play audio, then press Listen 10s", juce::dontSendNotification);
}
} // namespace semantifx
