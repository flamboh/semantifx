#include "PluginProcessor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{
using semantifx::PluginProcessor;
using semantifx::slots::Composition;
using semantifx::slots::Slot;

constexpr double kRate = 48000.0;
constexpr double kSineHz = 1000.0;
constexpr float kSinePeak = 0.251188643f; // -12 dBFS
constexpr float kNoiseAmp = 2.0e-4f;

int failures = 0;
int checks = 0;

void report (bool ok, const std::string& line)
{
    ++checks;
    std::cout << (ok ? "[t13-pass] " : "[t13-FAIL] ") << line << "\n" << std::flush;
    if (! ok)
        ++failures;
}

struct MessageThread : juce::Thread
{
    std::atomic<bool> ready { false };

    MessageThread() : juce::Thread ("T13 message thread") {}

    void run() override
    {
        juce::ScopedJuceInitialiser_GUI juceInitialiser;
        ready.store (true, std::memory_order_release);
        juce::MessageManager::getInstance()->runDispatchLoop();
    }
};

struct MessageCall
{
    std::function<void()> fn;

    static void* invoke (void* data)
    {
        (*static_cast<MessageCall*> (data)).fn();
        return nullptr;
    }
};

void onMessage (std::function<void()> fn)
{
    MessageCall call { std::move (fn) };
    juce::MessageManager::getInstance()->callFunctionOnMessageThread (&MessageCall::invoke, &call);
}

void pumpTimer (int milliseconds)
{
    std::this_thread::sleep_for (std::chrono::milliseconds (milliseconds));
}


struct ProcessorHandle
{
    std::unique_ptr<PluginProcessor> inner;

    ProcessorHandle (std::unique_ptr<PluginProcessor> taken) : inner (std::move (taken)) {}
    ProcessorHandle (ProcessorHandle&&) = default;
    ProcessorHandle& operator= (ProcessorHandle&&) = default;
    ProcessorHandle (const ProcessorHandle&) = delete;
    ProcessorHandle& operator= (const ProcessorHandle&) = delete;

    PluginProcessor* operator->() const { return inner.get(); }
    PluginProcessor& get() const { return *inner; }

    ~ProcessorHandle()
    {
        if (inner != nullptr)
            onMessage ([this] { inner.reset(); });
    }
};

ProcessorHandle makeProcessor()
{
    ProcessorHandle handle { nullptr };
    onMessage ([&] { handle.inner = std::make_unique<PluginProcessor>(); });
    return handle;
}

void msgSetComposition (PluginProcessor& processor, const Composition& composition)
{
    onMessage ([&] { processor.setComposition (composition); });
}

void msgPrepare (PluginProcessor& processor, double sampleRate, int maxBlock)
{
    onMessage ([&] { processor.prepareToPlay (sampleRate, maxBlock); });
}

void msgGetState (PluginProcessor& processor, juce::MemoryBlock& destination)
{
    onMessage ([&] { processor.getStateInformation (destination); });
}

void msgSetState (PluginProcessor& processor, const void* data, int size)
{
    onMessage ([&] { processor.setStateInformation (data, size); });
}

bool msgAddSlot (PluginProcessor& processor, int effectTypeIndex)
{
    bool added = false;
    onMessage ([&] { added = processor.addSlot (effectTypeIndex); });
    return added;
}

bool msgRemoveSlot (PluginProcessor& processor, Slot slot)
{
    bool removed = false;
    onMessage ([&] { removed = processor.removeSlot (slot); });
    return removed;
}

bool msgMoveSlot (PluginProcessor& processor, int from, int to)
{
    bool moved = false;
    onMessage ([&] { moved = processor.moveSlot (from, to); });
    return moved;
}

void setPhysical (PluginProcessor& processor, const juce::String& id, float physical)
{
    auto* parameter = processor.parameters.getParameter (id);
    jassert (parameter != nullptr);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (physical));
}

void setPhysical (PluginProcessor& processor, Slot slot, int parameterIndex, float physical)
{
    setPhysical (processor, semantifx::parameter::slotParameterId (slot, parameterIndex), physical);
}

void setGlobalDefaults (PluginProcessor& processor)
{
    setPhysical (processor, semantifx::parameter::outputGain, 0.0f);
    setPhysical (processor, semantifx::parameter::mix, 100.0f);
    setPhysical (processor, semantifx::parameter::bypass, 0.0f);
}

Composition standardChain()
{
    const auto parsed = semantifx::slots::parseComposition ("eq-1,distortion-1,compressor-1,reverb-1");
    jassert (parsed.has_value());
    return *parsed;
}

void configureStandardChain (PluginProcessor& processor)
{
    msgSetComposition (processor, standardChain());
    setGlobalDefaults (processor);
    setPhysical (processor, Slot { 1, 1 }, 0, 20.0f); // distortion drive_dB
    setPhysical (processor, Slot { 3, 1 }, 2, 0.4f); // reverb mix
}

struct StereoSignal
{
    std::vector<float> left, right;
};

StereoSignal makeInput (double rate, double seconds, float peak = kSinePeak, std::uint32_t seed = 0x12345678u)
{
    const auto total = static_cast<int> (std::lround (rate * seconds));
    StereoSignal signal { std::vector<float> (static_cast<size_t> (total)),
                          std::vector<float> (static_cast<size_t> (total)) };
    auto state = seed;
    const auto nextNoise = [&]
    {
        state = state * 1664525u + 1013904223u;
        const auto unit = static_cast<float> (state >> 8) / 16777216.0f; // [0,1)
        return (unit * 2.0f - 1.0f) * kNoiseAmp;
    };
    for (int sample = 0; sample < total; ++sample)
    {
        const auto sine = peak
            * std::sin (2.0f * juce::MathConstants<float>::pi * static_cast<float> (kSineHz)
                         * static_cast<float> (sample) / static_cast<float> (rate));
        signal.left[static_cast<size_t> (sample)] = sine + nextNoise();
        signal.right[static_cast<size_t> (sample)] = sine + nextNoise();
    }
    return signal;
}

