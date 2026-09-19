#pragma once

#include "dsp/DenormalGuard.h"
#include "dsp/ReverbEffect.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>

namespace semantifx::dsp
{
/** JUCE's wait-free impulse-response loader keeps generated IR work off the audio thread. */
class JuceConvolver final : public Convolver
{
public:
    void prepare (double sampleRate, int maxBlock, int numChannels) override
    {
        configuredSampleRate = sampleRate;
        configuredChannels = std::clamp (numChannels, 1, reverb::kNumChannels);
        const juce::dsp::ProcessSpec spec {
            sampleRate,
            static_cast<juce::uint32> (std::max (1, maxBlock)),
            static_cast<juce::uint32> (configuredChannels)
        };
        convolution.prepare (spec);
    }

    void setImpulseResponse (const float* const* irChannels, int irLength) override
    {
        if (irChannels == nullptr || irLength <= 0)
        {
            convolution.reset();
            return;
        }

        juce::AudioBuffer<float> copy (reverb::kNumChannels, irLength);
        for (int channel = 0; channel < reverb::kNumChannels; ++channel)
        {
            auto* destination = copy.getWritePointer (channel);
            if (irChannels[channel] != nullptr)
                std::copy_n (irChannels[channel], irLength, destination);
            else
                std::fill_n (destination, irLength, 0.0f);
        }

        convolution.loadImpulseResponse (std::move (copy), configuredSampleRate,
                                         juce::dsp::Convolution::Stereo::yes,
                                         juce::dsp::Convolution::Trim::no,
                                         juce::dsp::Convolution::Normalise::no);
    }

    void reset() override { convolution.reset(); }

    void process (float* const* channels, int numChannels, int numSamples) override
    {
        semantifx::dsp::DenormalGuard denormalGuard;
        const auto channelCount = std::clamp (std::min (numChannels, configuredChannels),
                                              0, reverb::kNumChannels);
        if (channels == nullptr || channelCount == 0 || numSamples <= 0)
            return;
        juce::dsp::AudioBlock<float> block (channels,
                                            static_cast<size_t> (channelCount),
                                            static_cast<size_t> (numSamples));
        juce::dsp::ProcessContextReplacing<float> context (block);
        convolution.process (context);
    }

private:
    juce::dsp::Convolution convolution;
    double configuredSampleRate { reverb::kSampleRate };
    int configuredChannels { reverb::kNumChannels };
};
} // namespace semantifx::dsp
