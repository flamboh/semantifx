#include "PluginProcessor.h"
#include "JuceConvolver.h"
#include "PluginEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace semantifx
{
namespace
{
constexpr double parameterRampSeconds = 0.05;
constexpr double globalRampSeconds = 0.02;

float rawValue (const std::atomic<float>* value) noexcept
{
    return value == nullptr ? 0.0f : value->load (std::memory_order_relaxed);
}

bool onMessageThread() noexcept
{
    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    return manager != nullptr && manager->isThisTheMessageThread();
}

juce::String durationText (double seconds)
{
    const auto total = std::max (0, juce::roundToInt (seconds));
    const auto minutes = total / 60;
    const auto remainder = total % 60;
    return minutes > 0 ? juce::String { minutes } + "m " + juce::String { remainder } + "s"
                       : juce::String { remainder } + "s";
}
} // namespace

class DevelopmentAnalysisRunner final : private juce::Thread, private juce::AsyncUpdater
{
    // Runner state machine. M = message thread, R = runner thread. `state`
    // is the single source of truth; the pending* queues are guarded by
    // dataLock and only acted on while `state` still permits it.
    //
    // | From               | Event (thread)                              | To         | Notes                                  |
    // |--------------------|---------------------------------------------|------------|----------------------------------------|
    // | idle/terminal      | start() (M)                                 | assembling | or running for optimize; thread born  |
    // | assembling         | R consumes assembly file                    | applying   | CAS; thread stays alive                |
    // | running            | R consumes analysis result                  | applying   | CAS; thread stays alive                |
    // | applying           | M applies assembly, chain non-empty         | running    | restartRequested; R republishes        |
    // | applying           | M applies assembly, chain empty             | succeeded  | finishRequested; R exits               |
    // | applying           | M applies analysis result                   | succeeded  | or failed if the apply failed          |
    // | applying           | stop()/revert (M)                           | cancelling | pending application dropped            |
    // | applying           | restartImmediately() (M)                    | running    | pending dropped; restartRequested      |
    // | assembling/running | stop()/revert (M)                           | cancelling | stopRequested; R interrupts worker     |
    // | assembling/running | restartImmediately() (M)                    | running    | restartRequested; R republishes        |
    // | cancelling         | R: result already taken                     | cancelled  | worker idle; no stop roundtrip         |
    // | cancelling         | R: worker answers stop / 3 s timeout        | cancelled  | worker killed on timeout               |
    // | succeeded/failed   | R observes terminal state                   | (exit)     | thread returns                         |
    // | any                | threadShouldExit (R)                        | (exit)     | destructor / finishRequested           |
    // | terminal           | resetStatus() (M, when !isBusy())           | idle       |                                        |
    //
    // R never exits while `state == applying` except via threadShouldExit:
    // it waits for M to acknowledge the queued application, cancellation, or
    // restart. Stop/Revert/restart during `applying` cancel the pending
    // application (pendings cleared, async update cancelled) and move `state`
    // on; handleAsyncUpdate() drops a pending whose state has moved on.
public:
    explicit DevelopmentAnalysisRunner (PluginProcessor& processor)
        : Thread ("SemantiFX development worker"), owner (processor),
          spoolDirectory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getNonexistentChildFile ("semantifx-worker", {}, true))
    {
        spoolDirectory.createDirectory();
    }

    ~DevelopmentAnalysisRunner() override
    {
        signalThreadShouldExit();
        stopRequested.store (true, std::memory_order_release);
        notify();
        const auto stopped = stopThread (3000);
        cancelPendingUpdate();
        if (stopped)
        {
            terminateWorker();
            spoolDirectory.deleteRecursively();
        }
    }

    juce::Result start (bool assemble, const juce::String& newPrompt, int newIterations)
    {
        if (isThreadRunning())
            return juce::Result::fail ("The previous worker request is still shutting down.");
        if (isBusy())
            return juce::Result::fail ("A SemantiFX run is already active.");
        if (owner.captureBuffer.getState() != CaptureBuffer::State::ready)
            return juce::Result::fail ("Capture ten seconds of audio before running SemantiFX.");
        if (newPrompt.trim().isEmpty())
            return juce::Result::fail ("Enter a description before running SemantiFX.");
        if (! assemble && owner.getComposition().items.empty())
            return juce::Result::fail ("Add an effect to the chain before optimizing it.");

        {
            const juce::ScopedLock lock (dataLock);
            prompt = newPrompt.trim();
            iterations = std::clamp (newIterations, 1, 10000);
            operation.store (assemble ? Operation::assembling : Operation::optimizing,
                             std::memory_order_release);
            requestId.clear();
            progress.reset();
            pendingProgress.reset();
            pendingAssembly.reset();
            pendingResult.reset();
            status = assemble ? "Assembling chain..." : "Optimizing chain...";
            lastAppliedDirection.clear();
            startedMs = juce::Time::getMillisecondCounterHiRes();
            optimizationStartedMs = 0.0;
            remainingText.clear();
            preRunSnapshot = owner.makeSnapshot();
        }
        stopRequested.store (false, std::memory_order_release);
        restartRequested.store (false, std::memory_order_release);
        finishRequested.store (false, std::memory_order_release);
        workerPoisoned.store (false, std::memory_order_release);
        clearRestartDeadline();
        state.store (assemble ? DevelopmentAnalysisState::assembling
                              : DevelopmentAnalysisState::running,
                     std::memory_order_release);
        if (! startThread())
        {
            fail ("Could not start the SemantiFX worker thread.");
            return juce::Result::fail (getStatus());
        }
        return juce::Result::ok();
    }

    void stop (bool revert)
    {
        const auto current = state.load (std::memory_order_acquire);
        if (current != DevelopmentAnalysisState::assembling
            && current != DevelopmentAnalysisState::running
            && current != DevelopmentAnalysisState::applying)
            return;

        if (revert)
        {
            owner.restoreSnapshot (preRunSnapshot);
            cancelPendingUpdate();
        }
        {
            const juce::ScopedLock lock (dataLock);
            status = revert ? "Reverted" : "Stopped";
            pendingProgress.reset();
            pendingAssembly.reset();
            pendingResult.reset();
        }
        clearRestartDeadline();
        stopRequested.store (true, std::memory_order_release);
        state.store (DevelopmentAnalysisState::cancelling, std::memory_order_release);
        notify();
    }

    void restartImmediately()
    {
        const auto current = state.load (std::memory_order_acquire);
        if (current == DevelopmentAnalysisState::applying)
        {
            // Cancel the pending application; the waiting runner thread picks
            // up `running` + restartRequested and republishes.
            cancelPendingUpdate();
            {
                const juce::ScopedLock lock (dataLock);
                pendingProgress.reset();
                pendingAssembly.reset();
                pendingResult.reset();
                operation.store (Operation::optimizing, std::memory_order_release);
                status = "Optimizing chain...";
            }
            clearRestartDeadline();
            stopRequested.store (false, std::memory_order_release);
            finishRequested.store (false, std::memory_order_release);
            state.store (DevelopmentAnalysisState::running, std::memory_order_release);
            if (isThreadRunning())
            {
                restartRequested.store (true, std::memory_order_release);
                notify();
            }
            else if (! startThread())
            {
                fail ("Could not restart the analysis request.");
            }
            return;
        }
        if (current == DevelopmentAnalysisState::assembling
            || current == DevelopmentAnalysisState::running)
        {
            clearRestartDeadline();
            restartRequested.store (true, std::memory_order_release);
            notify();
        }
    }

    void resetStatus()
    {
        if (isBusy())
            return;
        const juce::ScopedLock lock (dataLock);
        status.clear();
        progress.reset();
        state.store (DevelopmentAnalysisState::idle, std::memory_order_release);
    }

    bool isBusy() const noexcept
    {
        const auto current = state.load (std::memory_order_acquire);
        return isThreadRunning() || current == DevelopmentAnalysisState::assembling
            || current == DevelopmentAnalysisState::running
            || current == DevelopmentAnalysisState::cancelling
            || current == DevelopmentAnalysisState::applying;
    }

    DevelopmentAnalysisState getState() const noexcept
    {
        return state.load (std::memory_order_acquire);
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
    enum class Operation { assembling, optimizing };

    juce::File requestFile() const { return spoolDirectory.getChildFile ("request.json"); }
    juce::File progressFile() const { return spoolDirectory.getChildFile ("progress.json"); }
    juce::File resultFile() const { return spoolDirectory.getChildFile ("result.json"); }
    juce::File stopFile() const { return spoolDirectory.getChildFile ("stop.json"); }
    /** Runner-owned claim name; the shared result.json is renamed here, never read-then-deleted. */
    juce::File takenResultFile() const { return spoolDirectory.getChildFile ("result.taken.json"); }

    void clearRestartDeadline()
    {
        owner.parameterRestartDeadline.store (0, std::memory_order_release);
    }

    void fail (const juce::String& message)
    {
        const juce::ScopedLock lock (dataLock);
        status = message;
        state.store (DevelopmentAnalysisState::failed, std::memory_order_release);
        clearRestartDeadline();
    }

    bool writeJsonAtomically (const juce::File& destination, const juce::var& value)
    {
        const auto temporary = destination.getSiblingFile (destination.getFileName() + ".tmp");
        temporary.deleteFile();
        if (! temporary.replaceWithText (juce::JSON::toString (value, false)))
            return false;
        destination.deleteFile();
        return temporary.moveFileTo (destination);
    }

    void removeRequestFiles()
    {
        for (const auto& file : std::array<juce::File, 9> {
                 requestFile(), progressFile(), resultFile(), stopFile(),
                 takenResultFile(),
                 requestFile().getSiblingFile ("request.json.tmp"),
                 progressFile().getSiblingFile ("progress.json.tmp"),
                 resultFile().getSiblingFile ("result.json.tmp"),
                 stopFile().getSiblingFile ("stop.json.tmp") })
            file.deleteFile();
    }

    bool ensureWorker()
    {
        if (workerProcess != nullptr && workerProcess->isRunning())
            return true;
        if (workerPoisoned.load (std::memory_order_acquire))
        {
            fail ("The previous worker could not be terminated. Reopen the plugin before trying again.");
            return false;
        }

        const auto script = juce::File { juce::String { SEMANTIFX_DEVELOPMENT_REPO_PATH } }
                                .getChildFile ("scripts").getChildFile ("worker");
        if (! script.existsAsFile())
        {
            fail ("Development worker not found at " + script.getFullPathName());
            return false;
        }
        workerProcess = std::make_unique<juce::ChildProcess>();
        const juce::StringArray arguments { script.getFullPathName(), spoolDirectory.getFullPathName() };
        if (! workerProcess->start (arguments, 0))
        {
            workerProcess.reset();
            fail ("Could not launch the persistent development worker.");
            return false;
        }
        return true;
    }

    bool terminateWorker()
    {
        if (workerProcess == nullptr)
            return true;
        if (workerProcess->isRunning())
        {
            if (! workerProcess->kill())
            {
                workerPoisoned.store (true, std::memory_order_release);
                return false;
            }
            workerProcess->waitForProcessToFinish (1000);
        }
        if (workerProcess->isRunning())
        {
            workerPoisoned.store (true, std::memory_order_release);
            return false;
        }
        workerProcess.reset();
        return true;
    }

    juce::var makeAnalyzeChain() const
    {
        auto chain = juce::Array<juce::var> {};
        for (const auto slot : owner.getComposition().items)
        {
            const auto typeIndex = slot.typeIndex;
            const auto& type = protocol::effectTypes[static_cast<size_t> (typeIndex)];
            auto item = juce::var { new juce::DynamicObject {} };
            auto* itemObject = item.getDynamicObject();
            itemObject->setProperty ("instance_id", juce::String { slots::slotName (slot) });
            itemObject->setProperty ("effect_type", type.name);
            auto values = juce::var { new juce::DynamicObject {} };
            auto* valuesObject = values.getDynamicObject();
            const auto slotIndex = slots::slotIndex (slot);
            for (size_t parameterIndex = 0; parameterIndex < type.parameterCount; ++parameterIndex)
            {
                const auto& spec = protocol::parameters[type.firstParameter + parameterIndex];
                valuesObject->setProperty (spec.name,
                    rawValue (owner.rawSlotValues[static_cast<size_t> (slotIndex)][parameterIndex]));
            }
            itemObject->setProperty ("parameters", values);
            chain.add (item);
        }
        return chain;
    }

    bool publishAssemblyRequest()
    {
        juce::String localPrompt, localRequestId;
        {
            const juce::ScopedLock lock (dataLock);
            localPrompt = prompt;
            localRequestId = requestId = juce::Uuid {}.toString();
            progress.reset();
        }
        auto request = juce::var { new juce::DynamicObject {} };
        auto* object = request.getDynamicObject();
        object->setProperty ("type", "assemble");
        object->setProperty ("request_id", localRequestId);
        object->setProperty ("prompt", localPrompt);
        return writeJsonAtomically (requestFile(), request);
    }

    bool publishAnalyzeRequest (const juce::File& captureFile)
    {
        juce::String localPrompt, localRequestId;
        int localIterations {};
        {
            const juce::ScopedLock lock (dataLock);
            localPrompt = prompt;
            localRequestId = requestId = juce::Uuid {}.toString();
            localIterations = iterations;
            expected = owner.expectedChain();
            progress.reset();
            pendingProgress.reset();
            status = "Optimizing chain...";
            optimizationStartedMs = juce::Time::getMillisecondCounterHiRes();
        }

        auto request = juce::var { new juce::DynamicObject {} };
        auto* object = request.getDynamicObject();
        object->setProperty ("type", "analyze");
        object->setProperty ("request_id", localRequestId);
        object->setProperty ("prompt", localPrompt);
        auto audio = juce::var { new juce::DynamicObject {} };
        audio.getDynamicObject()->setProperty ("path", captureFile.getFullPathName());
        object->setProperty ("audio", audio);
        object->setProperty ("chain", makeAnalyzeChain());

        auto options = juce::var { new juce::DynamicObject {} };
        auto* optionObject = options.getDynamicObject();
        optionObject->setProperty ("backend", "clap");
        optionObject->setProperty ("iterations", localIterations);
        optionObject->setProperty ("model", "ms_clap");
        optionObject->setProperty ("criterion", "cosine-sim");
        optionObject->setProperty ("learning_rate", 0.01);
        optionObject->setProperty ("parameter_initialization", "current");
        optionObject->setProperty ("parameter_regularization", 0.0);
        optionObject->setProperty ("random_seed", 0);
        object->setProperty ("options", options);
        return writeJsonAtomically (requestFile(), request);
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
        stream.release();
        std::array<const float*, 2> channels {
            owner.captureBuffer.getReadPointer (0),
            owner.captureBuffer.getNumChannels() > 1 ? owner.captureBuffer.getReadPointer (1) : nullptr };
        return writer->writeFromFloatArrays (channels.data(), owner.captureBuffer.getNumChannels(),
                                             owner.captureBuffer.getNumSamples());
    }

    void updateStatusFromProgress (const protocol::AnalysisProgress& next)
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (next.phase == "optimizing")
        {
            const auto elapsed = (now - startedMs) / 1000.0;
            const auto optimizationElapsed = optimizationStartedMs > 0.0
                ? (now - optimizationStartedMs) / 1000.0 : 0.0;
            if (next.step > 0 && next.step < next.totalSteps)
                remainingText = durationText (optimizationElapsed
                    * static_cast<double> (next.totalSteps - next.step)
                    / static_cast<double> (next.step));
            status = "Optimizing chain | " + juce::String { next.step } + " / "
                   + juce::String { next.totalSteps } + " | " + durationText (elapsed) + " elapsed";
            if (! remainingText.isEmpty() && next.step < next.totalSteps)
                status += " | ~" + remainingText;
        }
        else if (next.phase == "preparing")
            status = "Preparing captured audio...";
        else if (next.phase == "loading_model")
            status = "Loading model | " + durationText ((now - startedMs) / 1000.0) + " elapsed";
        else if (next.phase == "finalizing")
            status = "Optimizing chain | " + juce::String { next.totalSteps } + " / "
                   + juce::String { next.totalSteps };
        else
            status = next.phase;
    }

    void readProgress()
    {
        const auto json = progressFile().loadFileAsString();
        if (json.trim().isEmpty())
            return;

        juce::var root;
        if (juce::JSON::parse (json, root).failed())
            return;
        auto* object = root.getDynamicObject();
        if (object == nullptr || ! object->getProperty ("request_id").isString())
            return;

        protocol::ExpectedChain localExpected;
        juce::String localRequestId;
        std::optional<protocol::AnalysisProgress> previous;
        {
            const juce::ScopedLock lock (dataLock);
            localRequestId = requestId;
            localExpected = expected;
            previous = progress;
        }
        if (object->getProperty ("request_id").toString() != localRequestId)
            return;

        protocol::AnalysisProgress next;
        if (protocol::parseAnalysisProgress (json, localExpected, next).failed())
            return;
        if (previous.has_value() && (next.totalSteps != previous->totalSteps || next.step < previous->step))
            return;
        {
            const juce::ScopedLock lock (dataLock);
            progress = next;
            if (operation.load (std::memory_order_acquire) == Operation::assembling)
                status = "Assembling chain...";
            else
                updateStatusFromProgress (next);
            if (next.hasParameters)
                pendingProgress = next;
        }
        if (next.hasParameters)
            triggerAsyncUpdate();
    }

    void resolveCancellation()
    {
        const juce::ScopedLock lock (dataLock);
        state.store (DevelopmentAnalysisState::cancelled, std::memory_order_release);
        if (status != "Reverted" && status != "Stopped")
            status = "Stopped";
        clearRestartDeadline();
    }

    /**
     * Claims the shared result file by atomically renaming it to the
     * runner-owned taken name before reading. Reading the shared pathname and
     * deleting it afterwards would drop a replacement the worker wrote in
     * between (mirrors the worker's request.json claim in spool_worker.py).
     * Progress stays advisory: it is read in place and never deleted.
     */
    bool consumeResultFile (juce::String& json)
    {
        const auto taken = takenResultFile();
        taken.deleteFile();
        if (! resultFile().existsAsFile())
            return false;
        if (! resultFile().moveFileTo (taken))
            return false;
        json = taken.loadFileAsString();
        taken.deleteFile();
        return ! json.trim().isEmpty();
    }

    static std::optional<juce::String> resultRequestId (const juce::String& json)
    {
        juce::var root;
        if (juce::JSON::parse (json, root).failed())
            return std::nullopt;
        if (auto* object = root.getDynamicObject())
        {
            const auto value = object->getProperty ("request_id");
            if (value.isString())
                return value.toString();
        }
        return std::nullopt;
    }

    void queueAssembly (protocol::AssemblyResult result)
    {
        auto expectedState = DevelopmentAnalysisState::assembling;
        if (! state.compare_exchange_strong (expectedState, DevelopmentAnalysisState::applying))
            return;
        const juce::ScopedLock lock (dataLock);
        pendingAssembly = std::move (result);
        status = "Applying assembled chain...";
        triggerAsyncUpdate();
    }

    void queueResult (protocol::AnalysisResult result)
    {
        auto expectedState = DevelopmentAnalysisState::running;
        if (! state.compare_exchange_strong (expectedState, DevelopmentAnalysisState::applying))
            return;
        const juce::ScopedLock lock (dataLock);
        pendingResult = std::move (result);
        status = "Applying...";
        triggerAsyncUpdate();
    }

    void handleError (const juce::String& json, const juce::String& activeRequestId)
    {
        protocol::ErrorResult error;
        if (protocol::parseErrorResult (json, error).wasOk()
            && (error.requestId.isEmpty() || error.requestId == activeRequestId))
        {
            auto message = error.message;
            if (error.code == "assembly_unavailable" && ! error.remediation.isEmpty())
                message += "\n" + error.remediation[0];
            fail (message);
            return;
        }
        fail ("The development worker returned an invalid result.");
    }

    void run() override
    {
        const auto captureFile = juce::File::createTempFile ("-semantifx-capture.wav");
        const auto removeCapture = juce::ScopeGuard { [&] { captureFile.deleteFile(); } };
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

        const auto assembling = operation.load (std::memory_order_acquire) == Operation::assembling;
        if ((assembling && ! publishAssemblyRequest())
            || (! assembling && ! publishAnalyzeRequest (captureFile)))
        {
            fail ("Could not submit the request to the development worker.");
            return;
        }

        bool stopPublished = false;
        double stopPublishedMs = 0.0;
        bool resultTaken = false;
        while (! threadShouldExit())
        {
            if (finishRequested.exchange (false, std::memory_order_acq_rel))
                return;
            // The thread stays alive while the message thread acknowledges a
            // queued application, cancellation, or restart (see state table).
            const auto awaitingState = state.load (std::memory_order_acquire);
            if (awaitingState == DevelopmentAnalysisState::succeeded
                || awaitingState == DevelopmentAnalysisState::failed)
                return;
            if (awaitingState == DevelopmentAnalysisState::applying)
            {
                wait (50);
                continue;
            }
            if (stopRequested.load (std::memory_order_acquire) && ! stopPublished)
            {
                if (resultTaken)
                {
                    // The worker already delivered; it is idle, so there is
                    // nothing to interrupt. Resolve without a stop roundtrip.
                    resolveCancellation();
                    return;
                }
                juce::String localRequestId;
                {
                    const juce::ScopedLock lock (dataLock);
                    localRequestId = requestId;
                }
                auto stop = juce::var { new juce::DynamicObject {} };
                stop.getDynamicObject()->setProperty ("request_id", localRequestId);
                if (! writeJsonAtomically (stopFile(), stop))
                {
                    fail ("Could not interrupt the development worker.");
                    return;
                }
                stopPublished = true;
                stopPublishedMs = juce::Time::getMillisecondCounterHiRes();
            }

            if (restartRequested.exchange (false, std::memory_order_acq_rel)
                && ! stopRequested.load (std::memory_order_acquire))
            {
                operation.store (Operation::optimizing, std::memory_order_release);
                state.store (DevelopmentAnalysisState::running, std::memory_order_release);
                resultFile().deleteFile();
                progressFile().deleteFile();
                if (! publishAnalyzeRequest (captureFile))
                {
                    fail ("Could not restart the analysis request.");
                    return;
                }
                stopPublished = false;
                resultTaken = false;
            }

            readProgress();
            juce::String json;
            if (consumeResultFile (json))
            {
                const auto returnedId = resultRequestId (json);
                juce::String localRequestId;
                protocol::ExpectedChain localExpected;
                {
                    const juce::ScopedLock lock (dataLock);
                    localRequestId = requestId;
                    localExpected = expected;
                }
                if (! returnedId.has_value() || *returnedId != localRequestId)
                    continue;
                // Only a result for the current request means the worker is idle;
                // a stale delivery for a superseded request must not let Stop skip
                // the cancellation roundtrip.
                resultTaken = true;

                if (stopPublished || stopRequested.load (std::memory_order_acquire))
                {
                    juce::String cancelled;
                    if (protocol::parseAnalysisCancellation (json, cancelled).wasOk()
                        && cancelled == localRequestId)
                    {
                        resolveCancellation();
                        return;
                    }
                    continue;
                }

                if (operation.load (std::memory_order_acquire) == Operation::assembling)
                {
                    protocol::AssemblyResult assembly;
                    if (protocol::parseAssemblyResult (json, assembly).wasOk())
                    {
                        queueAssembly (std::move (assembly));
                        continue;
                    }
                }
                else
                {
                    protocol::AnalysisResult result;
                    if (protocol::parseAnalysisResult (json, localExpected, result).wasOk())
                    {
                        // The CAS can fail if stop()/restart() moved `state`
                        // first; either way the loop below handles the new
                        // state, so never return while `applying` is possible.
                        queueResult (std::move (result));
                        continue;
                    }
                }
                handleError (json, localRequestId);
                return;
            }

            if (stopPublished
                && juce::Time::getMillisecondCounterHiRes() - stopPublishedMs >= 3000.0)
            {
                if (! terminateWorker())
                {
                    fail ("The development worker did not stop. Reopen the plugin before trying again.");
                    return;
                }
                resolveCancellation();
                return;
            }
            if (workerProcess == nullptr || ! workerProcess->isRunning())
            {
                fail ("The persistent development worker stopped unexpectedly.");
                return;
            }
            wait (50);
        }
    }

    void handleAsyncUpdate() override
    {
        std::optional<protocol::AnalysisProgress> nextProgress;
        std::optional<protocol::AssemblyResult> assembly;
        std::optional<protocol::AnalysisResult> result;
        {
            const juce::ScopedLock lock (dataLock);
            nextProgress = pendingProgress;
            pendingProgress.reset();
            assembly = pendingAssembly;
            pendingAssembly.reset();
            result = pendingResult;
            pendingResult.reset();
        }

        if (state.load (std::memory_order_acquire) == DevelopmentAnalysisState::running
            && nextProgress.has_value())
        {
            juce::String activeRequestId;
            {
                const juce::ScopedLock lock (dataLock);
                activeRequestId = requestId;
            }
            if (nextProgress->requestId == activeRequestId)
                owner.applyAnalysisProgress (*nextProgress);
        }

        if (assembly.has_value()
            && state.load (std::memory_order_acquire) == DevelopmentAnalysisState::applying)
        {
            const auto applied = owner.applyAssemblyResult (*assembly);
            if (applied.failed())
            {
                fail (applied.getErrorMessage());
                return;
            }
            if (owner.getComposition().items.empty())
            {
                const juce::ScopedLock lock (dataLock);
                status = "Prompt asks for no change; chain cleared";
                state.store (DevelopmentAnalysisState::succeeded, std::memory_order_release);
                clearRestartDeadline();
                finishRequested.store (true, std::memory_order_release);
                notify();
                return;
            }
            operation.store (Operation::optimizing, std::memory_order_release);
            state.store (DevelopmentAnalysisState::running, std::memory_order_release);
            {
                const juce::ScopedLock lock (dataLock);
                status = "Optimizing chain...";
            }
            restartRequested.store (true, std::memory_order_release);
            notify();
            return;
        }

        if (result.has_value()
            && state.load (std::memory_order_acquire) == DevelopmentAnalysisState::applying)
        {
            const auto applied = owner.applyAnalysisResult (*result);
            const juce::ScopedLock lock (dataLock);
            if (applied.failed())
            {
                status = applied.getErrorMessage();
                state.store (DevelopmentAnalysisState::failed, std::memory_order_release);
            }
            else
            {
                lastAppliedDirection = prompt;
                status = "Applied";
                state.store (DevelopmentAnalysisState::succeeded, std::memory_order_release);
            }
            clearRestartDeadline();
        }
    }

    PluginProcessor& owner;
    const juce::File spoolDirectory;
    std::unique_ptr<juce::ChildProcess> workerProcess;
    mutable juce::CriticalSection dataLock;
    juce::String prompt, requestId, status, lastAppliedDirection, remainingText;
    int iterations { 600 };
    protocol::ExpectedChain expected;
    PluginProcessor::Snapshot preRunSnapshot;
    std::optional<protocol::AnalysisProgress> progress, pendingProgress;
    std::optional<protocol::AssemblyResult> pendingAssembly;
    std::optional<protocol::AnalysisResult> pendingResult;
    std::atomic<Operation> operation { Operation::optimizing };
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> restartRequested { false };
    std::atomic<bool> finishRequested { false };
    std::atomic<bool> workerPoisoned { false };
    std::atomic<DevelopmentAnalysisState> state { DevelopmentAnalysisState::idle };
    double startedMs {};
    double optimizationStartedMs {};
};

PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    parameters.state.setProperty ("chain", "", nullptr);
    for (int index = 0; index < slots::kSlotCount; ++index)
    {
        const auto slot = slots::slotAt (index);
        const auto& type = protocol::effectTypes[static_cast<size_t> (slot.typeIndex)];
        dsp::ReverbResources resources;
#ifdef SEMANTIFX_DEVELOPMENT_REPO_PATH
        const auto noisePath = juce::File { juce::String { SEMANTIFX_DEVELOPMENT_REPO_PATH } }
                                   .getChildFile ("protocol/reverb/noise_44100.f32")
                                   .getFullPathName().toStdString();
        resources.noiseTablePath = juce::String { type.name } == "reverb" ? noisePath.c_str() : nullptr;
#endif
        resources.convolverFactory = [this] {
            return std::make_unique<dsp::JuceConvolver> (convolutionMessageQueue);
        };
        effects[static_cast<size_t> (index)] = dsp::makeEffect (type.name, resources);
    }

    for (int index = 0; index < slots::kSlotCount; ++index)
    {
        const auto slot = slots::slotAt (index);
        const auto& type = protocol::effectTypes[static_cast<size_t> (slot.typeIndex)];
        for (size_t parameterIndex = 0; parameterIndex < type.parameterCount; ++parameterIndex)
            rawSlotValues[static_cast<size_t> (index)][parameterIndex]
                = parameters.getRawParameterValue (parameter::slotParameterId (slot,
                                                                                  static_cast<int> (parameterIndex)));
        rawBypasses[static_cast<size_t> (index)]
            = parameters.getRawParameterValue (parameter::slotBypassId (slot));
    }
    rawOutputGain = parameters.getRawParameterValue (parameter::outputGain);
    rawMix = parameters.getRawParameterValue (parameter::mix);
    rawGlobalBypass = parameters.getRawParameterValue (parameter::bypass);
    syncCompositionFromState();
    developmentRunner = std::make_unique<DevelopmentAnalysisRunner> (*this);
    startTimerHz (30);
}

