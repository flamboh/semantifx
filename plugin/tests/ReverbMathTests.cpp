#include "dsp/ReverbMath.h"
#include "dsp/ReverbEffect.h"

#include <array>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <memory>
#include <vector>

namespace
{
bool near (double actual, double expected, double tolerance = 1.0e-6)
{
    return std::abs (actual - expected) <= tolerance;
}

class CapturingConvolver final : public semantifx::dsp::Convolver
{
public:
    void prepare (double, int, int) override {}

    void setImpulseResponse (const float* const* channels, int length) override
    {
        ++setCalls;
        for (int channel = 0; channel < semantifx::dsp::reverb::kNumChannels; ++channel)
        {
            energies[static_cast<size_t> (channel)] = 0.0;
            for (int sample = 0; sample < length; ++sample)
            {
                const auto value = static_cast<double> (channels[channel][sample]);
                energies[static_cast<size_t> (channel)] += value * value;
            }
        }
    }

    void reset() override {}
    void process (float* const*, int, int) override {}

    std::array<double, semantifx::dsp::reverb::kNumChannels> energies {};
    int setCalls {};
};

double captureEnergy (double sampleRate, const char* noisePath)
{
    auto convolver = std::make_unique<CapturingConvolver>();
    auto* capture = convolver.get();
    semantifx::dsp::ReverbEffect effect (std::move (convolver), noisePath);
    effect.prepare (sampleRate, 64);
    effect.rebuildImpulseResponse();
    return capture->energies[0];
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: semantifx_reverb_tests NOISE_TABLE\n";
        return 2;
    }
    using namespace semantifx::dsp::reverb;

    const auto defaults = expandMacros (0.5, 0.3);
    assert (near (defaults.bandGains[0], 1.0));
    assert (near (defaults.bandGains[11], 0.7));
    assert (near (defaults.bandDecays[0], 0.5));
    assert (near (defaults.bandDecays[11], 0.8));

    const auto minimum = expandMacros (0.0, 0.0);
    const auto maximum = expandMacros (1.0, 1.0);
    assert (near (minimum.bandGains[11], 1.0));
    assert (near (minimum.bandDecays[0], 1.0));
    assert (near (maximum.bandGains[0], 1.0));
    assert (near (maximum.bandGains[11], 0.0));
    assert (near (maximum.bandDecays[0], 0.0));
    assert (near (maximum.bandDecays[11], 1.0));

    std::array<std::vector<float>, 2> noise;
    for (auto& channel : noise)
        channel.assign (static_cast<size_t> (kNumBands * kNumSamples), 0.0f);
    for (int band = 0; band < kNumBands; ++band)
    {
        noise[0][static_cast<size_t> (band) * kNumSamples] = static_cast<float> (band + 1);
        noise[1][static_cast<size_t> (band) * kNumSamples] = static_cast<float> (2 * (band + 1));
    }

    const float* noisePointers[] { noise[0].data(), noise[1].data() };
    std::array<std::vector<float>, 2> impulse;
    buildImpulseResponse (noisePointers, defaults, impulse);
    assert (impulse[0].size() == static_cast<size_t> (kNumSamples));
    assert (impulse[1].size() == static_cast<size_t> (kNumSamples));
    double expectedChannel0 = 0.0;
    for (int band = 0; band < kNumBands; ++band)
        expectedChannel0 += static_cast<double> (band + 1)
                          * defaults.bandGains[static_cast<size_t> (band)]
                          / static_cast<double> (kNumBands);
    assert (near (impulse[0][0], expectedChannel0));
    assert (near (impulse[1][0], 2.0 * expectedChannel0));

