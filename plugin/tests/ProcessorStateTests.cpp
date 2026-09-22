#include "PluginProcessor.h"
#include "DistortionMath.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
using semantifx::PluginProcessor;
using semantifx::slots::Composition;
using semantifx::slots::Slot;

void setPhysical (PluginProcessor& processor, const juce::String& id, float physical)
{
    auto* parameter = processor.parameters.getParameter (id);
    assert (parameter != nullptr);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (physical));
}

void setPhysical (PluginProcessor& processor, Slot slot, int parameterIndex, float physical)
{
    setPhysical (processor, semantifx::parameter::slotParameterId (slot, parameterIndex), physical);
}

void setGlobalDefaults (PluginProcessor& processor)
{
    setPhysical (processor, semantifx::parameter::outputGain, 0.0f);
    setPhysical (processor, semantifx::parameter::mix, 100.0f);
    setPhysical (processor, semantifx::parameter::bypass, 0.0f);
}

std::vector<float> render (Composition composition, void (*configure) (PluginProcessor&))
{
    PluginProcessor processor;
    processor.setComposition (composition);
    setGlobalDefaults (processor);
    configure (processor);
    processor.prepareToPlay (48000.0, 2048);
    juce::AudioBuffer<float> buffer { 1, 2048 };
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        buffer.setSample (0, sample,
            0.6f * std::sin (2.0f * juce::MathConstants<float>::pi * 100.0f
                             * static_cast<float> (sample) / 48000.0f));
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
    std::vector<float> values (static_cast<size_t> (buffer.getNumSamples()));
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        values[static_cast<size_t> (sample)] = buffer.getSample (0, sample);
    return values;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    PluginProcessor processor;
    processor.setComposition ({ { Slot { 0, 1 }, Slot { 1, 1 } } });
    std::vector<float> expected;
    for (int index = 0; index < processor.getParameters().size(); ++index)
    {
        const auto value = static_cast<float> (index + 1)
                         / static_cast<float> (processor.getParameters().size() + 2);
        processor.getParameters()[index]->setValueNotifyingHost (value);
        expected.push_back (processor.getParameters()[index]->getValue());
    }

    juce::MemoryBlock state;
    processor.getStateInformation (state);
    assert (! state.isEmpty());
    processor.setComposition ({ });
    for (auto* parameter : processor.getParameters())
        parameter->setValueNotifyingHost (0.0f);
    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    for (int index = 0; index < processor.getParameters().size(); ++index)
        assert (std::abs (processor.getParameters()[index]->getValue()
                          - expected[static_cast<size_t> (index)]) < 1.0e-5f);
    assert (semantifx::slots::serializeComposition (processor.getComposition()) == "eq-1,distortion-1");
    assert (processor.parameters.getParameter ("effect_order") == nullptr);

    PluginProcessor empty;
    setGlobalDefaults (empty);
    empty.prepareToPlay (48000.0, 8);
    juce::AudioBuffer<float> unity { 1, 4 };
    const std::array<float, 4> input { -0.75f, -0.25f, 0.25f, 0.75f };
    for (int sample = 0; sample < 4; ++sample) unity.setSample (0, sample, input[static_cast<size_t> (sample)]);
    juce::MidiBuffer midi;
    empty.processBlock (unity, midi);
    for (int sample = 0; sample < 4; ++sample)
        assert (std::abs (unity.getSample (0, sample) - input[static_cast<size_t> (sample)]) < 1.0e-6f);

    PluginProcessor distortion;
    distortion.setComposition ({ { Slot { 1, 1 } } });
    setGlobalDefaults (distortion);
    setPhysical (distortion, Slot { 1, 1 }, 0, 36.0f);
    setPhysical (distortion, Slot { 1, 1 }, 1, 1.0f);
    setPhysical (distortion, Slot { 1, 1 }, 2, 0.0f);
    setPhysical (distortion, Slot { 1, 1 }, 3, 1.0f);
    distortion.prepareToPlay (48000.0, 8);
    juce::AudioBuffer<float> shaped { 1, 4 };
    for (int sample = 0; sample < 4; ++sample) shaped.setSample (0, sample, input[static_cast<size_t> (sample)]);
    distortion.processBlock (shaped, midi);
    for (int sample = 0; sample < 4; ++sample)
        assert (std::abs (shaped.getSample (0, sample)
                          - semantifx::distortion::shape (input[static_cast<size_t> (sample)], 36.0, 1.0)) < 1.0e-4f);

    const auto eqFirst = render ({ { Slot { 0, 1 }, Slot { 1, 1 } } }, [] (PluginProcessor& value)
    {
        setPhysical (value, Slot { 0, 1 }, 0, 12.0f);
        setPhysical (value, Slot { 1, 1 }, 0, 36.0f);
        setPhysical (value, Slot { 1, 1 }, 1, 0.8f);
        setPhysical (value, Slot { 1, 1 }, 3, 1.0f);
    });
    const auto distortionFirst = render ({ { Slot { 1, 1 }, Slot { 0, 1 } } }, [] (PluginProcessor& value)
    {
        setPhysical (value, Slot { 0, 1 }, 0, 12.0f);
        setPhysical (value, Slot { 1, 1 }, 0, 36.0f);
        setPhysical (value, Slot { 1, 1 }, 1, 0.8f);
        setPhysical (value, Slot { 1, 1 }, 3, 1.0f);
    });
    double orderDifference {};
    for (size_t index = 0; index < eqFirst.size(); ++index)
        orderDifference += std::abs (eqFirst[index] - distortionFirst[index]);
    assert (orderDifference / static_cast<double> (eqFirst.size()) > 1.0e-3);

    PluginProcessor bypassed;
    bypassed.setComposition ({ { Slot { 1, 1 } } });
    setGlobalDefaults (bypassed);
    setPhysical (bypassed, Slot { 1, 1 }, 0, 36.0f);
    setPhysical (bypassed, Slot { 1, 1 }, 3, 1.0f);
    setPhysical (bypassed, semantifx::parameter::slotBypassId ({ 1, 1 }), 1.0f);
    bypassed.prepareToPlay (48000.0, 8);
    juce::AudioBuffer<float> bypassBuffer { 1, 4 };
    for (int sample = 0; sample < 4; ++sample) bypassBuffer.setSample (0, sample, input[static_cast<size_t> (sample)]);
    bypassed.processBlock (bypassBuffer, midi);
    for (int sample = 0; sample < 4; ++sample)
        assert (std::abs (bypassBuffer.getSample (0, sample) - input[static_cast<size_t> (sample)]) < 1.0e-6f);

    PluginProcessor solo;
    solo.setComposition ({ { Slot { 0, 1 }, Slot { 1, 1 } } });
    setGlobalDefaults (solo);
    setPhysical (solo, Slot { 0, 1 }, 0, 12.0f);
    setPhysical (solo, Slot { 1, 1 }, 0, 36.0f);
    setPhysical (solo, Slot { 1, 1 }, 1, 0.8f);
    setPhysical (solo, Slot { 1, 1 }, 3, 1.0f);
    // Solo takes a slot index, not a chain position: distortion-1 is slot 2.
    solo.setSoloSlot (semantifx::slots::slotIndex (Slot { 1, 1 }));
    solo.prepareToPlay (48000.0, 8);
    juce::AudioBuffer<float> soloBuffer { 1, 4 };
    for (int sample = 0; sample < 4; ++sample) soloBuffer.setSample (0, sample, input[static_cast<size_t> (sample)]);
    solo.processBlock (soloBuffer, midi);

    // The soloed path must match a distortion-only chain fed identical input.
    PluginProcessor distortionOnlyProcessor;
    distortionOnlyProcessor.setComposition ({ { Slot { 1, 1 } } });
    setGlobalDefaults (distortionOnlyProcessor);
    setPhysical (distortionOnlyProcessor, Slot { 1, 1 }, 0, 36.0f);
    setPhysical (distortionOnlyProcessor, Slot { 1, 1 }, 1, 0.8f);
    setPhysical (distortionOnlyProcessor, Slot { 1, 1 }, 3, 1.0f);
    distortionOnlyProcessor.prepareToPlay (48000.0, 8);
    juce::AudioBuffer<float> distortionOnlyBuffer { 1, 4 };
    for (int sample = 0; sample < 4; ++sample)
        distortionOnlyBuffer.setSample (0, sample, input[static_cast<size_t> (sample)]);
    distortionOnlyProcessor.processBlock (distortionOnlyBuffer, midi);
    for (int sample = 0; sample < 4; ++sample)
        assert (std::abs (soloBuffer.getSample (0, sample)
                          - distortionOnlyBuffer.getSample (0, sample)) < 1.0e-6f);

    std::cout << "All SemantiFX processor-state tests passed\n";
}