PluginProcessor::~PluginProcessor()
{
    stopTimer();
    developmentRunner.reset();
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    using FloatParameter = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int typeIndex = 0; typeIndex < protocol::kEffectTypeCount; ++typeIndex)
        for (int ordinal = 1; ordinal <= slots::kSlotsPerType; ++ordinal)
        {
            const slots::Slot slot { typeIndex, ordinal };
            const auto& type = protocol::effectTypes[static_cast<size_t> (typeIndex)];
            for (size_t parameterIndex = 0; parameterIndex < type.parameterCount; ++parameterIndex)
            {
                const auto& spec = protocol::parameters[type.firstParameter + parameterIndex];
                juce::NormalisableRange<float> range { spec.minimum, spec.maximum, spec.step };
                if (spec.logarithmic)
                    range.setSkewForCentre (std::sqrt (spec.minimum * spec.maximum));
                const auto displayName = juce::String { slots::slotTitle (slot) } + " " + spec.displayName;
                layout.add (std::make_unique<FloatParameter> (
                    juce::ParameterID { parameter::slotParameterId (slot, static_cast<int> (parameterIndex)), 1 },
                    displayName, range, spec.defaultValue));
            }
            layout.add (std::make_unique<FloatParameter> (
                juce::ParameterID { parameter::slotBypassId (slot), 1 },
                juce::String { slots::slotTitle (slot) } + " Bypass",
                juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
        }

    layout.add (std::make_unique<FloatParameter> (
        juce::ParameterID { parameter::outputGain, 1 }, "Output Gain",
        juce::NormalisableRange<float> { -24.0f, 12.0f, 0.1f }, 0.0f));
    layout.add (std::make_unique<FloatParameter> (
        juce::ParameterID { parameter::mix, 1 }, "Mix",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f));
    layout.add (std::make_unique<FloatParameter> (
        juce::ParameterID { parameter::bypass, 1 }, "Bypass",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));
    return layout;
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    return input == layouts.getMainOutputChannelSet()
        && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

void PluginProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    currentSampleRate = std::max (sampleRate, 1.0);
    currentMaxBlockSize = std::max (maximumExpectedSamplesPerBlock, 1);
    for (auto& effect : effects)
        effect->prepare (currentSampleRate, currentMaxBlockSize);
    for (int index = 0; index < slots::kSlotCount; ++index)
        initializeEffectTargets (index);
    outputGainSmooth.reset (currentSampleRate, globalRampSeconds);
    wetMixSmooth.reset (currentSampleRate, globalRampSeconds);
    resetSmoothers();
    previousPackedComposition = 0;
    for (auto& effect : effects)
        effect->reset();
    dryBuffer.setSize (2, currentMaxBlockSize, false, true, true);
    captureBuffer.prepare (currentSampleRate,
                           std::max (1, std::min (getTotalNumInputChannels(), 2)), 10.0);
}

void PluginProcessor::initializeEffectTargets (int slotIndex)
{
    const auto slot = slots::slotAt (slotIndex);
    const auto& type = protocol::effectTypes[static_cast<size_t> (slot.typeIndex)];
    std::array<float, maxParametersPerType> values {};
    for (size_t index = 0; index < type.parameterCount; ++index)
        values[index] = rawValue (rawSlotValues[static_cast<size_t> (slotIndex)][index]);
    effects[static_cast<size_t> (slotIndex)]->setTargets (values.data());
    effects[static_cast<size_t> (slotIndex)]->snapSmoothers();
}

void PluginProcessor::resetSmoothers()
{
    outputGainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (rawValue (rawOutputGain)));
    const auto bypassed = rawValue (rawGlobalBypass) >= 0.5f;
    wetMixSmooth.setCurrentAndTargetValue (bypassed ? 0.0f : rawValue (rawMix) * 0.01f);
}

