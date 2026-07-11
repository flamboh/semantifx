#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>

namespace semantifx
{
class DevelopmentAnalysisRunner final : private juce::Thread, private juce::AsyncUpdater
{
public:
    explicit DevelopmentAnalysisRunner (PluginProcessor& processor)
        : Thread ("Semantifx development analysis"), owner (processor)
    {
    }

    ~DevelopmentAnalysisRunner() override
    {
        cancel();
        stopThread (3000);
        cancelPendingUpdate();
    }

    juce::Result start (const juce::String& newPrompt, int newIterations)
    {
        if (isThreadRunning())
            return juce::Result::fail ("The previous analyzer process is still shutting down.");
        if (state.load() == DevelopmentAnalysisState::running
            || state.load() == DevelopmentAnalysisState::applying)
            return juce::Result::fail ("Analysis is already running.");
        if (owner.captureBuffer.getState() != CaptureBuffer::State::ready)
            return juce::Result::fail ("Capture ten seconds of audio before analyzing.");
        if (newPrompt.trim().isEmpty())
            return juce::Result::fail ("Enter a description before analyzing.");

        {
            const juce::ScopedLock lock (dataLock);
            prompt = newPrompt.trim();
            iterations = std::clamp (newIterations, 1, 10000);
            status = "Writing captured audio...";
            pendingResult.clear();
        }
        state.store (DevelopmentAnalysisState::running);
        if (! startThread())
        {
            fail ("Could not start the analysis thread.");
            return juce::Result::fail (getStatus());
        }
        return juce::Result::ok();
    }

    void cancel()
    {
        const auto current = state.load();
        if (current != DevelopmentAnalysisState::running && current != DevelopmentAnalysisState::applying)
            return;
        signalThreadShouldExit();
        cancelPendingUpdate();
        {
            const juce::ScopedLock lock (dataLock);
            pendingResult.clear();
            status = current == DevelopmentAnalysisState::running ? "Cancelling analysis..." : "Analysis cancelled";
        }
        state.store (current == DevelopmentAnalysisState::running ? DevelopmentAnalysisState::cancelling
                                                                   : DevelopmentAnalysisState::cancelled);
    }

    void resetStatus()
    {
        const auto current = state.load();
        if (current == DevelopmentAnalysisState::running || current == DevelopmentAnalysisState::applying)
            return;
        const juce::ScopedLock lock (dataLock);
        status.clear();
        state.store (DevelopmentAnalysisState::idle);
    }

    DevelopmentAnalysisState getState() const noexcept { return state.load(); }
    bool isBusy() const noexcept
    {
        return isThreadRunning() || state.load() == DevelopmentAnalysisState::applying;
    }
    juce::String getStatus() const
    {
        const juce::ScopedLock lock (dataLock);
        return status;
    }

private:
    void fail (const juce::String& message)
    {
        if (state.load() == DevelopmentAnalysisState::cancelling)
            return;
        const juce::ScopedLock lock (dataLock);
        if (state.load() == DevelopmentAnalysisState::cancelling)
            return;
        status = message;
        state.store (DevelopmentAnalysisState::failed);
    }

    bool writeCapture (const juce::File& destination)
    {
        auto stream = destination.createOutputStream();
        if (stream == nullptr)
            return false;

        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            format.createWriterFor (stream.get(), owner.captureBuffer.getSampleRate(),
                                    static_cast<unsigned int> (owner.captureBuffer.getNumChannels()),
                                    24, {}, 0));
        if (writer == nullptr)
            return false;
        stream.release(); // The writer owns and closes the stream.

        std::array<const float*, 2> channels {
            owner.captureBuffer.getReadPointer (0),
            owner.captureBuffer.getNumChannels() > 1 ? owner.captureBuffer.getReadPointer (1) : nullptr
        };
        return writer->writeFromFloatArrays (channels.data(), owner.captureBuffer.getNumChannels(),
                                             owner.captureBuffer.getNumSamples());
    }

