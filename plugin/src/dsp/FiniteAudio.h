#pragma once

#include <cmath>

namespace semantifx::dsp
{
inline void sanitizeAudio (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr)
        return;
    for (int channel = 0; channel < numChannels; ++channel)
    {
        if (channels[channel] == nullptr)
            continue;
        for (int sample = 0; sample < numSamples; ++sample)
            if (! std::isfinite (channels[channel][sample]))
                channels[channel][sample] = 0.0f;
    }
}
}