void PluginProcessor::updateGlobalTargets() noexcept
{
    outputGainSmooth.setTargetValue (juce::Decibels::decibelsToGain (rawValue (rawOutputGain)));
    wetMixSmooth.setTargetValue (rawValue (rawGlobalBypass) >= 0.5f ? 0.0f : rawValue (rawMix) * 0.01f);
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    captureBuffer.push (buffer.getArrayOfReadPointers(), buffer.getNumChannels(), buffer.getNumSamples());
    updateGlobalTargets();

    const auto channels = std::min (buffer.getNumChannels(), 2);
    const auto totalSamples = buffer.getNumSamples();
    if (channels <= 0 || totalSamples <= 0)
        return;

    const auto packed = packedComposition.load (std::memory_order_acquire);
    const auto solo = soloSlot.load (std::memory_order_acquire);

    if (packed != previousPackedComposition)
    {
        // Entering slots are new membership minus old membership: a reorder
        // keeps every surviving effect's state, only genuinely added slots
        // reset.
        const auto membership = [] (std::uint32_t value)
        {
            std::uint32_t mask {};
            for (int chainPosition = 0; chainPosition < 8; ++chainPosition)
            {
                const auto encoded = (value >> (chainPosition * 4)) & 0x0fu;
                if (encoded == 0 || encoded > static_cast<std::uint32_t> (slots::kSlotCount))
                    break;
                mask |= (1u << (encoded - 1));
            }
            return mask;
        };
        const auto entering = membership (packed) & ~membership (previousPackedComposition);
        for (int slotIndex = 0; slotIndex < slots::kSlotCount; ++slotIndex)
            if ((entering & (1u << slotIndex)) != 0)
                effects[static_cast<size_t> (slotIndex)]->reset();
        previousPackedComposition = packed;
    }

    // dryBuffer holds one prepared-maximum chunk; larger host blocks are
    // processed in chunks so nothing writes past the preallocated storage.
    const auto chunkSize = std::max (1, currentMaxBlockSize);
    for (int offset = 0; offset < totalSamples; offset += chunkSize)
    {
        const auto count = std::min (chunkSize, totalSamples - offset);
        std::array<float*, 2> channelPointers { buffer.getWritePointer (0) + offset,
                                                channels > 1 ? buffer.getWritePointer (1) + offset
                                                             : nullptr };
        for (int channel = 0; channel < channels; ++channel)
            dryBuffer.copyFrom (channel, 0, channelPointers[static_cast<size_t> (channel)], count);

        for (int chainPosition = 0; chainPosition < 8; ++chainPosition)
        {
            const auto encoded = (packed >> (chainPosition * 4)) & 0x0fu;
            if (encoded == 0 || encoded > static_cast<std::uint32_t> (slots::kSlotCount))
                break;
            const auto slotIndex = static_cast<int> (encoded - 1);
            const auto slot = slots::slotAt (slotIndex);
            const auto& type = protocol::effectTypes[static_cast<size_t> (slot.typeIndex)];
            std::array<float, maxParametersPerType> values {};
            for (size_t parameterIndex = 0; parameterIndex < type.parameterCount; ++parameterIndex)
                values[parameterIndex] = rawValue (rawSlotValues[static_cast<size_t> (slotIndex)][parameterIndex]);
            auto& effect = effects[static_cast<size_t> (slotIndex)];
            effect->setTargets (values.data());
            if (rawValue (rawBypasses[static_cast<size_t> (slotIndex)]) >= 0.5f
                || (solo >= 0 && solo != slotIndex))
                continue;
            effect->process (channelPointers.data(), channels, count);
        }

        for (int sample = 0; sample < count; ++sample)
        {
            const auto outputGain = outputGainSmooth.getNextValue();
            const auto wetMix = wetMixSmooth.getNextValue();
            for (int channel = 0; channel < channels; ++channel)
            {
                auto* data = channelPointers[static_cast<size_t> (channel)];
                const auto dry = dryBuffer.getSample (channel, sample);
                const auto wet = data[sample] * outputGain;
                data[sample] = dry + wetMix * (wet - dry);
            }
        }
    }
}

