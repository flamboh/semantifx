#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace semantifx
{
namespace
{
constexpr double parameterRampSeconds = 0.05;
constexpr float bypassRampSeconds = 0.02f;
constexpr int coefficientUpdateInterval = 32;

juce::NormalisableRange<float> logarithmicRange (float minimum, float maximum)
{
    juce::NormalisableRange<float> range { minimum, maximum };
    range.setSkewForCentre (std::sqrt (minimum * maximum));
    return range;
}

float value (const juce::AudioProcessorValueTreeState& state, const char* id) noexcept
{
    return state.getRawParameterValue (id)->load();
}
} // namespace

PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    using FloatParameter = juce::AudioParameterFloat;
    using BoolParameter = juce::AudioParameterBool;
    using ID = juce::ParameterID;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto addBand = [&layout] (parameter::BandIDs ids, const juce::String& name,
                                    float minimumFrequency, float maximumFrequency, float defaultFrequency)
    {
        layout.add (std::make_unique<FloatParameter> (ID { ids.gain, 1 }, name + " Gain",
                                                       juce::NormalisableRange<float> { -20.0f, 20.0f, 0.1f }, 0.0f));
        layout.add (std::make_unique<FloatParameter> (ID { ids.frequency, 1 }, name + " Frequency",
                                                       logarithmicRange (minimumFrequency, maximumFrequency), defaultFrequency));
        layout.add (std::make_unique<FloatParameter> (ID { ids.q, 1 }, name + " Q",
                                                       logarithmicRange (0.1f, 6.0f), 0.7071f));
    };

    addBand (parameter::bands[0], "Low Shelf", 20.0f, 2000.0f, 200.0f);
    addBand (parameter::bands[1], "Low Mid", 80.0f, 2000.0f, 500.0f);
    addBand (parameter::bands[2], "Mid", 2000.0f, 8000.0f, 3500.0f);
    addBand (parameter::bands[3], "High Mid", 8000.0f, 12000.0f, 9000.0f);
    addBand (parameter::bands[4], "Air", 12000.0f, 21050.0f, 14000.0f);
    addBand (parameter::bands[5], "High Shelf", 4000.0f, 21050.0f, 12000.0f);
    layout.add (std::make_unique<FloatParameter> (ID { parameter::outputGain, 1 }, "Output Gain",
                                                   juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f));
    layout.add (std::make_unique<FloatParameter> (ID { parameter::mix, 1 }, "Mix",
                                                   juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f));
    layout.add (std::make_unique<BoolParameter> (ID { parameter::bypass, 1 }, "Bypass", false));
    return layout;
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    return input == layouts.getMainOutputChannelSet()
        && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

void PluginProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = std::max (sampleRate, 1.0);
    for (auto& bank : filters)
        for (auto& filter : bank)
            filter.reset();
    resetSmoothers();
}

void PluginProcessor::resetSmoothers()
{
    for (size_t index = 0; index < bandSmoothers.size(); ++index)
    {
        auto& smoother = bandSmoothers[index];
        smoother.gain.reset (currentSampleRate, parameterRampSeconds);
        smoother.frequency.reset (currentSampleRate, parameterRampSeconds);
        smoother.q.reset (currentSampleRate, parameterRampSeconds);
        smoother.gain.setCurrentAndTargetValue (value (parameters, parameter::bands[index].gain));
        smoother.frequency.setCurrentAndTargetValue (value (parameters, parameter::bands[index].frequency));
        smoother.q.setCurrentAndTargetValue (value (parameters, parameter::bands[index].q));
    }
    outputGainSmooth.reset (currentSampleRate, bypassRampSeconds);
    wetMixSmooth.reset (currentSampleRate, bypassRampSeconds);
    outputGainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (value (parameters, parameter::outputGain)));
    const auto bypassed = value (parameters, parameter::bypass) >= 0.5f;
    wetMixSmooth.setCurrentAndTargetValue (bypassed ? 0.0f : value (parameters, parameter::mix) * 0.01f);
}

