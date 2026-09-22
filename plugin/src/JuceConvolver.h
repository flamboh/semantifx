#pragma once

#include "dsp/DenormalGuard.h"
#include "dsp/ReverbEffect.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <atomic>
#include <utility>

namespace semantifx::dsp
{
/**
 * Real-time safe adapter from ReverbEffect's generated IR to JUCE convolution.
 *
 * Threading contract (JUCE 8.0.8,
 * modules/juce_dsp/frequency/juce_Convolution.h, Convolution class docstring):
 * "It is not safe to interleave calls to the methods of this class. If you
 * need to load new impulse responses during processing the load() calls must
 * be synchronised with process() calls, which in practice means making the
 * load() call from the audio thread. The loadImpulseResponse() functions
 * *are* wait-free and are therefore suitable for use in a realtime context."
 * The AudioBuffer&& overload's docstring adds: "To avoid memory allocation on
 * the audio thread, this function takes ownership of the buffer passed in. If
 * calling this function during processing, make sure that the buffer is not
 * allocated on the audio thread ... create these buffers on a separate thread
 * and ... use some wait-free construct ... to transfer ownership to the audio
 * thread without allocating."
 *
 * So: the message-thread timer builds each IR into a heap box and publishes
 * it through a single-slot mailbox (pending). The audio thread, at the top of
 * process(), adopts the box and moves its buffer into
 * loadImpulseResponse(AudioBuffer<float>&&, ...), which only enqueues a
 * wait-free command (ConvolutionEngineQueue::loadImpulseResponse in
 * juce_Convolution.cpp) for JUCE's background loader; the heavy FFT work
 * never runs on either of our threads. Emptied shells are parked in a second
 * single-slot mailbox for the timer thread to delete; the audio thread never
 * allocates or frees. A reset request (empty IR) travels the same way via a
 * flag so reset() is never invoked concurrently with process() from another
 * thread.
 *
 * The Convolution is constructed with a ConvolutionMessageQueue shared by all
 * reverb slots (explicit-size constructor, per the header's advice for shared
 * queues). The queue must outlive every Convolution; the processor owns it.
 */
class JuceConvolver final : public Convolver
{
public:
    explicit JuceConvolver (juce::dsp::ConvolutionMessageQueue& sharedQueue)
        : convolution (sharedQueue)
    {
    }

    ~JuceConvolver() override
    {
        drainMailbox();
    }

    void prepare (double sampleRate, int maxBlock, int numChannels) override
    {
        // Host contract: audio is stopped across prepareToPlay, matching the
        // existing dryBuffer.setSize() call there, so draining (which deletes
        // timer-built boxes, and the audio-side spare) is safe here.
        drainMailbox();
        resetRequested.store (false, std::memory_order_release);
        configuredSampleRate = sampleRate;
        configuredChannels = std::clamp (numChannels, 1, reverb::kNumChannels);
        const juce::dsp::ProcessSpec spec {
            sampleRate,
            static_cast<juce::uint32> (std::max (1, maxBlock)),
            static_cast<juce::uint32> (configuredChannels)
        };
        convolution.prepare (spec);
    }

    /** Timer thread only. Builds the IR into a heap box and publishes it. */
    void setImpulseResponse (const float* const* irChannels, int irLength) override
    {
        reclaimShells();
        if (irChannels == nullptr || irLength <= 0)
        {
            if (auto* stale = pending.exchange (nullptr, std::memory_order_acq_rel))
                delete stale;
            resetRequested.store (true, std::memory_order_release);
            return;
        }

        auto* fresh = new Pending {};
        fresh->buffer.setSize (reverb::kNumChannels, irLength);
        for (int channel = 0; channel < reverb::kNumChannels; ++channel)
        {
            auto* destination = fresh->buffer.getWritePointer (channel);
            if (irChannels[channel] != nullptr)
                std::copy_n (irChannels[channel], irLength, destination);
            else
                std::fill_n (destination, irLength, 0.0f);
        }
        fresh->sampleRate = configuredSampleRate;

        // A superseded box was never seen by the audio thread, so deleting it
        // here (timer thread) is safe.
        if (auto* superseded = pending.exchange (fresh, std::memory_order_acq_rel))
            delete superseded;
    }