double PluginProcessor::getTailLengthSeconds() const
{
    const auto packed = packedComposition.load (std::memory_order_acquire);
    for (int position = 0; position < 8; ++position)
    {
        const auto encoded = (packed >> (position * 4)) & 0x0fu;
        if (encoded == 0)
            break;
        const auto slot = slots::slotAt (static_cast<int> (encoded - 1));
        if (juce::String { protocol::effectTypes[static_cast<size_t> (slot.typeIndex)].name } == "reverb")
            return static_cast<double> (dsp::reverb::kNumSamples) / dsp::reverb::kSampleRate;
    }
    return 0.0;
}

slots::Composition PluginProcessor::getComposition() const
{
    return slots::unpackComposition (packedComposition.load (std::memory_order_acquire));
}

void PluginProcessor::syncCompositionFromState()
{
    const auto parsed = slots::parseComposition (parameters.state.getProperty ("chain").toString().toStdString());
    if (parsed.has_value())
        packedComposition.store (slots::packComposition (*parsed), std::memory_order_release);
    else
    {
        parameters.state.setProperty ("chain", "", nullptr);
        packedComposition.store (0, std::memory_order_release);
    }
}

void PluginProcessor::setCompositionInternal (const slots::Composition& composition, bool restartRunner)
{
    if (composition.items.size() > 8)
        return;
    for (size_t index = 0; index < composition.items.size(); ++index)
    {
        if (! slots::isValid (composition.items[index]))
            return;
        for (size_t previous = 0; previous < index; ++previous)
            if (composition.items[previous] == composition.items[index])
                return;
    }
    const auto oldPacked = packedComposition.load (std::memory_order_acquire);
    const auto packed = slots::packComposition (composition);
    parameters.state.setProperty ("chain", juce::String { slots::serializeComposition (composition) }, nullptr);
    packedComposition.store (packed, std::memory_order_release);
    // A removed slot cannot stay soloed: that would bypass everything.
    const auto solo = soloSlot.load (std::memory_order_acquire);
    if (solo >= 0
        && std::find (composition.items.begin(), composition.items.end(),
                       slots::slotAt (solo))
               == composition.items.end())
        soloSlot.store (-1, std::memory_order_release);
    if (restartRunner && oldPacked != packed && developmentRunner != nullptr)
        developmentRunner->restartImmediately();
}