double dryMaxJump (const StereoSignal& signal)
{
    double worst = 0.0;
    for (const auto* channel : { &signal.left, &signal.right })
        for (size_t index = 1; index < channel->size(); ++index)
            worst = std::max (worst,
                               static_cast<double> (std::abs ((*channel)[index] - (*channel)[index - 1])));
    return worst;
}

std::vector<int> uniformSizes (int total, int block)
{
    std::vector<int> sizes;
    for (int pos = 0; pos < total; pos += block)
        sizes.push_back (std::min (block, total - pos));
    return sizes;
}

std::vector<int> randomWalkSizes (int total, std::uint32_t seed)
{
    std::mt19937 rng (seed);
    std::vector<int> sizes;
    int pos = 0, current = 256;
    while (pos < total)
    {
        current = std::clamp (current + static_cast<int> (rng() % 81) - 40, 1, 900);
        const auto count = std::min (current, total - pos);
        sizes.push_back (count);
        pos += count;
    }
    return sizes;
}

struct RunOutput
{
    std::vector<float> left, right;
    std::vector<double> blockMicros;
};

RunOutput runBlocks (PluginProcessor& processor, const StereoSignal& input, const std::vector<int>& sizes,
                      std::function<void (int, int, juce::AudioBuffer<float>&)> beforeBlock = {})
{
    RunOutput output;
    output.left.reserve (input.left.size());
    output.right.reserve (input.right.size());
    output.blockMicros.reserve (sizes.size());
    juce::MidiBuffer midi;
    int pos = 0;
    for (int block = 0; block < static_cast<int> (sizes.size()); ++block)
    {
        const auto count = sizes[static_cast<size_t> (block)];
        juce::AudioBuffer<float> buffer (2, count);
        for (int sample = 0; sample < count; ++sample)
        {
            buffer.setSample (0, sample, input.left[static_cast<size_t> (pos + sample)]);
            buffer.setSample (1, sample, input.right[static_cast<size_t> (pos + sample)]);
        }
        if (beforeBlock)
            beforeBlock (block, pos, buffer);
        const auto start = std::chrono::steady_clock::now();
        processor.processBlock (buffer, midi);
        const auto end = std::chrono::steady_clock::now();
        output.blockMicros.push_back (
            std::chrono::duration<double, std::micro> (end - start).count());
        for (int sample = 0; sample < count; ++sample)
        {
            output.left.push_back (buffer.getSample (0, sample));
            output.right.push_back (buffer.getSample (1, sample));
        }
        pos += count;
    }
    return output;
}

bool allFinite (const RunOutput& output);

bool ensureReverbLive (PluginProcessor& processor, double rate, bool* finiteOut = nullptr)
{
    pumpTimer (600);
    const auto chunk = makeInput (rate, 0.5);
    const auto chunkSizes = uniformSizes (static_cast<int> (chunk.left.size()), 512);
    const StereoSignal probeZeros { std::vector<float> (512, 0.0f), std::vector<float> (512, 0.0f) };
    const auto probeSizes = uniformSizes (512, 512);
    bool finite = true;
    for (int attempt = 0; attempt < 40; ++attempt)
    {
        pumpTimer (50);
        finite = finite && allFinite (runBlocks (processor, chunk, chunkSizes));
        const auto probe = runBlocks (processor, probeZeros, probeSizes);
        finite = finite && allFinite (probe);
        double energy = 0.0;
        for (size_t index = 0; index < probe.left.size(); ++index)
            energy += static_cast<double> (probe.left[index]) * probe.left[index]
                    + static_cast<double> (probe.right[index]) * probe.right[index];
        if (energy > 1.0e-9)
        {
            if (finiteOut != nullptr)
                *finiteOut = finite;
            return true;
        }
    }
    if (finiteOut != nullptr)
        *finiteOut = finite;
    return false;
}

void flushZeros (PluginProcessor& processor, double rate)
{
    const auto total = static_cast<int> (2.0 * rate);
    const StereoSignal zeros { std::vector<float> (static_cast<size_t> (total), 0.0f),
                               std::vector<float> (static_cast<size_t> (total), 0.0f) };
    runBlocks (processor, zeros, uniformSizes (total, 512));
}

bool primeReverb (PluginProcessor& processor, double rate, bool* finiteOut = nullptr)
{
    bool finite = true;
    if (! ensureReverbLive (processor, rate, &finite))
    {
        if (finiteOut != nullptr)
            *finiteOut = finite;
        return false;
    }
    flushZeros (processor, rate);
    const auto chunk = makeInput (rate, 0.5);
    finite = finite && allFinite (runBlocks (processor, chunk,
                                             uniformSizes (static_cast<int> (chunk.left.size()), 512)));
    const StereoSignal probeZeros { std::vector<float> (512, 0.0f), std::vector<float> (512, 0.0f) };
    const auto probe = runBlocks (processor, probeZeros, uniformSizes (512, 512));
    finite = finite && allFinite (probe);
    double energy = 0.0;
    for (size_t index = 0; index < probe.left.size(); ++index)
        energy += static_cast<double> (probe.left[index]) * probe.left[index]
                + static_cast<double> (probe.right[index]) * probe.right[index];
    flushZeros (processor, rate);
    if (finiteOut != nullptr)
        *finiteOut = finite;
    return energy > 1.0e-9 && finite;
}

double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b, size_t from)
{
    double worst = 0.0;
    for (size_t index = from; index < a.size(); ++index)
    {
        if (! std::isfinite (a[index]) || ! std::isfinite (b[index]))
            return std::numeric_limits<double>::infinity();
        worst = std::max (worst, static_cast<double> (std::abs (a[index] - b[index])));
    }
    return worst;
}

bool allFinite (const RunOutput& output)
{
    for (const auto sample : output.left)
        if (! std::isfinite (sample))
            return false;
    for (const auto sample : output.right)
        if (! std::isfinite (sample))
            return false;
    return true;
}

double peakAbs (const RunOutput& output)
{
    double peak = 0.0;
    for (const auto sample : output.left)
        peak = std::max (peak, static_cast<double> (std::abs (sample)));
    for (const auto sample : output.right)
        peak = std::max (peak, static_cast<double> (std::abs (sample)));
    return peak;
}

