#pragma once

#include <algorithm>
#include <cmath>

namespace semantifx::distortion
{
inline constexpr double minimumDriveDb = 0.0;
inline constexpr double maximumDriveDb = 48.0;
inline constexpr double minimumToneDb = -12.0;
inline constexpr double maximumToneDb = 12.0;
inline constexpr double toneFrequencyHz = 3000.0;
inline constexpr double toneQ = 0.7071;

/** Crossfades from normalized arctangent to tanh saturation. */
inline double shape (double input, double driveDb, double character) noexcept
{
    const auto drive = std::clamp (driveDb, minimumDriveDb, maximumDriveDb);
    const auto curve = std::clamp (character, 0.0, 1.0);
    const auto amount = 0.04 * std::pow (10.0, drive / 20.0);
    const auto soft = std::atan (amount * input) / std::atan (amount);
    const auto hard = std::tanh (amount * input) / std::tanh (amount);
    return soft + curve * (hard - soft);
}

inline float blend (float dry, float wet, float mix) noexcept
{
    return dry + std::clamp (mix, 0.0f, 1.0f) * (wet - dry);
}
} // namespace semantifx::distortion
