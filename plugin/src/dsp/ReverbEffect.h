#pragma once

#include "Effect.h"

#include "ReverbMath.h"

#include <array>
#include <vector>

namespace semantifx::dsp
{
struct ReverbResources
{
    const char* noiseTablePath {};
};

class Convolver
{
public:
    virtual ~Convolver() = default;
    virtual void prepare (double sampleRate, int maxBlock, int numChannels) = 0;
    virtual void setImpulseResponse (const float* const* irChannels, int irLength) = 0;
    virtual void reset() = 0;
    virtual void process (float* const* channels, int numChannels, int numSamples) = 0;
};

class DirectConvolver final : public Convolver
{
public:
    void prepare (double sampleRate, int maxBlock, int numChannels) override;
    void setImpulseResponse (const float* const* irChannels, int irLength) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;

private:
    int configuredChannels { 2 };
    int impulseLength {};
    size_t writePosition {};
    std::array<std::vector<float>, reverb::kNumChannels> impulse;
    std::array<std::vector<float>, reverb::kNumChannels> history;
};

class ReverbEffect final : public Effect
{
public:
    static constexpr int parameters = 3;

    explicit ReverbEffect (const char* noiseTablePath = nullptr);

    bool loadNoiseTable (const char* path);

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setTargets (const float* values) override;
    void snapSmoothers() override;
    void process (float* const* channels, int numChannels, int numSamples) override;
    int parameterCount() const override { return parameters; }

private:
    void rebuildImpulseResponse();

    double sampleRate { reverb::kSampleRate };
    int maxBlockSize {};
    std::array<float, parameters> targets {};
    std::array<float, parameters> current {};
    std::array<std::vector<float>, reverb::kNumChannels> noise;
    std::array<std::vector<float>, reverb::kNumChannels> impulse;
    std::array<std::vector<float>, reverb::kNumChannels> dry;
    DirectConvolver convolver;
    bool hasNoiseTable {};
};
} // namespace semantifx::dsp
