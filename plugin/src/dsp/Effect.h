#pragma once

namespace semantifx::dsp
{
class Effect
{
public:
    virtual ~Effect() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual void setTargets (const float* values) = 0;
    virtual void snapSmoothers() = 0;
    virtual void process (float* const* channels, int numChannels, int numSamples) = 0;
    virtual int parameterCount() const = 0;
};
} // namespace semantifx::dsp
