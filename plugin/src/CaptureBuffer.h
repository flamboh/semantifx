#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace semantifx
{
class CaptureBuffer
{
public:
    enum class State { unavailable, idle, capturing, ready };

    void prepare (double newSampleRate, int newChannels, double seconds)
    {
        sampleRate = newSampleRate;
        channels = std::clamp (newChannels, 1, 2);
        targetSamples = std::max (1, static_cast<int> (newSampleRate * seconds));
        storage.assign (static_cast<size_t> (channels * targetSamples), 0.0f);
        writePosition.store (0, std::memory_order_relaxed);
        generation.fetch_add (1, std::memory_order_release);
        state.store (State::idle, std::memory_order_release);
    }

    bool start() noexcept
    {
        if (storage.empty() || state.load (std::memory_order_acquire) == State::capturing)
            return false;
        generation.fetch_add (1, std::memory_order_acq_rel);
        writePosition.store (0, std::memory_order_relaxed);
        state.store (State::capturing, std::memory_order_release);
        return true;
    }

    void cancel() noexcept
    {
        generation.fetch_add (1, std::memory_order_acq_rel);
        writePosition.store (0, std::memory_order_relaxed);
        state.store (storage.empty() ? State::unavailable : State::idle, std::memory_order_release);
    }

    /** Called only by the audio thread. Performs bounded copies and never allocates or locks. */
    void push (const float* const* input, int inputChannels, int numSamples) noexcept
    {
        if (state.load (std::memory_order_acquire) != State::capturing || input == nullptr
            || inputChannels <= 0 || numSamples <= 0)
            return;

        const auto activeGeneration = generation.load (std::memory_order_acquire);
        const auto position = writePosition.load (std::memory_order_relaxed);
        const auto count = std::min (numSamples, targetSamples - position);
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto sourceChannel = std::min (channel, inputChannels - 1);
            auto* destination = storage.data() + static_cast<size_t> (channel * targetSamples + position);
            std::copy_n (input[sourceChannel], count, destination);
        }

        if (generation.load (std::memory_order_acquire) != activeGeneration
            || state.load (std::memory_order_acquire) != State::capturing)
            return;

        const auto nextPosition = position + count;
        writePosition.store (nextPosition, std::memory_order_release);
        if (nextPosition >= targetSamples)
            state.store (State::ready, std::memory_order_release);
    }

    State getState() const noexcept { return state.load (std::memory_order_acquire); }
    float getProgress() const noexcept
    {
        return targetSamples > 0 ? static_cast<float> (writePosition.load (std::memory_order_acquire))
                                      / static_cast<float> (targetSamples)
                                 : 0.0f;
    }

    int getNumChannels() const noexcept { return channels; }
    int getNumSamples() const noexcept { return targetSamples; }
    double getSampleRate() const noexcept { return sampleRate; }
    const float* getReadPointer (int channel) const noexcept
    {
        return storage.data() + static_cast<size_t> (channel * targetSamples);
    }

private:
    std::vector<float> storage;
    std::atomic<State> state { State::unavailable };
    std::atomic<int> writePosition { 0 };
    std::atomic<std::uint64_t> generation { 0 };
    double sampleRate {};
    int channels {};
    int targetSamples {};
};
} // namespace semantifx

