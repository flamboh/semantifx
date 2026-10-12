#include "EqEffect.h"

#include "DenormalGuard.h"
#include "EffectDefaults.h"
#include "FiniteAudio.h"

#include <algorithm>

namespace semantifx::dsp
{
EqEffect::EqEffect()
{
    targets = defaultsFor<parameters> ("eq");
    current = targets;
}

void EqEffect::prepare (double newSampleRate, int)
{
    sampleRate = std::max (newSampleRate, 1.0);
    current = targets;
    reset();
    updateCoefficients (current);
}

void EqEffect::reset()
{
    for (auto& bank : filters)
        for (auto& filter : bank)
            filter.reset();
}

void EqEffect::setTargets (const float* values)
{
    std::copy (values, values + parameters, targets.begin());
}

void EqEffect::snapSmoothers()
{
    current = targets;
    updateCoefficients (current);
}

void EqEffect::updateCoefficients (const std::array<float, parameters>& values) noexcept
{
    for (size_t band = 0; band < 6; ++band)
    {
        const auto offset = band * 3;
        const auto gain = static_cast<double> (values[offset]);
        const auto frequency = static_cast<double> (values[offset + 1]);
        const auto q = static_cast<double> (values[offset + 2]);
        const auto coefficients = band == 0
            ? eq::lowShelf (sampleRate, frequency, q, gain)
            : band == 5 ? eq::highShelf (sampleRate, frequency, q, gain)
                        : eq::peak (sampleRate, frequency, q, gain);
        for (auto& filter : filters[band])
            filter.setCoefficients (coefficients);
    }
}

void EqEffect::process (float* const* channels, int numChannels, int numSamples)
{
    DenormalGuard denormalGuard;
    const auto channelCount = std::clamp (numChannels, 0, 2);
    sanitizeAudio (channels, channelCount, numSamples);
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
        // The plugin updates coefficients in small chunks. This standalone seam uses
        // a per-block linear ramp and recalculates at each ramp point, which keeps
        // it independent of a host's block size after snapSmoothers().
        updateCoefficients (current);

        for (int channel = 0; channel < channelCount; ++channel)
        {
            if (channels[channel] == nullptr)
                continue;
            auto& data = channels[channel];
            auto value = data[sample];
            for (auto& bank : filters)
                value = bank[static_cast<size_t> (channel)].process (value);
            data[sample] = value;
        }
    }
}
} // namespace semantifx::dsp
