#include "PluginProcessor.h"
#include "DistortionMath.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    semantifx::PluginProcessor processor;
    std::vector<float> expected;
    expected.reserve (static_cast<size_t> (processor.getParameters().size()));

    for (int index = 0; index < processor.getParameters().size(); ++index)
    {
        const auto value = index == processor.getParameters().size() - 1
                               ? 0.321689f
                               : static_cast<float> (index + 1) / static_cast<float> (processor.getParameters().size() + 2);
        processor.getParameters()[index]->setValueNotifyingHost (value);
        expected.push_back (processor.getParameters()[index]->getValue());
    }

    juce::MemoryBlock state;
    processor.getStateInformation (state);
    assert (! state.isEmpty());

    for (auto* parameter : processor.getParameters())
        parameter->setValueNotifyingHost (0.0f);
    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    for (int index = 0; index < processor.getParameters().size(); ++index)
        assert (std::abs (processor.getParameters()[index]->getValue() - expected[static_cast<size_t> (index)]) < 1.0e-5f);

    auto* bypass = processor.parameters.getParameter (semantifx::parameter::bypass);
    assert (bypass != nullptr);
    assert (std::abs (bypass->getValue() - 0.321689f) < 1.0e-5f);

    assert (processor.parameters.getParameter (semantifx::parameter::distortionDrive) != nullptr);
    assert (processor.parameters.getParameter (semantifx::parameter::distortionCharacter) != nullptr);
    assert (processor.parameters.getParameter (semantifx::parameter::distortionTone) != nullptr);
    assert (processor.parameters.getParameter (semantifx::parameter::distortionMix) != nullptr);
    assert (processor.parameters.getParameter (semantifx::parameter::effectOrder) != nullptr);

    semantifx::PluginProcessor audioProcessor;
    const auto setPhysical = [&audioProcessor] (const char* id, float physical)
    {
        auto* parameter = audioProcessor.parameters.getParameter (id);
        assert (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (physical));
    };
    setPhysical (semantifx::parameter::distortionDrive, 36.0f);
    setPhysical (semantifx::parameter::distortionCharacter, 1.0f);
    setPhysical (semantifx::parameter::distortionTone, 0.0f);
    setPhysical (semantifx::parameter::distortionMix, 1.0f);
    setPhysical (semantifx::parameter::outputGain, 0.0f);
    setPhysical (semantifx::parameter::mix, 100.0f);
    setPhysical (semantifx::parameter::bypass, 0.0f);
    audioProcessor.prepareToPlay (48000.0, 8);

    juce::AudioBuffer<float> buffer { 1, 4 };
    const std::array<float, 4> input { -0.75f, -0.25f, 0.25f, 0.75f };
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        buffer.setSample (0, sample, input[static_cast<size_t> (sample)]);
    juce::MidiBuffer midi;
    audioProcessor.processBlock (buffer, midi);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto expectedSample = semantifx::distortion::shape (
            input[static_cast<size_t> (sample)], 36.0, 1.0);
        assert (std::abs (buffer.getSample (0, sample) - expectedSample) < 1.0e-4);
    }

    const auto renderOrder = [] (float order)
    {
        semantifx::PluginProcessor orderedProcessor;
        const auto set = [&orderedProcessor] (const char* id, float physical)
        {
            auto* parameter = orderedProcessor.parameters.getParameter (id);
            assert (parameter != nullptr);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (physical));
        };
        set (semantifx::parameter::lowGain, 12.0f);
        set (semantifx::parameter::distortionDrive, 36.0f);
        set (semantifx::parameter::distortionCharacter, 0.8f);
        set (semantifx::parameter::distortionTone, 0.0f);
        set (semantifx::parameter::distortionMix, 1.0f);
        set (semantifx::parameter::effectOrder, order);
        set (semantifx::parameter::mix, 100.0f);
        orderedProcessor.prepareToPlay (48000.0, 2048);

        juce::AudioBuffer<float> orderedBuffer { 1, 2048 };
        for (int sample = 0; sample < orderedBuffer.getNumSamples(); ++sample)
            orderedBuffer.setSample (0, sample,
                0.6f * std::sin (2.0f * juce::MathConstants<float>::pi * 100.0f
                                 * static_cast<float> (sample) / 48000.0f));
        juce::MidiBuffer orderedMidi;
        orderedProcessor.processBlock (orderedBuffer, orderedMidi);
        std::vector<float> rendered (static_cast<size_t> (orderedBuffer.getNumSamples()));
        for (int sample = 0; sample < orderedBuffer.getNumSamples(); ++sample)
            rendered[static_cast<size_t> (sample)] = orderedBuffer.getSample (0, sample);
        return rendered;
    };

    const auto eqFirst = renderOrder (0.0f);
    const auto distortionFirst = renderOrder (1.0f);
    double orderDifference {};
    for (size_t sample = 0; sample < eqFirst.size(); ++sample)
        orderDifference += std::abs (eqFirst[sample] - distortionFirst[sample]);
    assert (orderDifference / static_cast<double> (eqFirst.size()) > 1.0e-3);

    std::cout << "All Semantifx processor-state tests passed\n";
}