void PluginProcessor::setComposition (const slots::Composition& composition)
{
    if (onMessageThread())
        setCompositionInternal (composition, true);
}

bool PluginProcessor::addSlot (int effectTypeIndex)
{
    const auto composition = getComposition();
    const auto free = slots::freeSlotFor (effectTypeIndex, composition);
    if (! free.has_value())
        return false;
    auto next = composition;
    next.items.push_back (*free);
    setComposition (next);
    return true;
}

bool PluginProcessor::removeSlot (slots::Slot slot)
{
    auto next = getComposition();
    const auto found = std::find (next.items.begin(), next.items.end(), slot);
    if (found == next.items.end())
        return false;
    next.items.erase (found);
    setComposition (next);
    return true;
}

bool PluginProcessor::moveSlot (int from, int to)
{
    auto next = getComposition();
    if (from < 0 || to < 0 || from >= static_cast<int> (next.items.size())
        || to >= static_cast<int> (next.items.size()) || from == to)
        return false;
    const auto item = next.items[static_cast<size_t> (from)];
    next.items.erase (next.items.begin() + from);
    next.items.insert (next.items.begin() + to, item);
    setComposition (next);
    return true;
}

float PluginProcessor::getSlotParameterValue (slots::Slot slot, int parameterIndex) const noexcept
{
    if (! slots::isValid (slot))
        return 0.0f;
    const auto type = protocol::effectTypes[static_cast<size_t> (slot.typeIndex)];
    if (parameterIndex < 0 || parameterIndex >= static_cast<int> (type.parameterCount))
        return 0.0f;
    return rawValue (rawSlotValues[static_cast<size_t> (slots::slotIndex (slot))]
                                      [static_cast<size_t> (parameterIndex)]);
}