double medianOf (std::vector<double> values)
{
    const auto mid = values.begin() + static_cast<long> (values.size() / 2);
    std::nth_element (values.begin(), mid, values.end());
    return *mid;
}

struct Click
{
    size_t index {};
    int channel {};
    double jump {};
};

std::vector<Click> findClicks (const std::vector<float>& left, const std::vector<float>& right,
                                double threshold, size_t from = 0)
{
    std::vector<Click> clicks;
    for (size_t index = std::max<size_t> (from, 1); index < left.size(); ++index)
    {
        const double jumpL = std::abs (static_cast<double> (left[index]) - left[index - 1]);
        const double jumpR = std::abs (static_cast<double> (right[index]) - right[index - 1]);
        if (jumpL > threshold)
            clicks.push_back ({ index, 0, jumpL });
        if (jumpR > threshold)
            clicks.push_back ({ index, 1, jumpR });
    }
    return clicks;
}

double rmsFrom (const std::vector<float>& left, const std::vector<float>& right, size_t from)
{
    double sum = 0.0;
    size_t count = 0;
    for (size_t index = from; index < left.size(); ++index)
    {
        sum += static_cast<double> (left[index]) * left[index];
        sum += static_cast<double> (right[index]) * right[index];
        count += 2;
    }
    return std::sqrt (sum / static_cast<double> (count));
}

void checkTransparent()
{
    const auto input = makeInput (kRate, 1.0);
    const auto sizes = uniformSizes (static_cast<int> (input.left.size()), 512);

    {
        auto processor = makeProcessor();
        msgSetComposition (processor.get(), {});
        setGlobalDefaults (processor.get());
        msgPrepare (processor.get(), kRate, 512);
        const auto output = runBlocks (processor.get(), input, sizes);
        const auto worst = std::max (maxAbsDiff (output.left, input.left, 0),
                                     maxAbsDiff (output.right, input.right, 0));
        report (worst <= 1.0e-6, "[t13-1] empty chain mix100 gain0: max diff " + std::to_string (worst));
    }
    {
        auto processor = makeProcessor();
        configureStandardChain (processor.get());
        setPhysical (processor.get(), semantifx::parameter::bypass, 1.0f);
        msgPrepare (processor.get(), kRate, 512);
        const auto output = runBlocks (processor.get(), input, sizes);
        const auto worst = std::max (maxAbsDiff (output.left, input.left, 0),
                                     maxAbsDiff (output.right, input.right, 0));
        report (worst <= 1.0e-6, "[t13-1] global bypass: max diff " + std::to_string (worst));
    }
    {
        auto processor = makeProcessor();
        configureStandardChain (processor.get());
        setPhysical (processor.get(), semantifx::parameter::mix, 0.0f);
        msgPrepare (processor.get(), kRate, 512);
        const auto output = runBlocks (processor.get(), input, sizes);
        const auto worst = std::max (maxAbsDiff (output.left, input.left, 0),
                                     maxAbsDiff (output.right, input.right, 0));
        report (worst <= 1.0e-6, "[t13-1] global mix 0%: max diff " + std::to_string (worst));
    }
}

void checkBlockSizes()
{
    const auto input = makeInput (kRate, 4.0);
    const int total = static_cast<int> (input.left.size());
    const size_t settle = static_cast<size_t> (0.1 * kRate); // smoothers settle

    RunOutput reference;
    bool referenceLive = false, referenceFinite = false;
    {
        auto processor = makeProcessor();
        configureStandardChain (processor.get());
        msgPrepare (processor.get(), kRate, 512);
        referenceLive = primeReverb (processor.get(), kRate, &referenceFinite);
        reference = runBlocks (processor.get(), input, uniformSizes (total, 512));
    }
    report (referenceLive && referenceFinite && allFinite (reference),
            "[t13-2] reference 512-block render: primed_live="
                + std::string (referenceLive ? "yes" : "NO") + " finite="
                + std::string (allFinite (reference) ? "yes" : "NO"));

    const std::vector<std::pair<std::string, std::vector<int>>> patterns {
        { "1", uniformSizes (total, 1) },
        { "17", uniformSizes (total, 17) },
        { "64", uniformSizes (total, 64) },
        { "480", uniformSizes (total, 480) },
        { "512-repeat", uniformSizes (total, 512) },
        { "random-walk", randomWalkSizes (total, 1234u) },
        { "1024", uniformSizes (total, 1024) },
        { "4096", uniformSizes (total, 4096) },
    };
    for (const auto& [name, sizes] : patterns)
    {
        auto processor = makeProcessor();
        configureStandardChain (processor.get());
        msgPrepare (processor.get(), kRate, 512);
        bool finite = true;
        const bool live = primeReverb (processor.get(), kRate, &finite);
        const auto output = runBlocks (processor.get(), input, sizes);
        const auto worst = std::max (maxAbsDiff (output.left, reference.left, settle),
                                     maxAbsDiff (output.right, reference.right, settle));
        report (live && finite && worst <= 1.0e-5 && allFinite (output),
                "[t13-2] block " + name + ": primed_live=" + std::string (live ? "yes" : "NO")
                    + " max diff vs 512-ref from 100ms " + std::to_string (worst));
    }
}

