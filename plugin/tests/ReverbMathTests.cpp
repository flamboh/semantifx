#include "dsp/ReverbMath.h"

#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
bool near (double actual, double expected, double tolerance = 1.0e-6)
{
    return std::abs (actual - expected) <= tolerance;
}
} // namespace

int main()
{
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
    assert (near (impulse[0][0], 6.5));
    assert (near (impulse[1][0], 13.0));

    std::cout << "All Semantifx reverb-math tests passed\n";
}
