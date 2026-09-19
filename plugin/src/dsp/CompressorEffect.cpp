#include "CompressorEffect.h"

#include <algorithm>
#include <cmath>

namespace semantifx::dsp
{
void CompressorEffect::prepare (double newSampleRate, int)
{
    sampleRate = std::max (newSampleRate, 1.0);
    current = targets;
    reset();
}

void CompressorEffect::reset()
{
    gainReductionDb = 0.0;
}

void CompressorEffect::setTargets (const float* values)
{
    std::copy (values, values + parameters, targets.begin());
    current = targets;
}

void CompressorEffect::snapSmoothers()
{
    current = targets;
}

void CompressorEffect::process (float* const* channels, int numChannels, int numSamples)
{
    const auto channelCount = std::clamp (numChannels, 0, 2);
    if (channelCount == 0 || numSamples <= 0)
        return;

    const auto thresholdDb = static_cast<double> (current[0]);
    const auto ratio = static_cast<double> (current[1]);
    const auto attackMs = static_cast<double> (current[2]);
    const auto kneeDb = static_cast<double> (current[3]);
    const auto makeupGainDb = static_cast<double> (current[4]);
    const auto alpha = compressor::attackCoefficient (sampleRate, attackMs);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        double sidechain = 0.0;
        for (int channel = 0; channel < channelCount; ++channel)
            if (channels[channel] != nullptr)
                sidechain += static_cast<double> (channels[channel][sample]);

        const auto levelDb = 20.0 * std::log10 (std::max (std::abs (sidechain), 1.0e-8));
        const auto staticDb = compressor::staticCurveDb (levelDb, thresholdDb, ratio, kneeDb);
        const auto targetGainReductionDb = staticDb - levelDb;
        gainReductionDb = (1.0 - alpha) * targetGainReductionDb + alpha * gainReductionDb;
        const auto gain = std::pow (10.0, (gainReductionDb + makeupGainDb) / 20.0);

        for (int channel = 0; channel < channelCount; ++channel)
            if (channels[channel] != nullptr)
                channels[channel][sample] = static_cast<float> (
                    static_cast<double> (channels[channel][sample]) * gain);
    }
}
} // namespace semantifx::dsp