    void reset() override
    {
        // Called on the audio thread (entering-slot reset in processBlock) or
        // with audio stopped (prepareToPlay), never concurrently with
        // process(). The timer-thread empty-IR path uses resetRequested
        // instead so it never calls this concurrently.
        convolution.reset();
    }

    /** Audio thread only. Adopts any pending IR before processing. */
    void process (float* const* channels, int numChannels, int numSamples) override
    {
        if (resetRequested.exchange (false, std::memory_order_acq_rel))
            convolution.reset();
        returnSpare();
        if (auto* box = pending.exchange (nullptr, std::memory_order_acq_rel))
        {
            convolution.loadImpulseResponse (std::move (box->buffer), box->sampleRate,
                                             juce::dsp::Convolution::Stereo::yes,
                                             juce::dsp::Convolution::Trim::no,
                                             juce::dsp::Convolution::Normalise::no);
            parkShell (box);
        }

        semantifx::dsp::DenormalGuard denormalGuard;
        const auto channelCount = std::clamp (std::min (numChannels, configuredChannels),
                                              0, reverb::kNumChannels);
        if (channels == nullptr || channelCount == 0 || numSamples <= 0)
            return;
        juce::dsp::AudioBlock<float> block (channels,
                                            static_cast<size_t> (channelCount),
                                            static_cast<size_t> (numSamples));
        juce::dsp::ProcessContextReplacing<float> context (block);
        convolution.process (context);
    }

private:
    struct Pending
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate { reverb::kSampleRate };
    };

    /**
     * Audio thread: hand an emptied shell to the timer thread for deletion.
     * The park can fail if the timer published again after we parked a shell
     * but before reclaiming it; the shell is then held locally and re-parked
     * on the next block. At most one shell is ever stranded this way: every
     * publish reclaims the shell slot first, so a second failure would
     * require consuming an IR published before the stranding, which the
     * single-slot mailbox has already superseded.
     */
    void parkShell (Pending* shell) noexcept
    {
        auto* expected = static_cast<Pending*> (nullptr);
        if (shells.compare_exchange_strong (expected, shell,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire))
            return;
        jassert (spare == nullptr);
        spare = shell;
    }

    /** Audio thread: retry a stranded shell. Never allocates or frees. */
    void returnSpare() noexcept
    {
        if (spare == nullptr)
            return;
        auto* expected = static_cast<Pending*> (nullptr);
        if (shells.compare_exchange_strong (expected, spare,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire))
            spare = nullptr;
    }

    /** Timer/message thread only: delete shells the audio thread parked. */
    void reclaimShells()
    {
        if (auto* shell = shells.exchange (nullptr, std::memory_order_acq_rel))
            delete shell;
    }

    /**
     * Timer/message thread only, with audio stopped (prepare/teardown):
     * drop everything, including the audio-side spare.
     */
    void drainMailbox()
    {
        reclaimShells();
        if (auto* box = pending.exchange (nullptr, std::memory_order_acq_rel))
            delete box;
        if (spare != nullptr)
        {
            delete spare;
            spare = nullptr;
        }
    }

    juce::dsp::Convolution convolution;
    double configuredSampleRate { reverb::kSampleRate };
    int configuredChannels { reverb::kNumChannels };
    std::atomic<Pending*> pending { nullptr };
    std::atomic<Pending*> shells { nullptr };
    std::atomic<bool> resetRequested { false };
    /** Audio-thread-local stranded shell; see parkShell. */
    Pending* spare { nullptr };
};
} // namespace semantifx::dsp
