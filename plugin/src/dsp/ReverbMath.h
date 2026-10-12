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

// Pink-weighted spectral normalization (T16). Bin definition, shared with
// inference/src/semantifx_inference/effects/reverb.py (float64 both sides):
// K = 64 log-spaced frequencies f_k in [20 Hz, 20 kHz], geometric with
// inclusive endpoints, f_k = 20 * (20000 / 20)^(k / (K - 1)).
inline constexpr int kNormalizationBins = 64;
inline constexpr double kNormalizationFMin = 20.0;
inline constexpr double kNormalizationFMax = 20000.0;

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

inline double normalizationBinFrequency (int bin) noexcept
{
    const auto ratio = kNormalizationFMax / kNormalizationFMin;
    return kNormalizationFMin
         * std::pow (ratio,
                     static_cast<double> (bin)
                         / static_cast<double> (kNormalizationBins - 1));
}

// Pink-weighted spectral normalization scale for a built stereo IR:
// P_k = mean over channels of |DFT{h[c]}(f_k)|^2 (plain unwindowed DFT at
// exactly the kNormalizationBins frequencies), W_k = 1 / f_k,
// s = sqrt (sum_k W_k / sum_k W_k P_k), applied as h' = s * h.
// Everything accumulates in float64; a silent or missing IR yields 1.0.
inline double pinkNormalizationScale (
    const std::array<std::vector<float>, kNumChannels>& impulse)
{
    for (const auto& channel : impulse)
        if (channel.size() != static_cast<size_t> (kNumSamples))
            return 1.0;

    constexpr double pi = 3.14159265358979323846;
    const auto& first = impulse[0];
    const auto& second = impulse[1];
    double weightSum = 0.0;
    double weightedPower = 0.0;
    for (int bin = 0; bin < kNormalizationBins; ++bin)
    {
        const auto frequency = normalizationBinFrequency (bin);
        const auto weight = 1.0 / frequency;
        const auto phaseStep = 2.0 * pi * frequency / kSampleRate;
        double real0 = 0.0, imag0 = 0.0, real1 = 0.0, imag1 = 0.0;
        for (int sample = 0; sample < kNumSamples; ++sample)
        {
            const auto angle = phaseStep * static_cast<double> (sample);
            const auto cosine = std::cos (angle);
            const auto sine = std::sin (angle);
            const auto value0 = static_cast<double> (first[static_cast<size_t> (sample)]);
            const auto value1 = static_cast<double> (second[static_cast<size_t> (sample)]);
            real0 += value0 * cosine;
            imag0 -= value0 * sine;
            real1 += value1 * cosine;
            imag1 -= value1 * sine;
        }
        const auto power = (real0 * real0 + imag0 * imag0
                            + real1 * real1 + imag1 * imag1)
                         / static_cast<double> (kNumChannels);
        weightSum += weight;
        weightedPower += weight * power;
    }
    if (! (weightedPower > 0.0))
        return 1.0;
    return std::sqrt (weightSum / weightedPower);
}
} // namespace semantifx::dsp::reverb
