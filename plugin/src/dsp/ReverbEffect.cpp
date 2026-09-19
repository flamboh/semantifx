#include "ReverbEffect.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace semantifx::dsp
{
void DirectConvolver::prepare (double, int, int numChannels)
{
    configuredChannels = std::clamp (numChannels, 1, reverb::kNumChannels);
    if (impulseLength > 0)
    {
        for (auto& channel : history)
            channel.assign (static_cast<size_t> (impulseLength), 0.0f);
    }
    writePosition = 0;
}

void DirectConvolver::setImpulseResponse (const float* const* irChannels, int irLength)
{
    impulseLength = std::max (irLength, 0);
    for (int channel = 0; channel < reverb::kNumChannels; ++channel)
    {
        auto& destination = impulse[static_cast<size_t> (channel)];
        if (impulseLength == 0 || irChannels == nullptr || irChannels[channel] == nullptr)
            destination.clear();
        else
            destination.assign (irChannels[channel], irChannels[channel] + impulseLength);
    }
    for (auto& channel : history)
        channel.assign (static_cast<size_t> (impulseLength), 0.0f);
    writePosition = 0;
}

void DirectConvolver::reset()
{
    for (auto& channel : history)
        std::fill (channel.begin(), channel.end(), 0.0f);
    writePosition = 0;
}

void DirectConvolver::process (float* const* channels, int numChannels, int numSamples)
{
    const auto channelCount = std::clamp (std::min (numChannels, configuredChannels),
                                          0, reverb::kNumChannels);
    if (channelCount == 0 || numSamples <= 0 || impulseLength == 0)
        return;

    // This is intentionally a direct, time-domain convolver. It has O(N*L) cost and is
    // for parity tests and offline use, not real-time audio. The plugin will use a
    // host convolution adapter for the audio callback.
    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto& channelHistory = history[static_cast<size_t> (channel)];
            channelHistory[writePosition] = channels[channel] == nullptr
                ? 0.0f : channels[channel][sample];
        }

        for (int channel = 0; channel < channelCount; ++channel)
        {
            if (channels[channel] == nullptr)
                continue;
            const auto irChannel = channelCount == 1 ? 0 : channel;
            const auto& coefficients = impulse[static_cast<size_t> (irChannel)];
            const auto& channelHistory = history[static_cast<size_t> (channel)];
            float output = 0.0f;
            auto historyIndex = static_cast<int> (writePosition);
            for (int tap = 0; tap < impulseLength; ++tap)
            {
                output += coefficients[static_cast<size_t> (tap)]
                        * channelHistory[static_cast<size_t> (historyIndex)];
                if (--historyIndex < 0)
                    historyIndex = impulseLength - 1;
            }
            channels[channel][sample] = output;
        }

        if (++writePosition == static_cast<size_t> (impulseLength))
            writePosition = 0;
    }
}

ReverbEffect::ReverbEffect (const char* noiseTablePath)
    : ReverbEffect (std::make_unique<DirectConvolver>(), noiseTablePath)
{
}

ReverbEffect::ReverbEffect (std::unique_ptr<Convolver> nextConvolver, const char* noiseTablePath)
    : convolver (std::move (nextConvolver))
{
    if (convolver == nullptr)
        convolver = std::make_unique<DirectConvolver>();
    targets = { 0.5f, 0.3f, 0.3f };
    current = targets;
    for (size_t index = 0; index < targets.size(); ++index)
        targetValues[index].store (targets[index], std::memory_order_relaxed);
    if (noiseTablePath != nullptr)
        loadNoiseTable (noiseTablePath);
}

bool ReverbEffect::loadNoiseTable (const char* path)
{
    if (path == nullptr)
        return false;

    constexpr auto valuesPerChannel = static_cast<size_t> (reverb::kNumBands)
                                    * static_cast<size_t> (reverb::kNumSamples);
    std::ifstream input (path, std::ios::binary);
    if (! input)
        return false;

    std::array<std::vector<float>, reverb::kNumChannels> loaded;
    for (auto& channel : loaded)
    {
        channel.resize (valuesPerChannel);
        input.read (reinterpret_cast<char*> (channel.data()),
                    static_cast<std::streamsize> (channel.size() * sizeof (float)));
        if (! input)
            return false;
    }

    noise = std::move (loaded);
    hasNoiseTable = true;
    targetGeneration.fetch_add (1, std::memory_order_acq_rel);
    dirty.store (true, std::memory_order_release);
    return true;
}

void ReverbEffect::prepare (double newSampleRate, int newMaxBlockSize)
{
    sampleRate.store (std::max (newSampleRate, 1.0), std::memory_order_release);
    maxBlockSize = std::max (newMaxBlockSize, 1);
    for (size_t index = 0; index < current.size(); ++index)
        current[index] = targetValues[index].load (std::memory_order_acquire);
    convolver->prepare (sampleRate.load (std::memory_order_acquire), maxBlockSize,
                        reverb::kNumChannels);
    for (auto& channel : dry)
        channel.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    impulseReady.store (false, std::memory_order_release);
    dirty.store (hasNoiseTable, std::memory_order_release);
    reset();
}