void checkAutomationClicks()
{
    const auto input = makeInput (kRate, 10.0);
    const int total = static_cast<int> (input.left.size());
    const auto sizes = uniformSizes (total, 512);
    const int numBlocks = static_cast<int> (sizes.size());
    const double threshold = 4.0 * dryMaxJump (input) + 0.05;

    struct AutomatedParam
    {
        std::string id;
        float minimum, maximum;
    };
    std::vector<AutomatedParam> params;
    for (const auto slot : standardChain().items)
    {
        const auto& type = semantifx::protocol::effectTypes[static_cast<size_t> (slot.typeIndex)];
        for (size_t index = 0; index < type.parameterCount; ++index)
        {
            const auto& spec = semantifx::protocol::parameters[type.firstParameter + index];
            params.push_back ({ semantifx::slots::parameterId (slot, static_cast<int> (index)),
                                spec.minimum, spec.maximum });
        }
    }
    params.push_back ({ semantifx::parameter::outputGain, -24.0f, 12.0f });
    params.push_back ({ semantifx::parameter::mix, 0.0f, 100.0f });
    params.push_back ({ semantifx::parameter::bypass, 0.0f, 1.0f });

    std::vector<float> lastValues (params.size(), 0.0f);
    std::vector<std::string> fastestPerBlock (static_cast<size_t> (numBlocks));
    std::vector<float> decayPerBlock (static_cast<size_t> (numBlocks), 0.5f);
    std::vector<float> dampingPerBlock (static_cast<size_t> (numBlocks), 0.3f);
    std::vector<char> rebuildZone (static_cast<size_t> (total), 0);
    int pumpCount = 0;

    auto processor = makeProcessor();
    configureStandardChain (processor.get());
    msgPrepare (processor.get(), kRate, 512);
    bool primedFinite = true;
    report (primeReverb (processor.get(), kRate, &primedFinite) && primedFinite,
            "[t13-3] reverb primed live+finite before automation: yes");
    auto* raw = &processor.get();

    const auto output = runBlocks (
        *raw, input, sizes,
        [&] (int block, int startSample, juce::AudioBuffer<float>&)
        {
            const double t = static_cast<double> (startSample) / kRate; // 10 s timeline
            double fastestDelta = -1.0;
            for (size_t index = 0; index < params.size(); ++index)
            {
                const auto phase = t / 10.0 + static_cast<double> (index) * 0.6180339887;
                const auto tri = 1.0 - std::abs (1.0 - 2.0 * (phase - std::floor (phase)));
                const auto value = params[index].minimum
                    + static_cast<float> (tri) * (params[index].maximum - params[index].minimum);
                setPhysical (*raw, juce::String { params[index].id }, value);
                const auto delta = std::abs (value - lastValues[index]);
                if (delta > fastestDelta)
                {
                    fastestDelta = delta;
                    fastestPerBlock[static_cast<size_t> (block)] = params[index].id;
                }
                lastValues[index] = value;
                if (params[index].id == "reverb1_decay")
                    decayPerBlock[static_cast<size_t> (block)] = value;
                if (params[index].id == "reverb1_damping")
                    dampingPerBlock[static_cast<size_t> (block)] = value;
            }
            if (block > 0 && block % 15 == 0)
            {
                pumpTimer (35);
                ++pumpCount;
                for (int fill = startSample; fill < std::min (startSample + 3 * 512, total); ++fill)
                    rebuildZone[static_cast<size_t> (fill)] = 1;
            }
        });

    report (allFinite (output), "[t13-3] automation run finite: "
                                + std::string (allFinite (output) ? "yes" : "NO"));
    const auto clicks = findClicks (output.left, output.right, threshold);
    int rebuildClicks = 0, otherClicks = 0;
    const Click* worst = nullptr;
    const Click* worstOther = nullptr;
    for (const auto& click : clicks)
    {
        if (rebuildZone[click.index] != 0)
            ++rebuildClicks;
        else
            ++otherClicks;
        if (worst == nullptr || click.jump > worst->jump)
            worst = &click;
        if (rebuildZone[click.index] == 0 && (worstOther == nullptr || click.jump > worstOther->jump))
            worstOther = &click;
    }
    auto describe = [&] (const Click* click)
    {
        if (click == nullptr)
            return std::string ("none");
        const int blockIdx = static_cast<int> (click->index / 512); // 512-sample blocks here
        return std::string ("t=") + std::to_string (click->index / kRate) + "s jump="
            + std::to_string (click->jump) + " ch=" + std::to_string (click->channel)
            + " rebuild_zone=" + (rebuildZone[click->index] != 0 ? "yes" : "no")
            + " fastest_param=" + fastestPerBlock[static_cast<size_t> (blockIdx)]
            + " decay=" + std::to_string (decayPerBlock[static_cast<size_t> (blockIdx)])
            + " damping=" + std::to_string (dampingPerBlock[static_cast<size_t> (blockIdx)]);
    };
    std::cout << "[t13-3] automation clicks: total=" << clicks.size() << " threshold=" << threshold
              << " non_rebuild=" << otherClicks << " rebuild=" << rebuildClicks
              << " pumps=" << pumpCount << "\n"
              << std::flush;
    std::cout << "[t13-3] worst click: " << describe (worst) << "\n" << std::flush;
    std::cout << "[t13-3] worst non-rebuild click: " << describe (worstOther) << "\n" << std::flush;
    report (clicks.empty(), "[t13-3] zero clicks across automation and reverb rebuilds: " + std::to_string (clicks.size()));
}

