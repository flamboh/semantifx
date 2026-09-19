#pragma once

#include "Effect.h"

#include "../EqMath.h"

#include <array>

namespace semantifx::dsp
{
class EqEffect final : public Effect
{
public:
    static constexpr int parameters = 18;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setTargets (const float* values) override;
    void snapSmoothers() override;
    void process (float* const* channels, int numChannels, int numSamples) override;
    int parameterCount() const override { return parameters; }

private:
    class Biquad
    {
    public:
        void setCoefficients (const eq::Coefficients& next) noexcept { coefficients = next; }
        void reset() noexcept { z1 = z2 = 0.0; }

        float process (float input) noexcept
        {
            const auto output = coefficients.b0 * static_cast<double> (input) + z1;
            z1 = coefficients.b1 * static_cast<double> (input)
               - coefficients.a1 * output + z2;
            z2 = coefficients.b2 * static_cast<double> (input)
               - coefficients.a2 * output;
            return static_cast<float> (output);
        }

    private:
        eq::Coefficients coefficients;
        double z1 {}, z2 {};
    };

    void updateCoefficients (const std::array<float, parameters>& values) noexcept;

    double sampleRate { 44100.0 };
    std::array<float, parameters> targets {};
    std::array<float, parameters> current {};
    std::array<std::array<Biquad, 2>, 6> filters;
};
} // namespace semantifx::dsp