void PluginProcessor::updateTargets() noexcept
{
    for (size_t index = 0; index < bandSmoothers.size(); ++index)
    {
        auto& smoother = bandSmoothers[index];
        smoother.gain.setTargetValue (value (parameters, parameter::bands[index].gain));
        smoother.frequency.setTargetValue (value (parameters, parameter::bands[index].frequency));
        smoother.q.setTargetValue (value (parameters, parameter::bands[index].q));
    }
    outputGainSmooth.setTargetValue (juce::Decibels::decibelsToGain (value (parameters, parameter::outputGain)));
    const auto bypassed = value (parameters, parameter::bypass) >= 0.5f;
    wetMixSmooth.setTargetValue (bypassed ? 0.0f : value (parameters, parameter::mix) * 0.01f);
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    updateTargets();

    const auto channels = std::min (buffer.getNumChannels(), 2);
    const auto samples = buffer.getNumSamples();
    for (int start = 0; start < samples; start += coefficientUpdateInterval)
    {
        const auto count = std::min (coefficientUpdateInterval, samples - start);
        std::array<eq::Coefficients, 6> nextCoefficients;
        for (size_t index = 0; index < bandSmoothers.size(); ++index)
        {
            auto& smoother = bandSmoothers[index];
            const auto gain = smoother.gain.skip (count);
            const auto frequency = smoother.frequency.skip (count);
            const auto q = smoother.q.skip (count);
            nextCoefficients[index] = index == 0 ? eq::lowShelf (currentSampleRate, frequency, q, gain)
                                    : index == 5 ? eq::highShelf (currentSampleRate, frequency, q, gain)
                                                 : eq::peak (currentSampleRate, frequency, q, gain);
        }

        for (int channel = 0; channel < channels; ++channel)
            for (size_t index = 0; index < filters.size(); ++index)
                filters[index][static_cast<size_t> (channel)].setCoefficients (nextCoefficients[index]);

        for (int sample = start; sample < start + count; ++sample)
        {
            const auto outputGain = outputGainSmooth.getNextValue();
            const auto wetMix = wetMixSmooth.getNextValue();
            for (int channel = 0; channel < channels; ++channel)
            {
                auto* data = buffer.getWritePointer (channel);
                const auto dry = data[sample];
                auto wet = dry;
                for (auto& bank : filters)
                    wet = bank[static_cast<size_t> (channel)].process (wet);
                wet *= outputGain;
                data[sample] = dry + wetMix * (wet - dry);
            }
        }
    }
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destination);
}

void PluginProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size); xml != nullptr)
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::Result PluginProcessor::applyAnalysisResultJson (const juce::String& json)
{
    auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
    if (messageManager == nullptr || ! messageManager->isThisTheMessageThread())
        return juce::Result::fail ("Analysis results must be applied on the message thread.");

    protocol::AnalysisResult result;
    if (const auto parsed = protocol::parseAnalysisResult (json, result); parsed.failed())
        return parsed;

    std::array<juce::RangedAudioParameter*, protocol::canonicalParameterNames.size()> targets {};
    std::array<float, protocol::canonicalParameterNames.size()> clampedValues {};
    for (size_t index = 0; index < protocol::canonicalParameterNames.size(); ++index)
    {
        targets[index] = parameters.getParameter (protocol::canonicalParameterNames[index]);
        if (targets[index] == nullptr)
            return juce::Result::fail ("Plugin parameter mapping is incomplete.");

        const auto& range = targets[index]->getNormalisableRange();
        clampedValues[index] = range.snapToLegalValue (std::clamp (static_cast<float> (result.values[index]),
                                                                   range.start, range.end));
    }

    for (size_t index = 0; index < targets.size(); ++index)
    {
        targets[index]->beginChangeGesture();
        targets[index]->setValueNotifyingHost (targets[index]->convertTo0to1 (clampedValues[index]));
        targets[index]->endChangeGesture();
    }

    return juce::Result::ok();
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}
} // namespace semantifx

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new semantifx::PluginProcessor();
}