void checkChainEdits()
{
    const auto input = makeInput (kRate, 10.0);
    const int total = static_cast<int> (input.left.size());
    const auto sizes = uniformSizes (total, 512);
    const double threshold = 4.0 * dryMaxJump (input) + 0.05;
    const Composition defaultChain = standardChain();

    auto processor = makeProcessor();
    configureStandardChain (processor.get());
    msgPrepare (processor.get(), kRate, 512);
    bool primedFinite = true;
    report (primeReverb (processor.get(), kRate, &primedFinite) && primedFinite,
            "[t13-4] reverb primed live+finite before edits: yes");
    auto* raw = &processor.get();

    Composition saved = defaultChain;
    int editCount = 0;
    int nextEdit = 1;
    std::string lastOp;
    std::vector<std::pair<int, std::string>> editLog; // (startSample, op)
    bool editsApplied = true;
    Slot lastAdded { 0, 2 };
    bool soloClearedOk = true;
    bool soloSetOk = true;

    const auto output = runBlocks (
        *raw, input, sizes,
        [&] (int, int startSample, juce::AudioBuffer<float>&)
        {
            if (nextEdit > 40 || startSample < nextEdit * 12000)
                return;
            const int op = (nextEdit - 1) % 8;
            const auto before = raw->getComposition();
            const Slot slot = before.items.empty() ? Slot { 0, 1 } : before.items.back();
            const int slotIdx = semantifx::slots::slotIndex (slot);
            switch (op)
            {
                case 0:
                    editsApplied = msgAddSlot (*raw, 0) && editsApplied;
                    lastAdded = raw->getComposition().items.back();
                    lastOp = "add";
                    break;
                case 1:
                    editsApplied = msgRemoveSlot (*raw, lastAdded) && editsApplied;
                    lastOp = "remove";
                    break;
                case 2:
                {
                    const auto comp = raw->getComposition();
                    if (comp.items.size() >= 2)
                        editsApplied = msgMoveSlot (*raw, 0, static_cast<int> (comp.items.size()) - 1) && editsApplied;
                    lastOp = "reorder";
                    break;
                }
                case 3:
                    onMessage ([&] { raw->setSlotBypassed (slot, ! raw->isSlotBypassed (slot)); });
                    lastOp = "bypass-toggle";
                    break;
                case 4:
                    raw->setSoloSlot (raw->getSoloSlot() == slotIdx ? -1 : slotIdx);
                    lastOp = "solo-toggle";
                    break;
                case 5:
                {
                    auto comp = raw->getComposition();
                    if (comp.items.empty())
                    {
                        msgSetComposition (*raw, defaultChain);
                        comp = defaultChain;
                    }
                    const Slot target = comp.items[0];
                    const int targetIdx = semantifx::slots::slotIndex (target);
                    raw->setSoloSlot (targetIdx);
                    if (raw->getSoloSlot() != targetIdx)
                        soloSetOk = false;
                    editsApplied = msgRemoveSlot (*raw, target) && editsApplied;
                    if (raw->getSoloSlot() != -1)
                        soloClearedOk = false;
                    lastOp = "remove-solo-slot";
                    break;
                }
                case 6:
                    saved = raw->getComposition();
                    msgSetComposition (*raw, {});
                    lastOp = "empty";
                    break;
                default:
                    msgSetComposition (*raw, saved.items.empty() ? defaultChain : saved);
                    lastOp = "restore";
                    break;
            }
            ++editCount;
            ++nextEdit;
            editLog.emplace_back (startSample, lastOp);
        });

    report (editsApplied && editCount >= 30, "[t13-4] add/remove/reorder calls changed the chain: "
                                           + std::string (editsApplied ? "yes" : "NO"));
    report (allFinite (output), "[t13-4] chain edits finite: "
                                + std::string (allFinite (output) ? "yes" : "NO"));
    report (peakAbs (output) <= 2.0, "[t13-4] chain edits peak<=+6dBFS: " + std::to_string (peakAbs (output)));
    report (soloSetOk, "[t13-4] soloing a chained slot sticks: "
                        + std::string (soloSetOk ? "yes" : "NO"));
    report (soloClearedOk, "[t13-4] removing soloed slot clears solo: "
                           + std::string (soloClearedOk ? "yes" : "NO"));
    const auto clicks = findClicks (output.left, output.right, threshold);
    auto triggerOp = [&] (size_t index)
    {
        std::string op = "startup";
        int since = static_cast<int> (index);
        for (const auto& [pos, name] : editLog)
            if (pos <= static_cast<int> (index))
            {
                op = name;
                since = static_cast<int> (index) - pos;
            }
        return op + "+" + std::to_string (since) + "samp";
    };
    std::cout << "[t13-4] chain edits: ops=" << editCount << " last_op=" << lastOp
              << " clicks=" << clicks.size() << " threshold=" << threshold << "\n"
              << std::flush;
    for (const auto& click : clicks)
        std::cout << "[t13-4] click: t=" << click.index / kRate << "s jump=" << click.jump
                  << " ch=" << click.channel << " after_op=" << triggerOp (click.index) << "\n"
                  << std::flush;
    report (clicks.empty(), "[t13-4] zero clicks across chain edits: " + std::to_string (clicks.size()));
}

void checkReorderTail()
{
    auto processor = makeProcessor();
    const auto parsed = semantifx::slots::parseComposition ("eq-1,reverb-1");
    msgSetComposition (processor.get(), *parsed);
    setGlobalDefaults (processor.get());
    setPhysical (processor.get(), Slot { 3, 1 }, 0, 1.0f); // decay max: long tail
    setPhysical (processor.get(), Slot { 3, 1 }, 2, 1.0f); // mix full wet
    msgPrepare (processor.get(), kRate, 512);

    bool primedFinite = true;
    report (primeReverb (processor.get(), kRate, &primedFinite) && primedFinite,
            "[t13-4] reorder tail primed live+finite: yes");
    const auto loud = makeInput (kRate, 1.0);
    runBlocks (processor.get(), loud, uniformSizes (static_cast<int> (loud.left.size()), 512));

    const StereoSignal preZeros { std::vector<float> (4800, 0.0f), std::vector<float> (4800, 0.0f) };
    const auto ringing = runBlocks (processor.get(), preZeros, uniformSizes (4800, 512));
    double liveEnergy = 0.0;
    for (size_t index = 2400; index < ringing.left.size(); ++index)
        liveEnergy += static_cast<double> (ringing.left[index]) * ringing.left[index]
                    + static_cast<double> (ringing.right[index]) * ringing.right[index];
    report (liveEnergy > 1.0e-12, "[t13-4] reorder tail liveness (wet tail ringing): "
                                  + std::to_string (liveEnergy));

    const StereoSignal zeros { std::vector<float> (1536, 0.0f), std::vector<float> (1536, 0.0f) };
    const auto before = runBlocks (processor.get(), zeros, uniformSizes (1536, 512));
    msgMoveSlot (processor.get(), 0, 1);
    const auto after = runBlocks (processor.get(), zeros, uniformSizes (1536, 512));

    const auto energy = [] (const RunOutput& o)
    {
        double sum = 0.0;
        for (const auto s : o.left)
            sum += static_cast<double> (s) * s;
        for (const auto s : o.right)
            sum += static_cast<double> (s) * s;
        return sum;
    };
    const double energyBefore = energy (before), energyAfter = energy (after);
    const double db = 10.0 * std::log10 ((energyAfter + 1e-30) / (energyBefore + 1e-30));
    report (std::abs (db) <= 3.0 && energyBefore > 1e-12,
            "[t13-4] reorder tail: energy before=" + std::to_string (energyBefore)
                + " after=" + std::to_string (energyAfter) + " diff=" + std::to_string (db) + "dB");
}

