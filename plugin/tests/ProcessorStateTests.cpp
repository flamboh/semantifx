#include "PluginProcessor.h"

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

    std::cout << "All Semantifx processor-state tests passed\n";
}