juce::RangedAudioParameter* PluginProcessor::getSlotParameter (slots::Slot slot, int parameterIndex) const
{
    if (! slots::isValid (slot))
        return nullptr;
    const auto type = protocol::effectTypes[static_cast<size_t> (slot.typeIndex)];
    if (parameterIndex < 0 || parameterIndex >= static_cast<int> (type.parameterCount))
        return nullptr;
    return parameters.getParameter (parameter::slotParameterId (slot, parameterIndex));
}

void PluginProcessor::setParameterValue (juce::RangedAudioParameter* parameterValue, double value,
                                         bool notifyHost)
{
    if (parameterValue == nullptr)
        return;
    const auto& range = parameterValue->getNormalisableRange();
    const auto clamped = range.snapToLegalValue (std::clamp (static_cast<float> (value),
                                                               range.start, range.end));
    if (notifyHost)
        parameterValue->setValueNotifyingHost (parameterValue->convertTo0to1 (clamped));
    else
        parameterValue->setValue (parameterValue->convertTo0to1 (clamped));
}

void PluginProcessor::setSlotParameterValue (slots::Slot slot, int parameterIndex, double value,
                                             bool notifyHost)
{
    setParameterValue (getSlotParameter (slot, parameterIndex), value, notifyHost);
}

bool PluginProcessor::isSlotBypassed (slots::Slot slot) const noexcept
{
    if (! slots::isValid (slot))
        return false;
    return rawValue (rawBypasses[static_cast<size_t> (slots::slotIndex (slot))]) >= 0.5f;
}

void PluginProcessor::setSlotBypassed (slots::Slot slot, bool bypassed)
{
    if (! slots::isValid (slot))
        return;
    setParameterValue (parameters.getParameter (parameter::slotBypassId (slot)), bypassed ? 1.0 : 0.0, true);
}

void PluginProcessor::setSoloSlot (int slotIndex) noexcept
{
    if (slotIndex < -1 || slotIndex >= slots::kSlotCount)
        return;
    soloSlot.store (static_cast<std::int8_t> (slotIndex), std::memory_order_release);
}

void PluginProcessor::scheduleActiveAnalysisRestart() noexcept
{
    // Idle knob edits must not arm a deadline that fires on the next Shape:
    // only debounce while a run is active. The deadline is cleared when a run
    // starts, restarts, or ends (see DevelopmentAnalysisRunner).
    if (developmentRunner != nullptr
        && developmentRunner->getState() == DevelopmentAnalysisState::running)
        parameterRestartDeadline.store (juce::Time::currentTimeMillis() + 300,
                                        std::memory_order_release);
}

