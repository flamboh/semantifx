#pragma once

#include "EqMath.h"

#include <array>
#include <cmath>

namespace semantifx::eq::graph
{
inline constexpr double minimumFrequency = 20.0;
inline constexpr double maximumFrequency = 20000.0;
inline constexpr double minimumGainDb = -24.0;
inline constexpr double maximumGainDb = 24.0;

struct Band
{
    double gainDb {};
    double frequency { 1000.0 };
    double q { 0.7071 };
};

inline double frequencyToUnit (double frequency) noexcept
{
    const auto clamped = std::clamp (frequency, minimumFrequency, maximumFrequency);
    return std::log (clamped / minimumFrequency) / std::log (maximumFrequency / minimumFrequency);
}

inline double unitToFrequency (double unit) noexcept
{
    return minimumFrequency * std::pow (maximumFrequency / minimumFrequency,
                                        std::clamp (unit, 0.0, 1.0));
}

inline double gainToUnit (double gainDb) noexcept
{
    return (maximumGainDb - std::clamp (gainDb, minimumGainDb, maximumGainDb))
         / (maximumGainDb - minimumGainDb);
}

inline double unitToGain (double unit) noexcept
{
    return maximumGainDb - std::clamp (unit, 0.0, 1.0) * (maximumGainDb - minimumGainDb);
}

inline Coefficients coefficientsForBand (size_t index, double sampleRate, const Band& band) noexcept
{
    if (index == 0)
        return lowShelf (sampleRate, band.frequency, band.q, band.gainDb);
    if (index == 5)
        return highShelf (sampleRate, band.frequency, band.q, band.gainDb);
    return peak (sampleRate, band.frequency, band.q, band.gainDb);
}

inline double combinedMagnitudeDb (const std::array<Band, 6>& bands, double frequency,
                                   double sampleRate) noexcept
{
    double total {};
    for (size_t index = 0; index < bands.size(); ++index)
        total += magnitudeDb (coefficientsForBand (index, sampleRate, bands[index]), frequency, sampleRate);
    return total;
}
} // namespace semantifx::eq::graph
