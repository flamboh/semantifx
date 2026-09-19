#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace semantifx::dsp::reverb
{
inline constexpr int kNumChannels = 2;
inline constexpr int kNumBands = 12;
inline constexpr int kNumSamples = 65536;
inline constexpr double kSampleRate = 44100.0;

struct ExpandedMacros
{
    std::array<double, kNumBands> bandGains {};
    std::array<double, kNumBands> bandDecays {};
};

inline ExpandedMacros expandMacros (double decay, double damping) noexcept
{
    ExpandedMacros expanded;
    for (int band = 0; band < kNumBands; ++band)
    {
        const auto position = static_cast<double> (band) / static_cast<double> (kNumBands - 1);
        expanded.bandDecays[static_cast<size_t> (band)] = std::clamp (
            (1.0 - decay) + damping * position, 0.0, 1.0);
        expanded.bandGains[static_cast<size_t> (band)] = std::clamp (
            1.0 - damping * position, 0.0, 1.0);
    }
    return expanded;
}

inline void buildImpulseResponse (
    const float* const noiseChannels[kNumChannels],
    const ExpandedMacros& expanded,
    std::array<std::vector<float>, kNumChannels>& impulse)
{
    for (int channel = 0; channel < kNumChannels; ++channel)
    {
        impulse[static_cast<size_t> (channel)].assign (kNumSamples, 0.0f);
        if (noiseChannels[channel] == nullptr)
            continue;

        for (int sample = 0; sample < kNumSamples; ++sample)
        {
            const auto time = static_cast<float> (sample)
                            / static_cast<float> (kNumSamples - 1);
            float sum = 0.0f;
            for (int band = 0; band < kNumBands; ++band)
            {
                const auto gain = static_cast<float> (
                    expanded.bandGains[static_cast<size_t> (band)]);
                const auto decay = static_cast<float> (
                    expanded.bandDecays[static_cast<size_t> (band)]);
                const auto envelope = static_cast<float> (
                    std::exp (-static_cast<double> (10.0f * decay + 1.0f) * time));
                const auto noise = noiseChannels[channel][
                    static_cast<size_t> (band) * kNumSamples + sample];
                sum += noise * gain * envelope;
            }
            impulse[static_cast<size_t> (channel)][static_cast<size_t> (sample)]
                = sum / static_cast<float> (kNumBands);
        }
    }
}
} // namespace semantifx::dsp::reverb