struct HandoffResult
{
    int timerWindows {};
    int silentBlocks {};
    int maxSilentRun {};
    double medianMicros {};
    double maxMicros {};
    bool finite {};
};

HandoffResult runHandoff (bool reverbBypassed)
{
    const auto input = makeInput (kRate, 20.0);
    const int total = static_cast<int> (input.left.size());
    const auto sizes = uniformSizes (total, 64);

    auto processor = makeProcessor();
    configureStandardChain (processor.get());
    if (reverbBypassed)
        onMessage ([&] { processor->setSlotBypassed (Slot { 3, 1 }, true); });
    msgPrepare (processor.get(), kRate, 512);
    pumpTimer (600);
    auto* raw = &processor.get();

    int timerWindows = 0;
    const auto output = runBlocks (
        *raw, input, sizes,
        [&] (int block, int startSample, juce::AudioBuffer<float>&)
        {
            const double t = static_cast<double> (startSample) / kRate;
            const auto tri = [] (double phase)
            {
                const auto frac = phase - std::floor (phase);
                return 1.0 - std::abs (1.0 - 2.0 * frac);
            };
            setPhysical (*raw, Slot { 3, 1 }, 0, static_cast<float> (tri (t / 5.0)));
            setPhysical (*raw, Slot { 3, 1 }, 1, static_cast<float> (tri (t / 7.0 + 0.35)));
            if (block > 0 && block % 60 == 0)
            {
                pumpTimer (40);
                ++timerWindows;
            }
        });

    HandoffResult result {};
    result.timerWindows = timerWindows;
    result.finite = allFinite (output);
    int run = 0;
    {
        size_t pos = 0;
        for (const auto count : sizes)
        {
            double blockPeak = 0.0;
            for (int sample = 0; sample < count; ++sample)
            {
                blockPeak = std::max (blockPeak,
                                      static_cast<double> (std::abs (output.left[pos + static_cast<size_t> (sample)])));
                blockPeak = std::max (blockPeak,
                                      static_cast<double> (std::abs (output.right[pos + static_cast<size_t> (sample)])));
            }
            if (blockPeak < 1.0e-6)
            {
                ++result.silentBlocks;
                ++run;
                result.maxSilentRun = std::max (result.maxSilentRun, run);
            }
            else
            {
                run = 0;
            }
            pos += static_cast<size_t> (count);
        }
    }
    result.medianMicros = medianOf (output.blockMicros);
    result.maxMicros = *std::max_element (output.blockMicros.begin(), output.blockMicros.end());
    return result;
}

void checkReverbHandoff()
{
    {
        auto processor = makeProcessor();
        msgSetComposition (processor.get(), *semantifx::slots::parseComposition ("reverb-1"));
        setGlobalDefaults (processor.get());
        setPhysical (processor.get(), Slot { 3, 1 }, 2, 1.0f);
        msgPrepare (processor.get(), kRate, 512);
        bool primedFinite = true;
        report (primeReverb (processor.get(), kRate, &primedFinite) && primedFinite,
                "[t13-5] adoption proof primed live+finite: yes");
        const auto loud = makeInput (kRate, 1.0);
        runBlocks (processor.get(), loud, uniformSizes (static_cast<int> (loud.left.size()), 512));
        StereoSignal zeros { std::vector<float> (48000, 0.0f), std::vector<float> (48000, 0.0f) };
        const auto output = runBlocks (processor.get(), zeros, uniformSizes (48000, 512));
        double tailEnergy = 0.0;
        for (size_t index = 2400; index < output.left.size(); ++index)
            tailEnergy += static_cast<double> (output.left[index]) * output.left[index]
                        + static_cast<double> (output.right[index]) * output.right[index];
        report (tailEnergy > 1.0e-6,
                "[t13-5] IR adoption proof (post-prime zeros tail energy): " + std::to_string (tailEnergy));
    }

    const auto live = runHandoff (false);
    report (live.finite, "[t13-5] sweep finite: " + std::string (live.finite ? "yes" : "NO"));
    report (live.maxSilentRun <= 1,
            "[t13-5] sweep dropouts: silent_blocks=" + std::to_string (live.silentBlocks)
                + " max_run=" + std::to_string (live.maxSilentRun));
    std::cout << "[t13-5] sweep: timer_windows=" << live.timerWindows
              << " processBlock median_us=" << live.medianMicros << " max_us=" << live.maxMicros
              << " max_over_median=" << live.maxMicros / std::max (live.medianMicros, 1e-9) << "\n"
              << std::flush;

    const auto baseline = runHandoff (true);
    std::cout << "[t13-5] baseline (reverb bypassed): timer_windows=" << baseline.timerWindows
              << " processBlock median_us=" << baseline.medianMicros << " max_us=" << baseline.maxMicros
              << " max_over_median=" << baseline.maxMicros / std::max (baseline.medianMicros, 1e-9)
              << "\n"
              << std::flush;

    {
        const auto onsetInput = makeInput (kRate, 4.0);
        const auto onsetSizes = uniformSizes (static_cast<int> (onsetInput.left.size()), 512);
        auto renderOnset = [&] (bool bypassed)
        {
            auto processor = makeProcessor();
            configureStandardChain (processor.get());
            if (bypassed)
                onMessage ([&] { processor->setSlotBypassed (Slot { 3, 1 }, true); });
            msgPrepare (processor.get(), kRate, 512);
            if (! bypassed)
                pumpTimer (600);
            return runBlocks (processor.get(), onsetInput, onsetSizes);
        };
        const auto liveOut = renderOnset (false);
        const auto dryOut = renderOnset (true);
        size_t onset = liveOut.left.size();
        for (size_t index = 0; index < liveOut.left.size(); ++index)
        {
            if (std::abs (liveOut.left[index] - dryOut.left[index]) <= 1.0e-4
                && std::abs (liveOut.right[index] - dryOut.right[index]) <= 1.0e-4)
                continue;
            bool sustained = true;
            for (size_t look = index; look < std::min (index + 512, liveOut.left.size()); ++look)
                if (std::abs (liveOut.left[look] - dryOut.left[look]) <= 1.0e-4
                    && std::abs (liveOut.right[look] - dryOut.right[look]) <= 1.0e-4)
                {
                    sustained = false;
                    break;
                }
            if (sustained)
            {
                onset = index;
                break;
            }
        }
        if (onset < liveOut.left.size())
            std::cout << "[t13-5] wet onset after adoption (informational): sample " << onset << " ("
                      << onset / kRate * 1000.0 << "ms of audio)\n"
                      << std::flush;
        else
            std::cout << "[t13-5] wet onset after adoption (informational): not live within 4 s of audio\n"
                      << std::flush;
    }
}