    void run() override
    {
        const auto markCancellationComplete = juce::ScopeGuard { [this]
        {
            if (state.load() == DevelopmentAnalysisState::cancelling)
            {
                const juce::ScopedLock lock (dataLock);
                status = "Analysis cancelled";
                state.store (DevelopmentAnalysisState::cancelled);
            }
        } };
        const auto captureFile = juce::File::createTempFile ("-semantifx-capture.wav");
        const auto resultFile = juce::File::createTempFile ("-semantifx-result.json");
        const auto removeTemporaryFiles = juce::ScopeGuard { [&]
        {
            captureFile.deleteFile();
            resultFile.deleteFile();
        } };

        if (! writeCapture (captureFile))
        {
            fail ("Could not write the temporary WAV capture.");
            return;
        }
        if (threadShouldExit())
            return;

        const auto script = juce::File { juce::String { SEMANTIFX_DEVELOPMENT_REPO_PATH } }
                                .getChildFile ("scripts").getChildFile ("analyze");
        if (! script.existsAsFile())
        {
            fail ("Development analyzer not found at " + script.getFullPathName());
            return;
        }

        juce::String localPrompt;
        int localIterations {};
        {
            const juce::ScopedLock lock (dataLock);
            localPrompt = prompt;
            localIterations = iterations;
            status = "Running Text2FX (development mode)...";
        }

        juce::StringArray arguments;
        arguments.add (script.getFullPathName());
        arguments.add (captureFile.getFullPathName());
        arguments.add (localPrompt);
        arguments.add (juce::String { localIterations });
        arguments.add (resultFile.getFullPathName());

        juce::ChildProcess process;
        // Do not capture pipes here: a blocking pipe read would make cancellation unreliable.
        // The analyzer's machine-readable output is written directly to resultFile.
        if (! process.start (arguments, 0))
        {
            fail ("Could not launch the development analyzer.");
            return;
        }

        while (process.isRunning())
        {
            if (threadShouldExit())
            {
                process.kill();
                return;
            }
            process.waitForProcessToFinish (100);
        }
        if (threadShouldExit())
            return;

        if (process.getExitCode() != 0)
        {
            fail ("Analyzer failed with exit code " + juce::String { process.getExitCode() }
                  + ". Run scripts/analyze in Terminal for diagnostics.");
            return;
        }

        const auto json = resultFile.loadFileAsString();
        if (json.trim().isEmpty())
        {
            fail ("Analyzer produced no protocol result.");
            return;
        }

        {
            const juce::ScopedLock lock (dataLock);
            pendingResult = json;
            status = "Applying analysis...";
        }
        state.store (DevelopmentAnalysisState::applying);
        triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        if (state.load() != DevelopmentAnalysisState::applying)
            return;

        juce::String json;
        {
            const juce::ScopedLock lock (dataLock);
            json = pendingResult;
            pendingResult.clear();
        }
        const auto applied = owner.applyAnalysisResultJson (json);
        {
            const juce::ScopedLock lock (dataLock);
            status = applied.wasOk() ? "Analysis applied" : applied.getErrorMessage();
        }
        state.store (applied.wasOk() ? DevelopmentAnalysisState::succeeded
                                     : DevelopmentAnalysisState::failed);
    }

    PluginProcessor& owner;
    mutable juce::CriticalSection dataLock;
    juce::String prompt, status, pendingResult;
    int iterations { 50 };
    std::atomic<DevelopmentAnalysisState> state { DevelopmentAnalysisState::idle };
};

namespace
{
constexpr double parameterRampSeconds = 0.05;
constexpr float bypassRampSeconds = 0.02f;
constexpr int coefficientUpdateInterval = 32;

juce::NormalisableRange<float> logarithmicRange (float minimum, float maximum)
{
    juce::NormalisableRange<float> range { minimum, maximum };
    range.setSkewForCentre (std::sqrt (minimum * maximum));
    return range;
}

float value (const juce::AudioProcessorValueTreeState& state, const char* id) noexcept
{
    return state.getRawParameterValue (id)->load();
}
} // namespace

PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    developmentRunner = std::make_unique<DevelopmentAnalysisRunner> (*this);
}

