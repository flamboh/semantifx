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
        : Thread ("Semantifx development analysis"), owner (processor),
          spoolDirectory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getNonexistentChildFile ("semantifx-worker", {}, true))
    {
        spoolDirectory.createDirectory();
    }

    ~DevelopmentAnalysisRunner() override
    {
        signalThreadShouldExit();
        notify();
        const auto stopped = stopThread (3000);
        cancelPendingUpdate();
        if (stopped)
        {
            if (terminateWorker())
                spoolDirectory.deleteRecursively();
        }
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
            requestId = juce::Uuid {}.toString();
            iterations = std::clamp (newIterations, 1, 10000);
            status = "Writing captured audio...";
            pendingResult.reset();
            progress.reset();
            applyingPreview = false;
            analysisStartedMs = juce::Time::getMillisecondCounterHiRes();
            optimizationStartedMs = 0.0;
            lastEtaUpdateMs = 0.0;
            remainingText.clear();
        }
        stopRequested.store (false);
        acceptLatestPreview.store (false);
        state.store (DevelopmentAnalysisState::running);
        if (! startThread())
        {
            fail ("Could not start the analysis thread.");
            return juce::Result::fail (getStatus());
        }
        return juce::Result::ok();
    }

    void stop (bool useLatestPreview)
    {
        acceptLatestPreview.store (useLatestPreview);
        stopRequested.store (true);
        auto expected = DevelopmentAnalysisState::running;
        if (! state.compare_exchange_strong (expected, DevelopmentAnalysisState::cancelling))
            return;
        {
            const juce::ScopedLock lock (dataLock);
            status = useLatestPreview ? "Stopping and keeping the latest preview..."
                                      : "Cancelling analysis...";
        }
        notify();
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

    std::optional<protocol::AnalysisProgress> getProgress() const
    {
        const juce::ScopedLock lock (dataLock);
        return progress;
    }

    juce::String getLastAppliedDirection() const
    {
        const juce::ScopedLock lock (dataLock);
        return lastAppliedDirection;
    }

private:
    static juce::String durationText (double seconds)
    {
        const auto total = std::max (0, juce::roundToInt (seconds));
        const auto minutes = total / 60;
        const auto remainder = total % 60;
        return minutes > 0 ? juce::String { minutes } + "m " + juce::String { remainder } + "s"
                           : juce::String { remainder } + "s";
    }

    void readProgress()
    {
        const auto json = progressFile().loadFileAsString();
        if (json.trim().isEmpty())
            return;

        protocol::AnalysisProgress next;
        if (protocol::parseAnalysisProgress (json, next).failed())
            return;

        juce::String activeRequestId;
        std::optional<protocol::AnalysisProgress> previous;
        {
            const juce::ScopedLock lock (dataLock);
            activeRequestId = requestId;
            previous = progress;
        }
        if (next.requestId != activeRequestId
            || (previous.has_value() && (next.totalSteps != previous->totalSteps
                                         || next.step < previous->step)))
            return;

        const auto now = juce::Time::getMillisecondCounterHiRes();
        juce::String nextStatus;
        if (next.phase == "preparing")
        {
            nextStatus = "Preparing captured audio...";
        }
        else if (next.phase == "loading_model")
        {
            nextStatus = "Loading MS-CLAP model  |  " + durationText ((now - analysisStartedMs) / 1000.0)
                       + " elapsed";
        }
        else if (next.phase == "optimizing")
        {
            if (optimizationStartedMs <= 0.0)
                optimizationStartedMs = now;
            const auto elapsed = (now - analysisStartedMs) / 1000.0;
            const auto optimizationElapsed = (now - optimizationStartedMs) / 1000.0;
            const auto remaining = next.step > 0
                ? optimizationElapsed * static_cast<double> (next.totalSteps - next.step)
                    / static_cast<double> (next.step)
                : 0.0;
            if (next.step > 0 && next.step < next.totalSteps
                && (remainingText.isEmpty() || now - lastEtaUpdateMs >= 1000.0))
            {
                remainingText = durationText (remaining);
                lastEtaUpdateMs = now;
            }
            nextStatus = "Optimizing EQ  |  " + juce::String { next.step } + " / "
                       + juce::String { next.totalSteps } + "  |  " + durationText (elapsed) + " elapsed";
            if (! remainingText.isEmpty() && next.step < next.totalSteps)
                nextStatus += "  |  ~" + remainingText + " left";
        }
        else if (next.phase == "finalizing")
        {
            nextStatus = "Finalizing result  |  " + juce::String { next.totalSteps } + " / "
                       + juce::String { next.totalSteps };
        }
        else
        {
            nextStatus = next.phase;
        }

        const juce::ScopedLock lock (dataLock);
        progress = std::move (next);
        if (state.load() == DevelopmentAnalysisState::running)
            status = std::move (nextStatus);
    }

    void fail (const juce::String& message)
    {
        const juce::ScopedLock lock (dataLock);
        status = message;
        state.store (DevelopmentAnalysisState::failed);
    }

    juce::File requestFile() const { return spoolDirectory.getChildFile ("request.json"); }
    juce::File progressFile() const { return spoolDirectory.getChildFile ("progress.json"); }
    juce::File resultFile() const { return spoolDirectory.getChildFile ("result.json"); }
    juce::File stopFile() const { return spoolDirectory.getChildFile ("stop.json"); }

    void removeRequestFiles()
    {
        for (const auto& file : std::array<juce::File, 8> {
                 requestFile(), progressFile(), resultFile(), stopFile(),
                 requestFile().getSiblingFile ("request.json.tmp"),
                 progressFile().getSiblingFile ("progress.json.tmp"),
                 resultFile().getSiblingFile ("result.json.tmp"),
                 stopFile().getSiblingFile ("stop.json.tmp") })
            file.deleteFile();
    }

    bool writeJsonAtomically (const juce::File& destination, const juce::var& value)
    {
        const auto temporary = destination.getSiblingFile (destination.getFileName() + ".tmp");
        temporary.deleteFile();
        if (! temporary.replaceWithText (juce::JSON::toString (value, true)))
            return false;
        destination.deleteFile();
        return temporary.moveFileTo (destination);
    }

    bool ensureWorker()
    {
        if (workerPoisoned.load())
        {
            if (workerProcess != nullptr && workerProcess->isRunning())
            {
                fail ("The previous development worker could not be terminated. Reopen the plugin before shaping again.");
                return false;
            }
            workerProcess.reset();
            workerPoisoned.store (false);
        }
        if (workerProcess != nullptr && workerProcess->isRunning())
            return true;

        workerProcess = std::make_unique<juce::ChildProcess>();
        const auto script = juce::File { juce::String { SEMANTIFX_DEVELOPMENT_REPO_PATH } }
                                .getChildFile ("scripts").getChildFile ("worker");
        if (! script.existsAsFile())
        {
            fail ("Development worker not found at " + script.getFullPathName());
            return false;
        }

        juce::StringArray arguments { script.getFullPathName(), spoolDirectory.getFullPathName() };
        // The worker communicates through atomic spool files, so no pipe can fill and block cancellation.
        if (! workerProcess->start (arguments, 0))
        {
            fail ("Could not launch the persistent development worker.");
            workerProcess.reset();
            return false;
        }
        return true;
    }

    bool terminateWorker()
    {
        if (workerProcess == nullptr)
        {
            workerPoisoned.store (false);
            return true;
        }
        if (workerProcess->isRunning())
        {
            if (! workerProcess->kill() && workerProcess->isRunning())
            {
                workerPoisoned.store (true);
                return false;
            }
            workerProcess->waitForProcessToFinish (1000);
        }
        if (workerProcess->isRunning())
        {
            workerPoisoned.store (true);
            return false;
        }
        workerProcess.reset();
        workerPoisoned.store (false);
        return true;
    }

    bool publishRequest (const juce::File& captureFile)
    {
        juce::String localPrompt, localRequestId;
        int localIterations {};
        {
            const juce::ScopedLock lock (dataLock);
            localPrompt = prompt;
            localRequestId = requestId;
            localIterations = iterations;
        }

        auto request = juce::var { new juce::DynamicObject {} };
        auto* object = request.getDynamicObject();
        object->setProperty ("protocol_version", "1");
        object->setProperty ("type", "analyze");
        object->setProperty ("request_id", localRequestId);
        object->setProperty ("prompt", localPrompt);

        auto audio = juce::var { new juce::DynamicObject {} };
        audio.getDynamicObject()->setProperty ("path", captureFile.getFullPathName());
        object->setProperty ("audio", audio);
        juce::Array<juce::var> effects;
        effects.add ("eq");
        object->setProperty ("effects", effects);

        auto options = juce::var { new juce::DynamicObject {} };
        auto* optionObject = options.getDynamicObject();
        optionObject->setProperty ("backend", "text2fx");
        optionObject->setProperty ("iterations", localIterations);
        optionObject->setProperty ("model", "ms_clap");
        optionObject->setProperty ("criterion", "cosine-sim");
        optionObject->setProperty ("learning_rate", 0.01);
        optionObject->setProperty ("parameter_initialization", "zeros");
        optionObject->setProperty ("parameter_regularization", 0.0);
        optionObject->setProperty ("random_seed", 0);
        object->setProperty ("options", options);
        return writeJsonAtomically (requestFile(), request);
    }

    bool publishStopRequest()
    {
        juce::String localRequestId;
        {
            const juce::ScopedLock lock (dataLock);
            localRequestId = requestId;
        }
        auto value = juce::var { new juce::DynamicObject {} };
        value.getDynamicObject()->setProperty ("request_id", localRequestId);
        return writeJsonAtomically (stopFile(), value);
    }

    void resolveInterruption (const juce::String& activeRequestId)
    {
        std::optional<protocol::AnalysisProgress> latest;
        {
            const juce::ScopedLock lock (dataLock);
            latest = progress;
        }
        if (acceptLatestPreview.load() && latest.has_value() && latest->hasParameters)
        {
            auto preview = latest->preview;
            preview.requestId = activeRequestId;
            queueApplication (std::move (preview), true, latest->step, latest->totalSteps,
                              DevelopmentAnalysisState::cancelling);
            return;
        }

        auto expected = DevelopmentAnalysisState::cancelling;
        if (state.compare_exchange_strong (expected, DevelopmentAnalysisState::cancelled))
        {
            const juce::ScopedLock lock (dataLock);
            status = "Analysis cancelled";
        }
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
        const auto captureFile = juce::File::createTempFile ("-semantifx-capture.wav");
        bool preserveCapture {};
        const auto removeCapture = juce::ScopeGuard { [&]
        {
            if (! preserveCapture)
                captureFile.deleteFile();
        } };

        if (! writeCapture (captureFile))
        {
            fail ("Could not write the temporary WAV capture.");
            return;
        }
        if (threadShouldExit())
            return;
        removeRequestFiles();
        if (! ensureWorker())
            return;
        if (! publishRequest (captureFile))
        {
            fail ("Could not submit the analysis request to the development worker.");
            return;
        }
        {
            const juce::ScopedLock lock (dataLock);
            status = "Waiting for persistent Text2FX worker...";
        }

        bool stopPublished = false;
        double stopPublishedMs {};
        while (! resultFile().existsAsFile())
        {
            if (threadShouldExit())
                return;
            if (stopRequested.load() && ! stopPublished)
            {
                if (! publishStopRequest())
                {
                    if (! terminateWorker())
                        preserveCapture = true;
                    fail (preserveCapture
                              ? "Could not interrupt or terminate the development worker. Reopen the plugin before shaping again."
                              : "Could not send the interruption request; the development worker was restarted safely.");
                    return;
                }
                stopPublished = true;
                stopPublishedMs = juce::Time::getMillisecondCounterHiRes();
            }
            readProgress();
            if (stopPublished && juce::Time::getMillisecondCounterHiRes() - stopPublishedMs >= 3000.0)
            {
                if (! terminateWorker())
                {
                    preserveCapture = true;
                    fail ("The development worker did not stop. Reopen the plugin before shaping again.");
                    return;
                }
                readProgress();
                juce::String activeRequestId;
                {
                    const juce::ScopedLock lock (dataLock);
                    activeRequestId = requestId;
                }
                resolveInterruption (activeRequestId);
                return;
            }
            if (workerProcess == nullptr || ! workerProcess->isRunning())
            {
                workerProcess.reset();
                fail ("The persistent development worker stopped unexpectedly. Try shaping again to restart it.");
                return;
            }
            wait (100);
        }
        readProgress();
        if (threadShouldExit())
            return;

        auto json = resultFile().loadFileAsString();
        if (json.trim().isEmpty())
        {
            fail ("Analyzer produced no protocol result.");
            return;
        }

        juce::String activeRequestId;
        {
            const juce::ScopedLock lock (dataLock);
            activeRequestId = requestId;
        }

        juce::var resultRoot;
        if (juce::JSON::parse (json, resultRoot).wasOk())
        {
            if (auto* object = resultRoot.getDynamicObject())
            {
                const auto returnedRequestId = object->getProperty ("request_id");
                if (returnedRequestId.isString() && returnedRequestId.toString() != activeRequestId)
                {
                    resultFile().deleteFile();
                    fail ("The persistent development worker returned a stale result. Try shaping again.");
                    return;
                }
            }
        }

        protocol::AnalysisResult finalResult;
        if (protocol::parseAnalysisResult (json, finalResult).wasOk()
            && finalResult.requestId == activeRequestId)
        {
            auto expected = DevelopmentAnalysisState::running;
            if (state.compare_exchange_strong (expected, DevelopmentAnalysisState::applying))
            {
                queueApplication (std::move (finalResult), false, 0, 0,
                                  DevelopmentAnalysisState::applying);
            }
            else if (expected == DevelopmentAnalysisState::cancelling)
            {
                resolveInterruption (activeRequestId);
            }
            return;
        }

        juce::String cancelledRequestId;
        if (protocol::parseAnalysisCancellation (json, cancelledRequestId).wasOk()
            && cancelledRequestId == activeRequestId)
        {
            resolveInterruption (activeRequestId);
            return;
        }

        auto errorMessage = juce::String { "The development worker returned an invalid result." };
        juce::var errorRoot;
        if (juce::JSON::parse (json, errorRoot).wasOk())
            if (auto* object = errorRoot.getDynamicObject())
            {
                const auto errorValue = object->getProperty ("error");
                if (auto* error = errorValue.getDynamicObject())
                    if (error->getProperty ("message").isString())
                        errorMessage = error->getProperty ("message").toString();
            }
        fail (errorMessage);
    }

    void queueApplication (protocol::AnalysisResult result, bool preview, int step, int totalSteps,
                           DevelopmentAnalysisState expectedState)
    {
        if (expectedState != DevelopmentAnalysisState::applying)
        {
            auto expected = expectedState;
            if (! state.compare_exchange_strong (expected, DevelopmentAnalysisState::applying))
                return;
        }
        {
            const juce::ScopedLock lock (dataLock);
            pendingResult = std::move (result);
            applyingPreview = preview;
            appliedPreviewStep = step;
            appliedPreviewTotal = totalSteps;
            status = preview ? "Applying interrupted preview..." : "Applying analysis...";
        }
        triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        if (state.load() != DevelopmentAnalysisState::applying)
            return;

        std::optional<protocol::AnalysisResult> result;
        bool preview {};
        int previewStep {}, previewTotal {};
        {
            const juce::ScopedLock lock (dataLock);
            result = pendingResult;
            pendingResult.reset();
            preview = applyingPreview;
            previewStep = appliedPreviewStep;
            previewTotal = appliedPreviewTotal;
        }
        const auto applied = result.has_value() ? owner.applyAnalysisResult (*result)
                                                : juce::Result::fail ("No analysis result was ready to apply.");
        {
            const juce::ScopedLock lock (dataLock);
            if (applied.wasOk())
            {
                lastAppliedDirection = prompt;
                status = preview ? "Applied preview at " + juce::String { previewStep } + " / "
                                     + juce::String { previewTotal }
                                 : "Analysis applied";
            }
            else
            {
                status = applied.getErrorMessage();
            }
        }
        state.store (applied.wasOk() ? DevelopmentAnalysisState::succeeded
                                     : DevelopmentAnalysisState::failed);
    }

    PluginProcessor& owner;
    const juce::File spoolDirectory;
    std::unique_ptr<juce::ChildProcess> workerProcess;
    mutable juce::CriticalSection dataLock;
    juce::String prompt, requestId, status, lastAppliedDirection;
    int iterations { 600 };
    std::optional<protocol::AnalysisProgress> progress;
    std::optional<protocol::AnalysisResult> pendingResult;
    bool applyingPreview {};
    int appliedPreviewStep {}, appliedPreviewTotal {};
    double analysisStartedMs {};
    double optimizationStartedMs {};
    double lastEtaUpdateMs {};
    juce::String remainingText;
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> acceptLatestPreview { false };
    std::atomic<bool> workerPoisoned { false };
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

    return applyAnalysisResult (result);
}

juce::Result PluginProcessor::applyAnalysisResult (const protocol::AnalysisResult& result)
{
    auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
    if (messageManager == nullptr || ! messageManager->isThisTheMessageThread())
        return juce::Result::fail ("Analysis results must be applied on the message thread.");

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

void PluginProcessor::stopDevelopmentWork (bool acceptLatestPreview)
{
    captureBuffer.cancel();
    developmentRunner->stop (acceptLatestPreview);
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

std::optional<protocol::AnalysisProgress> PluginProcessor::getDevelopmentProgress() const
{
    return developmentRunner->getProgress();
}

juce::String PluginProcessor::getLastAppliedDirection() const
{
    return developmentRunner->getLastAppliedDirection();
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
