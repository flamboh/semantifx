#pragma once

#include <cmath>

namespace semantifx::dsp::compressor
{
inline double staticCurveDb (double xDb, double thresholdDb, double ratio, double kneeDb) noexcept
{
    const auto lower = thresholdDb - kneeDb / 2.0;
    const auto upper = thresholdDb + kneeDb / 2.0;
    if (xDb < lower)
        return xDb;
    if (xDb <= upper)
    {
        const auto distance = xDb - thresholdDb + kneeDb / 2.0;
        return xDb + (1.0 / ratio - 1.0) * distance * distance / (2.0 * kneeDb);
    }
    return thresholdDb + (xDb - thresholdDb) / ratio;
}

inline double attackCoefficient (double sampleRate, double attackMs) noexcept
{
    return std::exp (-std::log (9.0) / (sampleRate * attackMs / 1000.0));
}
} // namespace semantifx::dsp::compressor
