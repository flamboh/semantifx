#include "DistortionEffect.h"

#include "DenormalGuard.h"
#include "EffectDefaults.h"

#include <algorithm>

namespace semantifx::dsp
{
DistortionEffect::DistortionEffect()
{
    targets = defaultsFor<parameters> ("distortion");
    current = targets;
}

void DistortionEffect::prepare (double newSampleRate, int)
{
    sampleRate = std::max (newSampleRate, 1.0);
    current = targets;
    reset();
    updateTone (current[2]);
}

void DistortionEffect::reset()
{
    for (auto& filter : toneFilters)
        filter.reset();
}

void DistortionEffect::setTargets (const float* values)
{
    std::copy (values, values + parameters, targets.begin());
}

void DistortionEffect::snapSmoothers()
{
    current = targets;
    updateTone (current[2]);
}

void DistortionEffect::updateTone (double toneDb) noexcept
{
    const auto coefficients = eq::highShelf (sampleRate, distortion::toneFrequencyHz,
                                             distortion::toneQ, toneDb);
    for (auto& filter : toneFilters)
        filter.setCoefficients (coefficients);
}

void DistortionEffect::process (float* const* channels, int numChannels, int numSamples)
{
    DenormalGuard denormalGuard;
    const auto channelCount = std::clamp (numChannels, 0, 2);
    if (channelCount == 0 || numSamples <= 0)
        return;

    const auto blockStart = current;
    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto progress = static_cast<float> (sample + 1)
                            / static_cast<float> (numSamples);
        for (size_t parameter = 0; parameter < targets.size(); ++parameter)
            current[parameter] = blockStart[parameter]
                               + progress * (targets[parameter] - blockStart[parameter]);
        updateTone (current[2]);

        for (int channel = 0; channel < channelCount; ++channel)
        {
            if (channels[channel] == nullptr)
                continue;
            auto& data = channels[channel];
            const auto input = data[sample];
            const auto shaped = static_cast<float> (
                distortion::shape (input, current[0], current[1]));
            const auto toned = toneFilters[static_cast<size_t> (channel)].process (shaped);
            data[sample] = distortion::blend (input, toned, current[3]);
        }
    }
}
} // namespace semantifx::dsp
