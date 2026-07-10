#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace semantifx::eq
{
struct Coefficients
{
    double b0 { 1.0 }, b1 {}, b2 {}, a1 {}, a2 {};
};

inline double clampFrequency (double frequency, double sampleRate) noexcept
{
    return std::clamp (frequency, 10.0, sampleRate * 0.49);
}

inline Coefficients peak (double sampleRate, double frequency, double q, double gainDb) noexcept
{
    frequency = clampFrequency (frequency, sampleRate);
    q = std::max (q, 0.01);
    const auto A = std::pow (10.0, gainDb / 40.0);
    const auto omega = 2.0 * 3.14159265358979323846 * frequency / sampleRate;
    const auto alpha = std::sin (omega) / (2.0 * q);
    const auto cosine = std::cos (omega);
    const auto a0 = 1.0 + alpha / A;
    return { (1.0 + alpha * A) / a0, (-2.0 * cosine) / a0,
             (1.0 - alpha * A) / a0, (-2.0 * cosine) / a0,
             (1.0 - alpha / A) / a0 };
}

inline Coefficients lowShelf (double sampleRate, double frequency, double q, double gainDb) noexcept
{
    frequency = clampFrequency (frequency, sampleRate);
    q = std::max (q, 0.01);
    const auto A = std::pow (10.0, gainDb / 40.0);
    const auto omega = 2.0 * 3.14159265358979323846 * frequency / sampleRate;
    const auto cosine = std::cos (omega);
    const auto sine = std::sin (omega);
    const auto alpha = sine / (2.0 * q);
    const auto rootA = std::sqrt (A);
    const auto a0 = (A + 1.0) + (A - 1.0) * cosine + 2.0 * rootA * alpha;
    return { A * ((A + 1.0) - (A - 1.0) * cosine + 2.0 * rootA * alpha) / a0,
             2.0 * A * ((A - 1.0) - (A + 1.0) * cosine) / a0,
             A * ((A + 1.0) - (A - 1.0) * cosine - 2.0 * rootA * alpha) / a0,
             -2.0 * ((A - 1.0) + (A + 1.0) * cosine) / a0,
             ((A + 1.0) + (A - 1.0) * cosine - 2.0 * rootA * alpha) / a0 };
}

inline Coefficients highShelf (double sampleRate, double frequency, double q, double gainDb) noexcept
{
    frequency = clampFrequency (frequency, sampleRate);
    q = std::max (q, 0.01);
    const auto A = std::pow (10.0, gainDb / 40.0);
    const auto omega = 2.0 * 3.14159265358979323846 * frequency / sampleRate;
    const auto cosine = std::cos (omega);
    const auto sine = std::sin (omega);
    const auto alpha = sine / (2.0 * q);
    const auto rootA = std::sqrt (A);
    const auto a0 = (A + 1.0) - (A - 1.0) * cosine + 2.0 * rootA * alpha;
    return { A * ((A + 1.0) + (A - 1.0) * cosine + 2.0 * rootA * alpha) / a0,
             -2.0 * A * ((A - 1.0) + (A + 1.0) * cosine) / a0,
             A * ((A + 1.0) + (A - 1.0) * cosine - 2.0 * rootA * alpha) / a0,
             2.0 * ((A - 1.0) - (A + 1.0) * cosine) / a0,
             ((A + 1.0) - (A - 1.0) * cosine - 2.0 * rootA * alpha) / a0 };
}

inline double magnitudeDb (const Coefficients& c, double frequency, double sampleRate) noexcept
{
    const auto omega = 2.0 * 3.14159265358979323846 * frequency / sampleRate;
    const auto z1r = std::cos (omega), z1i = -std::sin (omega);
    const auto z2r = std::cos (2.0 * omega), z2i = -std::sin (2.0 * omega);
    const auto nr = c.b0 + c.b1 * z1r + c.b2 * z2r;
    const auto ni = c.b1 * z1i + c.b2 * z2i;
    const auto dr = 1.0 + c.a1 * z1r + c.a2 * z2r;
    const auto di = c.a1 * z1i + c.a2 * z2i;
    return 20.0 * std::log10 (std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di)));
}
} // namespace semantifx::eq