void ReverbEffect::reset()
{
    convolver->reset();
}

void ReverbEffect::setTargets (const float* values)
{
    for (size_t index = 0; index < targets.size(); ++index)
    {
        targets[index] = values[index];
        targetValues[index].store (values[index], std::memory_order_release);
    }
    current[2] = values[2];
    targetGeneration.fetch_add (1, std::memory_order_acq_rel);
    dirty.store (hasNoiseTable, std::memory_order_release);
}

void ReverbEffect::snapSmoothers()
{
    for (size_t index = 0; index < current.size(); ++index)
        current[index] = targetValues[index].load (std::memory_order_acquire);
}

void ReverbEffect::rebuildImpulseResponse()
{
    if (! hasNoiseTable)
        return;

    const auto generation = targetGeneration.load (std::memory_order_acquire);
    const auto decay = targetValues[0].load (std::memory_order_acquire);
    const auto damping = targetValues[1].load (std::memory_order_acquire);
    const auto expanded = reverb::expandMacros (decay, damping);
    std::array<const float*, reverb::kNumChannels> noisePointers {
        noise[0].data(), noise[1].data() };
    std::array<std::vector<float>, reverb::kNumChannels> baseImpulse;
    reverb::buildImpulseResponse (noisePointers.data(), expanded, baseImpulse);

    const auto targetLength = std::max (1, static_cast<int> (std::lround (
        static_cast<double> (reverb::kNumSamples) * sampleRate.load (std::memory_order_acquire)
            / reverb::kSampleRate)));
    for (int channel = 0; channel < reverb::kNumChannels; ++channel)
    {
        auto& destination = impulse[static_cast<size_t> (channel)];
        if (targetLength == reverb::kNumSamples)
        {
            destination = std::move (baseImpulse[static_cast<size_t> (channel)]);
            continue;
        }

        destination.resize (static_cast<size_t> (targetLength));
        const auto& source = baseImpulse[static_cast<size_t> (channel)];
        for (int sample = 0; sample < targetLength; ++sample)
        {
            const auto position = targetLength == 1 ? 0.0
                : static_cast<double> (sample) * static_cast<double> (reverb::kNumSamples - 1)
                    / static_cast<double> (targetLength - 1);
            const auto first = static_cast<size_t> (std::floor (position));
            const auto second = std::min (first + 1, source.size() - 1);
            const auto fraction = static_cast<float> (position - static_cast<double> (first));
            destination[static_cast<size_t> (sample)] = source[first]
                + fraction * (source[second] - source[first]);
        }
    }

    std::array<const float*, reverb::kNumChannels> impulsePointers {
        impulse[0].data(), impulse[1].data() };
    convolver->setImpulseResponse (impulsePointers.data(), targetLength);
    impulseReady.store (true, std::memory_order_release);
    if (targetGeneration.load (std::memory_order_acquire) == generation)
        dirty.store (false, std::memory_order_release);
}

void ReverbEffect::process (float* const* channels, int numChannels, int numSamples)
{
    const auto channelCount = std::clamp (numChannels, 0, reverb::kNumChannels);
    if (channelCount == 0 || numSamples <= 0 || maxBlockSize <= 0
        || ! impulseReady.load (std::memory_order_acquire))
        return;

    if (numSamples > maxBlockSize)
    {
        for (int offset = 0; offset < numSamples; offset += maxBlockSize)
        {
            const auto count = std::min (maxBlockSize, numSamples - offset);
            std::array<float*, reverb::kNumChannels> subChannels {
                channels[0] == nullptr ? nullptr : channels[0] + offset,
                channelCount > 1 && channels[1] != nullptr ? channels[1] + offset : nullptr };
            process (subChannels.data(), channelCount, count);
        }
        return;
    }

    for (int channel = 0; channel < channelCount; ++channel)
    {
        auto& channelDry = dry[static_cast<size_t> (channel)];
        if (numSamples > static_cast<int> (channelDry.size()))
            return;
        if (channels[channel] != nullptr)
            std::copy_n (channels[channel], numSamples, channelDry.data());
        else
            std::fill_n (channelDry.data(), numSamples, 0.0f);
    }

    convolver->process (channels, channelCount, numSamples);
    const auto mix = std::clamp (current[2], 0.0f, 1.0f);
    for (int channel = 0; channel < channelCount; ++channel)
    {
        if (channels[channel] == nullptr)
            continue;
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto drySample = dry[static_cast<size_t> (channel)][static_cast<size_t> (sample)];
            channels[channel][sample] = (1.0f - mix) * drySample
                                      + mix * channels[channel][sample];
        }
    }
}
} // namespace semantifx::dsp