    {
        // T16 pink-weighted spectral normalization scale, evaluated on IRs
        // built from the real frozen noise table.
        std::ifstream noiseInput (argv[1], std::ios::binary);
        assert (static_cast<bool> (noiseInput));
        constexpr auto valuesPerChannel = static_cast<size_t> (kNumBands)
                                        * static_cast<size_t> (kNumSamples);
        std::array<std::vector<float>, 2> table;
        for (auto& channel : table)
        {
            channel.resize (valuesPerChannel);
            noiseInput.read (reinterpret_cast<char*> (channel.data()),
                             static_cast<std::streamsize> (channel.size() * sizeof (float)));
            assert (static_cast<bool> (noiseInput));
        }
        const float* tablePointers[] { table[0].data(), table[1].data() };

        auto scaleAt = [&tablePointers] (double decay, double damping)
        {
            const auto expanded = expandMacros (decay, damping);
            std::array<std::vector<float>, 2> built;
            buildImpulseResponse (tablePointers, expanded, built);
            return pinkNormalizationScale (built);
        };

        const auto defaultsScale = scaleAt (0.5, 0.3);
        assert (std::isfinite (defaultsScale));
        assert (defaultsScale > 0.0 && defaultsScale < 1.0);
        // Approximately -17.6 dB at defaults (the T15 prediction): +/-0.5 dB
        // pins the normalization while allowing the measured K=64-vs-128 grid
        // disagreement (0.15 dB), float32 IR rounding, and libm differences.
        const auto defaultsDb = 20.0 * std::log10 (defaultsScale);
        assert (std::abs (defaultsDb - -17.6) <= 0.5);

        // Longer decays carry more IR energy, so s decreases monotonically.
        for (const auto damping : { 0.0, 0.5, 1.0 })
        {
            auto previous = scaleAt (0.0, damping);
            assert (std::isfinite (previous));
            for (const auto decay : { 0.25, 0.5, 0.75, 1.0 })
            {
                const auto current = scaleAt (decay, damping);
                assert (std::isfinite (current));
                assert (current < previous);
                previous = current;
            }
        }

        // A silent IR normalizes to the 1.0 no-op.
        std::array<std::vector<float>, 2> silence;
        for (auto& channel : silence)
            channel.assign (static_cast<size_t> (kNumSamples), 0.0f);
        assert (near (pinkNormalizationScale (silence), 1.0));

        std::cout << std::setprecision (12)
                  << "Reverb normalization scale at defaults: " << defaultsScale
                  << " (" << defaultsDb << " dB)\n";
    }

    std::cout << "All Semantifx reverb-math tests passed\n";
    {
        semantifx::dsp::ReverbEffect effect (argv[1]);
        effect.prepare (48000.0, 32);
        effect.rebuildImpulseResponse();
        std::array<float, 32> block;
        block.fill (1.0f);
        float* channels[] { block.data() };
        effect.process (channels, 1, static_cast<int> (block.size()));
        for (const auto sample : block)
            assert (std::isfinite (sample));
    }

    {
        semantifx::dsp::DirectConvolver convolver;
        convolver.prepare (44100.0, 32, 1);
        const std::array<float, 2> firstImpulse { 1.0f, 0.0f };
        const std::array<float, 2> replacementImpulse { 0.0f, 1.0f };
        const float* firstPointers[] { firstImpulse.data(), firstImpulse.data() };
        const float* replacementPointers[] { replacementImpulse.data(), replacementImpulse.data() };
        convolver.setImpulseResponse (firstPointers, 2);
        float firstSample = 1.0f;
        float* channels[] { &firstSample };
        convolver.process (channels, 1, 1);
        assert (near (firstSample, 1.0));
        convolver.setImpulseResponse (replacementPointers, 2);
        float tailSample = 0.0f;
        channels[0] = &tailSample;
        convolver.process (channels, 1, 1);
        assert (near (tailSample, 1.0));
    }

    {
        auto convolver = std::make_unique<CapturingConvolver>();
        auto* capture = convolver.get();
        semantifx::dsp::ReverbEffect effect (std::move (convolver), argv[1]);
        effect.prepare (44100.0, 32);
        effect.rebuildImpulseResponse();
        assert (capture->setCalls == 1);
        const float unchanged[] { 0.5f, 0.3f, 0.3f };
        effect.setTargets (unchanged);
        assert (! effect.impulseResponseDirty());
        effect.rebuildImpulseResponse();
        assert (capture->setCalls == 1);
        const float changed[] { 0.6f, 0.3f, 0.3f };
        effect.setTargets (changed);
        assert (effect.impulseResponseDirty());
        effect.rebuildImpulseResponse();
        assert (capture->setCalls == 2);
    }
    const auto baseEnergy = captureEnergy (44100.0, argv[1]);
    const auto energy48k = captureEnergy (48000.0, argv[1]);
    const auto energy96k = captureEnergy (96000.0, argv[1]);
    const auto energyTolerance = 0.01;
    assert (std::abs (energy48k - baseEnergy * 44100.0 / 48000.0)
            <= energyTolerance * baseEnergy * 44100.0 / 48000.0);
    assert (std::abs (energy96k - baseEnergy * 44100.0 / 96000.0)
            <= energyTolerance * baseEnergy * 44100.0 / 96000.0);
    const auto expected48k = baseEnergy * 44100.0 / 48000.0;
    const auto expected96k = baseEnergy * 44100.0 / 96000.0;
    std::cout << std::setprecision (12)
              << "Reverb IR energies: 44100=" << baseEnergy
              << " 48000=" << energy48k
              << " 96000=" << energy96k
              << " expected48k=" << expected48k
              << " expected96k=" << expected96k << "\n";
}
