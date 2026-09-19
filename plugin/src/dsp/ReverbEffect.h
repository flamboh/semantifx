#pragma once

#include "Effect.h"

#include "ReverbMath.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace semantifx::dsp
{
class Convolver;

struct ReverbResources
{
    const char* noiseTablePath {};
    std::function<std::unique_ptr<Convolver>()> convolverFactory;
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
    ReverbEffect (std::unique_ptr<Convolver> convolver, const char* noiseTablePath = nullptr);

    bool loadNoiseTable (const char* path);
    bool impulseResponseDirty() const noexcept { return dirty.load (std::memory_order_acquire); }
    void rebuildImpulseResponse();

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setTargets (const float* values) override;
    void snapSmoothers() override;
    void process (float* const* channels, int numChannels, int numSamples) override;
    int parameterCount() const override { return parameters; }

private:
    std::atomic<double> sampleRate { reverb::kSampleRate };
    int maxBlockSize {};
    std::array<float, parameters> targets {};
    std::array<float, parameters> current {};
    std::array<std::atomic<float>, parameters> targetValues {};
    std::array<std::vector<float>, reverb::kNumChannels> noise;
    std::array<std::vector<float>, reverb::kNumChannels> impulse;
    std::array<std::vector<float>, reverb::kNumChannels> dry;
    std::unique_ptr<Convolver> convolver;
    bool hasNoiseTable {};
    std::atomic<bool> dirty { false };
    std::atomic<bool> impulseReady { false };
    std::atomic<float> lastBuiltDecay { 0.0f };
    std::atomic<float> lastBuiltDamping { 0.0f };
    std::atomic<bool> hasBuiltMacros { false };
};
} // namespace semantifx::dsp