protocol::ExpectedChain PluginProcessor::expectedChain() const
{
    protocol::ExpectedChain result;
    for (const auto slot : getComposition().items)
        result.items.emplace_back (juce::String { slots::slotName (slot) }, slot.typeIndex);
    return result;
}

PluginProcessor::Snapshot PluginProcessor::makeSnapshot() const
{
    Snapshot result;
    result.composition = getComposition();
    for (int index = 0; index < slots::kSlotCount; ++index)
    {
        const auto type = protocol::effectTypes[static_cast<size_t> (slots::slotAt (index).typeIndex)];
        for (size_t parameterIndex = 0; parameterIndex < type.parameterCount; ++parameterIndex)
            result.values[static_cast<size_t> (index)][parameterIndex]
                = rawValue (rawSlotValues[static_cast<size_t> (index)][parameterIndex]);
        result.bypass[static_cast<size_t> (index)] = rawValue (rawBypasses[static_cast<size_t> (index)]);
    }
    return result;
}

void PluginProcessor::restoreSnapshot (const Snapshot& snapshot)
{
    if (! onMessageThread())
        return;
    setCompositionInternal (snapshot.composition, false);
    for (int index = 0; index < slots::kSlotCount; ++index)
    {
        const auto slot = slots::slotAt (index);
        const auto type = protocol::effectTypes[static_cast<size_t> (slot.typeIndex)];
        for (size_t parameterIndex = 0; parameterIndex < type.parameterCount; ++parameterIndex)
            setSlotParameterValue (slot, static_cast<int> (parameterIndex),
                                   snapshot.values[static_cast<size_t> (index)][parameterIndex], true);
        setParameterValue (parameters.getParameter (parameter::slotBypassId (slot)),
                           snapshot.bypass[static_cast<size_t> (index)], true);
    }
}

juce::Result PluginProcessor::applyAnalysisResult (const protocol::AnalysisResult& result)
{
    if (! onMessageThread())
        return juce::Result::fail ("Analysis results must be applied on the message thread.");
    const auto expected = expectedChain();
    if (result.instances.size() != expected.items.size())
        return juce::Result::fail ("Analysis result does not match the current chain.");
    for (size_t instanceIndex = 0; instanceIndex < result.instances.size(); ++instanceIndex)
    {
        const auto& instance = result.instances[instanceIndex];
        const auto& expectedItem = expected.items[instanceIndex];
        if (instance.instanceId != expectedItem.first || instance.effectTypeIndex != expectedItem.second)
            return juce::Result::fail ("Analysis result does not match the current chain.");
        const auto slot = slots::parseSlotName (instance.instanceId.toStdString());
        if (! slot.has_value() || instance.values.size() != protocol::effectTypes[static_cast<size_t> (slot->typeIndex)].parameterCount)
            return juce::Result::fail ("Analysis result contains an incomplete effect instance.");
        for (size_t parameterIndex = 0; parameterIndex < instance.values.size(); ++parameterIndex)
            setSlotParameterValue (*slot, static_cast<int> (parameterIndex), instance.values[parameterIndex], true);
    }
    return juce::Result::ok();
}

juce::Result PluginProcessor::applyAnalysisProgress (const protocol::AnalysisProgress& progressValue)
{
    protocol::AnalysisResult result { progressValue.requestId, progressValue.instances };
    if (! progressValue.hasParameters)
        return juce::Result::ok();
    return applyAnalysisResult (result);
}

juce::Result PluginProcessor::applyAssemblyResult (const protocol::AssemblyResult& result)
{
    if (! onMessageThread())
        return juce::Result::fail ("Assembly results must be applied on the message thread.");
    slots::Composition composition;
    const auto old = getComposition();
    for (const auto& instance : result.chain)
    {
        const auto slot = slots::parseSlotName (instance.instanceId.toStdString());
        if (! slot.has_value() || slot->typeIndex != instance.effectTypeIndex)
            return juce::Result::fail ("Assembly result contains an invalid effect instance.");
        composition.items.push_back (*slot);
    }
    setCompositionInternal (composition, false);
    for (const auto& instance : result.chain)
    {
        const auto slot = slots::parseSlotName (instance.instanceId.toStdString());
        if (! slot.has_value())
            continue;
        if (std::find (old.items.begin(), old.items.end(), *slot) != old.items.end())
            continue;
        for (size_t parameterIndex = 0; parameterIndex < instance.values.size(); ++parameterIndex)
            setSlotParameterValue (*slot, static_cast<int> (parameterIndex), instance.values[parameterIndex], true);
        setSlotBypassed (*slot, false);
    }
    return juce::Result::ok();
}

juce::Result PluginProcessor::applyAnalysisResultJson (const juce::String& json)
{
    if (! onMessageThread())
        return juce::Result::fail ("Analysis results must be applied on the message thread.");
    protocol::AnalysisResult result;
    if (const auto parsed = protocol::parseAnalysisResult (json, expectedChain(), result); parsed.failed())
        return parsed;
    return applyAnalysisResult (result);
}

juce::Result PluginProcessor::startDevelopmentCapture()
{
    if (developmentRunner->isBusy())
        return juce::Result::fail ("Stop the current run before capturing again.");
    if (! captureBuffer.start())
        return juce::Result::fail ("Capture is unavailable or already running.");
    developmentRunner->resetStatus();
    return juce::Result::ok();
}

void PluginProcessor::stopDevelopmentWork (bool revertToPreRun)
{
    captureBuffer.cancel();
    developmentRunner->stop (revertToPreRun);
}

juce::Result PluginProcessor::startDevelopmentShape (const juce::String& prompt, int iterations)
{
    return developmentRunner->start (true, prompt, iterations);
}

juce::Result PluginProcessor::startDevelopmentOptimization (const juce::String& prompt, int iterations)
{
    return developmentRunner->start (false, prompt, iterations);
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

void PluginProcessor::timerCallback()
{
    for (auto& effect : effects)
        if (auto* reverb = dynamic_cast<dsp::ReverbEffect*> (effect.get()); reverb != nullptr
            && reverb->impulseResponseDirty())
            reverb->rebuildImpulseResponse();

    if (developmentRunner != nullptr
        && developmentRunner->isBusy())
    {
        const auto deadline = parameterRestartDeadline.load (std::memory_order_acquire);
        if (deadline != 0 && juce::Time::currentTimeMillis() >= deadline)
        {
            parameterRestartDeadline.store (0, std::memory_order_release);
            developmentRunner->restartImmediately();
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
        {
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
            syncCompositionFromState();
        }
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
