#pragma once

#include "AnalysisResult.h"
#include "CaptureBuffer.h"
#include "SlotPool.h"
#include "dsp/EffectFactory.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

namespace semantifx
{
class DevelopmentAnalysisRunner;

enum class DevelopmentAnalysisState
{
    idle,
    assembling,
    running,
    cancelling,
    applying,
    succeeded,
    failed,
    cancelled
};

namespace parameter
{
inline constexpr auto outputGain = "outputGain";
inline constexpr auto mix = "mix";
inline constexpr auto bypass = "bypass";

inline juce::String slotParameterId (slots::Slot slot, int parameterIndex)
{
    return juce::String { slots::parameterId (slot, parameterIndex) };
}

inline juce::String slotBypassId (slots::Slot slot)
{
    return juce::String { slots::bypassId (slot) };
}
} // namespace parameter

class PluginProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int maxParametersPerType = 18;

    struct Snapshot
    {
        slots::Composition composition;
        std::array<std::array<float, maxParametersPerType>, slots::kSlotCount> values {};
        std::array<float, slots::kSlotCount> bypass {};
    };

    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    slots::Composition getComposition() const;
    void setComposition (const slots::Composition& composition);
    bool addSlot (int effectTypeIndex);
    bool removeSlot (slots::Slot slot);
    bool moveSlot (int from, int to);

    float getSlotParameterValue (slots::Slot slot, int parameterIndex) const noexcept;
    juce::RangedAudioParameter* getSlotParameter (slots::Slot slot, int parameterIndex) const;
    void setSlotParameterValue (slots::Slot slot, int parameterIndex, double value,
                                bool notifyHost = true);
    bool isSlotBypassed (slots::Slot slot) const noexcept;
    void setSlotBypassed (slots::Slot slot, bool bypassed);
    int getSoloSlot() const noexcept { return soloSlot.load (std::memory_order_acquire); }
    void setSoloSlot (int slotIndex) noexcept;
    void scheduleActiveAnalysisRestart() noexcept;

    /** Applies a successful analysis result for the current chain on the message thread. */
    juce::Result applyAnalysisResultJson (const juce::String& json);
    juce::Result startDevelopmentCapture();
    void stopDevelopmentWork (bool revertToPreRun);
    CaptureBuffer::State getCaptureState() const noexcept { return captureBuffer.getState(); }
    float getCaptureProgress() const noexcept { return captureBuffer.getProgress(); }
    juce::Result startDevelopmentShape (const juce::String& prompt, int iterations = 600);
    juce::Result startDevelopmentOptimization (const juce::String& prompt, int iterations = 600);
    juce::Result startDevelopmentAnalysis (const juce::String& prompt, int iterations = 600)
    {
        return startDevelopmentOptimization (prompt, iterations);
    }
    DevelopmentAnalysisState getDevelopmentAnalysisState() const noexcept;
    juce::String getDevelopmentStatus() const;
    std::optional<protocol::AnalysisProgress> getDevelopmentProgress() const;
    juce::String getLastAppliedDirection() const;

    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void timerCallback() override;
    void syncCompositionFromState();
    void setCompositionInternal (const slots::Composition&, bool restartRunner);
    protocol::ExpectedChain expectedChain() const;
    Snapshot makeSnapshot() const;
    void restoreSnapshot (const Snapshot& snapshot);
    juce::Result applyAnalysisResult (const protocol::AnalysisResult&);
    juce::Result applyAnalysisProgress (const protocol::AnalysisProgress&);
    juce::Result applyAssemblyResult (const protocol::AssemblyResult&);
    void initializeEffectTargets (int slotIndex);
    void setParameterValue (juce::RangedAudioParameter*, double value, bool notifyHost);
    void resetSmoothers();
    void updateGlobalTargets() noexcept;

    static constexpr int parameterTableSize = 18;
    std::array<std::unique_ptr<dsp::Effect>, slots::kSlotCount> effects;
    std::array<std::array<std::atomic<float>*, parameterTableSize>, slots::kSlotCount> rawSlotValues {};
    std::array<std::atomic<float>*, slots::kSlotCount> rawBypasses {};
    std::atomic<float>* rawOutputGain {};
    std::atomic<float>* rawMix {};
    std::atomic<float>* rawGlobalBypass {};
    std::atomic<std::uint32_t> packedComposition {};
    std::atomic<std::int8_t> soloSlot { -1 };
    std::atomic<juce::int64> parameterRestartDeadline { 0 };
    std::uint32_t previousPackedComposition {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGainSmooth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMixSmooth;
    juce::AudioBuffer<float> dryBuffer;
    double currentSampleRate { 48000.0 };
    int currentMaxBlockSize { 512 };
    CaptureBuffer captureBuffer;
    std::unique_ptr<DevelopmentAnalysisRunner> developmentRunner;

    friend class DevelopmentAnalysisRunner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
} // namespace semantifx