PluginProcessor::~PluginProcessor()
{
    developmentRunner.reset();
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    using FloatParameter = juce::AudioParameterFloat;
    using ID = juce::ParameterID;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto addBand = [&layout] (parameter::BandIDs ids, const juce::String& name,
                                    float minimumFrequency, float maximumFrequency, float defaultFrequency)
    {
        layout.add (std::make_unique<FloatParameter> (ID { ids.gain, 1 }, name + " Gain",
                                                       juce::NormalisableRange<float> { -20.0f, 20.0f, 0.1f }, 0.0f));
        layout.add (std::make_unique<FloatParameter> (ID { ids.frequency, 1 }, name + " Frequency",
                                                       logarithmicRange (minimumFrequency, maximumFrequency), defaultFrequency));
        layout.add (std::make_unique<FloatParameter> (ID { ids.q, 1 }, name + " Q",
                                                       logarithmicRange (0.1f, 6.0f), 0.7071f));
    };

    addBand (parameter::bands[0], "Low Shelf", 20.0f, 2000.0f, 200.0f);
    addBand (parameter::bands[1], "Low Mid", 80.0f, 2000.0f, 500.0f);
    addBand (parameter::bands[2], "Mid", 2000.0f, 8000.0f, 3500.0f);
    addBand (parameter::bands[3], "High Mid", 8000.0f, 12000.0f, 9000.0f);
    addBand (parameter::bands[4], "Air", 12000.0f, 21050.0f, 14000.0f);
    addBand (parameter::bands[5], "High Shelf", 4000.0f, 21050.0f, 12000.0f);
    layout.add (std::make_unique<FloatParameter> (ID { parameter::outputGain, 1 }, "Output Gain",
                                                   juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f));
    layout.add (std::make_unique<FloatParameter> (ID { parameter::mix, 1 }, "Mix",
                                                   juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f));
    // Keep this normalized value continuous so arbitrary host automation/state test values
    // round-trip exactly. The toggle writes 0/1 and DSP uses a 0.5 threshold.
    layout.add (std::make_unique<FloatParameter> (ID { parameter::bypass, 1 }, "Bypass",
                                                   juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
    return layout;
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    return input == layouts.getMainOutputChannelSet()
        && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

void PluginProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = std::max (sampleRate, 1.0);
    for (auto& bank : filters)
        for (auto& filter : bank)
            filter.reset();
    resetSmoothers();
    captureBuffer.prepare (currentSampleRate, std::max (1, std::min (getTotalNumInputChannels(), 2)), 10.0);
}

void PluginProcessor::resetSmoothers()
{
    for (size_t index = 0; index < bandSmoothers.size(); ++index)
    {
        auto& smoother = bandSmoothers[index];
        smoother.gain.reset (currentSampleRate, parameterRampSeconds);
        smoother.frequency.reset (currentSampleRate, parameterRampSeconds);
        smoother.q.reset (currentSampleRate, parameterRampSeconds);
        smoother.gain.setCurrentAndTargetValue (value (parameters, parameter::bands[index].gain));
        smoother.frequency.setCurrentAndTargetValue (value (parameters, parameter::bands[index].frequency));
        smoother.q.setCurrentAndTargetValue (value (parameters, parameter::bands[index].q));
    }
    outputGainSmooth.reset (currentSampleRate, bypassRampSeconds);
    wetMixSmooth.reset (currentSampleRate, bypassRampSeconds);
    outputGainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (value (parameters, parameter::outputGain)));
    const auto bypassed = value (parameters, parameter::bypass) >= 0.5f;
    wetMixSmooth.setCurrentAndTargetValue (bypassed ? 0.0f : value (parameters, parameter::mix) * 0.01f);
}

void PluginProcessor::updateTargets() noexcept
{
    for (size_t index = 0; index < bandSmoothers.size(); ++index)
    {
        auto& smoother = bandSmoothers[index];
        smoother.gain.setTargetValue (value (parameters, parameter::bands[index].gain));
        smoother.frequency.setTargetValue (value (parameters, parameter::bands[index].frequency));
        smoother.q.setTargetValue (value (parameters, parameter::bands[index].q));
    }
    outputGainSmooth.setTargetValue (juce::Decibels::decibelsToGain (value (parameters, parameter::outputGain)));
    const auto bypassed = value (parameters, parameter::bypass) >= 0.5f;
    wetMixSmooth.setTargetValue (bypassed ? 0.0f : value (parameters, parameter::mix) * 0.01f);
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    captureBuffer.push (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), buffer.getNumSamples());
    updateTargets();

    const auto channels = std::min (buffer.getNumChannels(), 2);
    const auto samples = buffer.getNumSamples();
    for (int start = 0; start < samples; start += coefficientUpdateInterval)
    {
        const auto count = std::min (coefficientUpdateInterval, samples - start);
        std::array<eq::Coefficients, 6> nextCoefficients;
        for (size_t index = 0; index < bandSmoothers.size(); ++index)
        {
            auto& smoother = bandSmoothers[index];
            const auto gain = smoother.gain.skip (count);
            const auto frequency = smoother.frequency.skip (count);
            const auto q = smoother.q.skip (count);
            nextCoefficients[index] = index == 0 ? eq::lowShelf (currentSampleRate, frequency, q, gain)
                                    : index == 5 ? eq::highShelf (currentSampleRate, frequency, q, gain)
                                                 : eq::peak (currentSampleRate, frequency, q, gain);
        }

        for (int channel = 0; channel < channels; ++channel)
            for (size_t index = 0; index < filters.size(); ++index)
                filters[index][static_cast<size_t> (channel)].setCoefficients (nextCoefficients[index]);

        for (int sample = start; sample < start + count; ++sample)
        {
            const auto outputGain = outputGainSmooth.getNextValue();
            const auto wetMix = wetMixSmooth.getNextValue();
            for (int channel = 0; channel < channels; ++channel)
            {
                auto* data = buffer.getWritePointer (channel);
                const auto dry = data[sample];
                auto wet = dry;
                for (auto& bank : filters)
                    wet = bank[static_cast<size_t> (channel)].process (wet);
                wet *= outputGain;
                data[sample] = dry + wetMix * (wet - dry);
            }
        }
    }
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destination);
}

void PluginProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size); xml != nullptr)
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::Result PluginProcessor::applyAnalysisResultJson (const juce::String& json)
{
    auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
    if (messageManager == nullptr || ! messageManager->isThisTheMessageThread())
        return juce::Result::fail ("Analysis results must be applied on the message thread.");

    protocol::AnalysisResult result;
    if (const auto parsed = protocol::parseAnalysisResult (json, result); parsed.failed())
        return parsed;

    std::array<juce::RangedAudioParameter*, protocol::canonicalParameterNames.size()> targets {};
    std::array<float, protocol::canonicalParameterNames.size()> clampedValues {};
    for (size_t index = 0; index < protocol::canonicalParameterNames.size(); ++index)
    {
        targets[index] = parameters.getParameter (protocol::canonicalParameterNames[index]);
        if (targets[index] == nullptr)
            return juce::Result::fail ("Plugin parameter mapping is incomplete.");

        const auto& range = targets[index]->getNormalisableRange();
        clampedValues[index] = range.snapToLegalValue (std::clamp (static_cast<float> (result.values[index]),
                                                                   range.start, range.end));
    }

    for (size_t index = 0; index < targets.size(); ++index)
    {
        targets[index]->beginChangeGesture();
        targets[index]->setValueNotifyingHost (targets[index]->convertTo0to1 (clampedValues[index]));
        targets[index]->endChangeGesture();
    }

    return juce::Result::ok();
}

juce::Result PluginProcessor::startDevelopmentCapture()
{
    if (developmentRunner->isBusy())
        return juce::Result::fail ("Cancel the current analysis before capturing again.");
    if (! captureBuffer.start())
        return juce::Result::fail ("Capture is unavailable or already running.");
    developmentRunner->resetStatus();
    return juce::Result::ok();
}

void PluginProcessor::cancelDevelopmentWork()
{
    captureBuffer.cancel();
    developmentRunner->cancel();
}

juce::Result PluginProcessor::startDevelopmentAnalysis (const juce::String& prompt, int iterations)
{
    return developmentRunner->start (prompt, iterations);
}

DevelopmentAnalysisState PluginProcessor::getDevelopmentAnalysisState() const noexcept
{
    return developmentRunner->getState();
}

juce::String PluginProcessor::getDevelopmentStatus() const
{
    return developmentRunner->getStatus();
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}
} // namespace semantifx

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new semantifx::PluginProcessor();
}
