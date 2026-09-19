#pragma once

#include "Effect.h"

#include "CompressorMath.h"

#include <array>

namespace semantifx::dsp
{
class CompressorEffect final : public Effect
{
public:
    static constexpr int parameters = 5;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setTargets (const float* values) override;
    void snapSmoothers() override;
    void process (float* const* channels, int numChannels, int numSamples) override;
    int parameterCount() const override { return parameters; }

private:
    double sampleRate { 44100.0 };
    std::array<float, parameters> targets {};
    std::array<float, parameters> current {};
    double gainReductionDb {};
};
} // namespace semantifx::dsp
