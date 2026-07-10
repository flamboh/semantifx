#pragma once

#include "AnalysisResult.h"
#include "CaptureBuffer.h"
#include "EqMath.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>

namespace semantifx
{
class DevelopmentAnalysisRunner;

enum class DevelopmentAnalysisState { idle, running, cancelling, applying, succeeded, failed, cancelled };

namespace parameter
{
inline constexpr auto lowGain = protocol::canonicalParameterNames[0];
inline constexpr auto lowFrequency = protocol::canonicalParameterNames[1];
inline constexpr auto lowQ = protocol::canonicalParameterNames[2];
inline constexpr auto band0Gain = protocol::canonicalParameterNames[3];
inline constexpr auto band0Frequency = protocol::canonicalParameterNames[4];
inline constexpr auto band0Q = protocol::canonicalParameterNames[5];
inline constexpr auto band1Gain = protocol::canonicalParameterNames[6];
inline constexpr auto band1Frequency = protocol::canonicalParameterNames[7];
inline constexpr auto band1Q = protocol::canonicalParameterNames[8];
inline constexpr auto band2Gain = protocol::canonicalParameterNames[9];
inline constexpr auto band2Frequency = protocol::canonicalParameterNames[10];
inline constexpr auto band2Q = protocol::canonicalParameterNames[11];
inline constexpr auto band3Gain = protocol::canonicalParameterNames[12];
inline constexpr auto band3Frequency = protocol::canonicalParameterNames[13];
inline constexpr auto band3Q = protocol::canonicalParameterNames[14];
inline constexpr auto highGain = protocol::canonicalParameterNames[15];
inline constexpr auto highFrequency = protocol::canonicalParameterNames[16];
inline constexpr auto highQ = protocol::canonicalParameterNames[17];
inline constexpr auto outputGain = "outputGain";
inline constexpr auto mix = "mix";
inline constexpr auto bypass = "bypass";

struct BandIDs
{
    const char* gain;
    const char* frequency;
    const char* q;
};

inline constexpr std::array<BandIDs, 6> bands {{
    { lowGain, lowFrequency, lowQ },
    { band0Gain, band0Frequency, band0Q },
    { band1Gain, band1Frequency, band1Q },
    { band2Gain, band2Frequency, band2Q },
    { band3Gain, band3Frequency, band3Q },
    { highGain, highFrequency, highQ },
}};
} // namespace parameter

class Biquad
{
public:
    void setCoefficients (const eq::Coefficients& next) noexcept { coefficients = next; }
    void reset() noexcept { z1 = z2 = 0.0; }

    float process (float input) noexcept
    {
        const auto output = coefficients.b0 * input + z1;
        z1 = coefficients.b1 * input - coefficients.a1 * output + z2;
        z2 = coefficients.b2 * input - coefficients.a2 * output;
        return static_cast<float> (output);
    }

private:
    eq::Coefficients coefficients;
    double z1 {}, z2 {};
};

class PluginProcessor final : public juce::AudioProcessor
{
public:
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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Applies a complete protocol-v1 EQ result. Must be called on the message thread. */
    juce::Result applyAnalysisResultJson (const juce::String& json);
    juce::Result startDevelopmentCapture();
    void cancelDevelopmentWork();
    CaptureBuffer::State getCaptureState() const noexcept { return captureBuffer.getState(); }
    float getCaptureProgress() const noexcept { return captureBuffer.getProgress(); }
    juce::Result startDevelopmentAnalysis (const juce::String& prompt, int iterations = 50);
    DevelopmentAnalysisState getDevelopmentAnalysisState() const noexcept;
    juce::String getDevelopmentStatus() const;

    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void resetSmoothers();
    void updateTargets() noexcept;

    struct BandSmoothers
    {
        juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> gain;
        juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> frequency;
        juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> q;
    };

    std::array<std::array<Biquad, 2>, 6> filters;
    std::array<BandSmoothers, 6> bandSmoothers;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGainSmooth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMixSmooth;
    double currentSampleRate { 48000.0 };
    CaptureBuffer captureBuffer;
    std::unique_ptr<DevelopmentAnalysisRunner> developmentRunner;

    friend class DevelopmentAnalysisRunner;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
} // namespace semantifx