void checkSampleRates()
{
    const double rates[3] = { 44100.0, 96000.0, 48000.0 };
    double rms[3] = {};
    bool firstFinite = true;
    auto processor = makeProcessor();
    configureStandardChain (processor.get());
    for (int step = 0; step < 3; ++step)
    {
        const double rate = rates[static_cast<size_t> (step)];
        msgPrepare (processor.get(), rate, 512);
        bool gateFinite = true;
        report (primeReverb (processor.get(), rate, &gateFinite) && gateFinite,
                "[t13-6] reverb primed live+finite at " + std::to_string (static_cast<int> (rate)) + "Hz: yes");
        firstFinite = firstFinite && gateFinite;
        const auto input = makeInput (rate, 3.0);
        const auto output = runBlocks (processor.get(), input,
                                       uniformSizes (static_cast<int> (input.left.size()), 512));
        rms[static_cast<size_t> (step)] = rmsFrom (output.left, output.right,
                                                  static_cast<size_t> (2.0 * rate));
    }
    report (firstFinite, "[t13-6] first 100ms finite at 44100/96000/48000: "
                         + std::string (firstFinite ? "yes" : "NO"));
    double worstDb = 0.0;
    for (int a = 0; a < 3; ++a)
        for (int b = a + 1; b < 3; ++b)
            worstDb = std::max (worstDb, std::abs (20.0 * std::log10 (rms[a] / rms[b])));
    report (worstDb <= 0.5, "[t13-6] steady-state RMS 44100=" + std::to_string (rms[0])
                                + " 96000=" + std::to_string (rms[1]) + " 48000=" + std::to_string (rms[2])
                                + " worst_diff=" + std::to_string (worstDb) + "dB");
}

void checkStateRoundTrip()
{
    const auto chainX = *semantifx::slots::parseComposition ("eq-1,distortion-1,compressor-1");
    const auto input3s = makeInput (kRate, 3.1);
    const auto seg = [&] (double fromSec, double toSec)
    {
        StereoSignal part;
        const auto from = static_cast<size_t> (fromSec * kRate);
        const auto to = static_cast<size_t> (toSec * kRate);
        part.left.assign (input3s.left.begin() + static_cast<ptrdiff_t> (from), input3s.left.begin() + static_cast<ptrdiff_t> (to));
        part.right.assign (input3s.right.begin() + static_cast<ptrdiff_t> (from), input3s.right.begin() + static_cast<ptrdiff_t> (to));
        return part;
    };

    auto live = makeProcessor();
    msgSetComposition (live.get(), chainX);
    setGlobalDefaults (live.get());
    setPhysical (live.get(), Slot { 1, 1 }, 0, 20.0f);
    msgPrepare (live.get(), kRate, 512);
    runBlocks (live.get(), seg (0.0, 2.0), uniformSizes (96000, 512));

    juce::MemoryBlock saved;
    msgGetState (live.get(), saved);

    for (const auto slot : chainX.items)
    {
        const auto& type = semantifx::protocol::effectTypes[static_cast<size_t> (slot.typeIndex)];
        for (size_t index = 0; index < type.parameterCount; ++index)
        {
            const auto& spec = semantifx::protocol::parameters[type.firstParameter + index];
            setPhysical (live.get(), slot, static_cast<int> (index),
                         0.5f * (spec.minimum + spec.maximum) + 0.45f * (spec.maximum - spec.minimum));
        }
    }
    setPhysical (live.get(), semantifx::parameter::outputGain, 6.0f);
    setPhysical (live.get(), semantifx::parameter::mix, 80.0f);
    const auto chainY = *semantifx::slots::parseComposition ("compressor-1,distortion-1,eq-1");
    msgSetComposition (live.get(), chainY);
    runBlocks (live.get(), seg (2.0, 2.1), uniformSizes (4800, 512));

    msgSetState (live.get(), saved.getData(), static_cast<int> (saved.getSize()));
    const auto restored = runBlocks (live.get(), seg (2.1, 3.1), uniformSizes (48000, 512));

    auto fresh = makeProcessor();
    msgSetState (fresh.get(), saved.getData(), static_cast<int> (saved.getSize()));
    msgPrepare (fresh.get(), kRate, 512);
    const auto fromFresh = runBlocks (fresh.get(), seg (2.1, 3.1), uniformSizes (48000, 512));

    const size_t settle = static_cast<size_t> (0.75 * kRate);
    const auto worst = std::max (maxAbsDiff (restored.left, fromFresh.left, settle),
                                 maxAbsDiff (restored.right, fromFresh.right, settle));
    const auto early = std::max (maxAbsDiff (restored.left, fromFresh.left, 0),
                                 maxAbsDiff (restored.right, fromFresh.right, 0));
    std::cout << "[t13-7] state round trip: max diff full first second " << early << "\n" << std::flush;
    report (worst <= 1.0e-4, "[t13-7] state round trip: max diff from 750ms " + std::to_string (worst));
}

void checkLatency()
{
    auto probe = makeProcessor();
    msgPrepare (probe.get(), kRate, 512);
    const int reported = probe->getLatencySamples();
    std::cout << "[t13-8] getLatencySamples()=" << reported << "\n" << std::flush;

    const std::vector<std::string> chains { "", "eq-1", "distortion-1", "compressor-1", "reverb-1" };
    for (const auto& name : chains)
    {
        auto processor = makeProcessor();
        Composition comp;
        if (! name.empty())
            comp = *semantifx::slots::parseComposition (name);
        msgSetComposition (processor.get(), comp);
        setGlobalDefaults (processor.get());
        msgPrepare (processor.get(), kRate, 512);
        pumpTimer (400);
        StereoSignal impulse { std::vector<float> (48000, 0.0f), std::vector<float> (48000, 0.0f) };
        impulse.left[0] = 1.0f;
        impulse.right[0] = 1.0f;
        const auto output = runBlocks (processor.get(), impulse, uniformSizes (48000, 512));
        size_t peakIndex = 0;
        float peak = 0.0f;
        for (size_t index = 0; index < output.left.size(); ++index)
        {
            const auto value = std::abs (output.left[index]) + std::abs (output.right[index]);
            if (value > peak)
            {
                peak = value;
                peakIndex = index;
            }
        }
        const std::string label = name.empty() ? "(empty)" : name;
        report (reported == static_cast<int> (peakIndex),
                "[t13-8] latency " + label + ": reported=" + std::to_string (reported)
                    + " measured=" + std::to_string (peakIndex)
                    + (reported == static_cast<int> (peakIndex) ? "" : " MISMATCH"));
    }
    {
        auto processor = makeProcessor();
        msgSetComposition (processor.get(), *semantifx::slots::parseComposition ("reverb-1"));
        setGlobalDefaults (processor.get());
        setPhysical (processor.get(), Slot { 3, 1 }, 2, 1.0f);
        msgPrepare (processor.get(), kRate, 512);
        bool finite = true;
        report (primeReverb (processor.get(), kRate, &finite) && finite,
                "[t13-8] full-wet latency probe has a live IR");
        StereoSignal impulse { std::vector<float> (48000, 0.0f), std::vector<float> (48000, 0.0f) };
        impulse.left[0] = 1.0f;
        impulse.right[0] = 1.0f;
        const auto output = runBlocks (processor.get(), impulse, uniformSizes (48000, 512));
        size_t firstWet = output.left.size();
        for (size_t index = 0; index < output.left.size(); ++index)
            if (std::abs (output.left[index]) > 1.0e-6f || std::abs (output.right[index]) > 1.0e-6f)
            {
                firstWet = index;
                break;
            }
        report (allFinite (output) && firstWet == static_cast<size_t> (processor->getLatencySamples()),
                "[t13-8] reverb full-wet first nonzero sample: " + std::to_string (firstWet));
    }
}

void checkHostileInput()
{
    for (int mode = 0; mode < 3; ++mode)
    {
        auto render = [&] (bool hostile)
        {
            auto processor = makeProcessor();
            if (mode > 0)
                configureStandardChain (processor.get());
            else
            {
                msgSetComposition (processor.get(), {});
                setGlobalDefaults (processor.get());
            }
            if (mode == 1)
                setPhysical (processor.get(), semantifx::parameter::bypass, 1.0f);
            if (mode == 2)
            {
                setPhysical (processor.get(), semantifx::parameter::mix, 50.0f);
                setPhysical (processor.get(), semantifx::parameter::outputGain, 6.0f);
            }
            msgPrepare (processor.get(), kRate, 512);
            if (mode == 2)
            {
                bool finite = true;
                report (primeReverb (processor.get(), kRate, &finite) && finite,
                        "hostile-input probe has a live reverb");
            }
            auto input = makeInput (kRate, 0.1);
            input.left[255] = hostile ? std::numeric_limits<float>::quiet_NaN() : 0.0f;
            input.left[512] = hostile ? std::numeric_limits<float>::infinity() : 0.0f;
            input.right[1025] = hostile ? -std::numeric_limits<float>::infinity() : 0.0f;
            return runBlocks (processor.get(), input, uniformSizes (static_cast<int> (input.left.size()), 512));
        };
        const auto hostile = render (true);
        const auto clean = render (false);
        const auto diff = std::max (maxAbsDiff (hostile.left, clean.left, 0),
                                   maxAbsDiff (hostile.right, clean.right, 0));
        report (allFinite (hostile) && diff <= 1.0e-6,
                "hostile-input mode=" + std::to_string (mode) + " zero-substitution diff=" + std::to_string (diff));
    }
}

void checkDenormals()
{
    auto processor = makeProcessor();
    configureStandardChain (processor.get());
    msgPrepare (processor.get(), kRate, 512);
    pumpTimer (600);

    const auto loud = makeInput (kRate, 2.0, 0.501187234f); // -6 dBFS
    const auto loudOut = runBlocks (processor.get(), loud,
                                    uniformSizes (static_cast<int> (loud.left.size()), 512));
    const double baseline = medianOf (loudOut.blockMicros);

    StereoSignal silence { std::vector<float> (480000, 0.0f), std::vector<float> (480000, 0.0f) };
    const auto silentOut = runBlocks (processor.get(), silence,
                                      uniformSizes (static_cast<int> (silence.left.size()), 512));
    const double silentMedian = medianOf (silentOut.blockMicros);
    const double silentMax = *std::max_element (silentOut.blockMicros.begin(), silentOut.blockMicros.end());
    const double ratio = silentMax / std::max (baseline, 1e-9);
    report (allFinite (silentOut), "[t13-9] silence run finite: "
                                   + std::string (allFinite (silentOut) ? "yes" : "NO"));
    report (ratio <= 3.0, "[t13-9] denormal tail: loud median_us=" + std::to_string (baseline)
                              + " silent median_us=" + std::to_string (silentMedian) + " silent max_us="
                              + std::to_string (silentMax) + " ratio=" + std::to_string (ratio));
}
} // namespace

int main()
{
    MessageThread messageThread;
    messageThread.startThread();
    while (! messageThread.ready.load (std::memory_order_acquire))
        std::this_thread::sleep_for (std::chrono::milliseconds (5));

    checkTransparent();
    checkBlockSizes();
    checkAutomationClicks();
    checkChainEdits();
    checkReorderTail();
    checkReverbHandoff();
    checkSampleRates();
    checkStateRoundTrip();
    checkLatency();
    checkHostileInput();
    checkDenormals();

    std::cout << (failures == 0 ? "T13_RESULT: PASS" : "T13_RESULT: FAIL")
              << " checks=" << checks << " failures=" << failures << "\n"
              << std::flush;

    const int exitCode = failures == 0 ? 0 : 1;
    onMessage ([&] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    messageThread.waitForThreadToExit (-1);
    return exitCode;
}
