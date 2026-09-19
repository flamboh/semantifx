#include "DistortionMath.h"
#include "dsp/DistortionEffect.h"

#include <cassert>
#include <array>
#include <cmath>
#include <iostream>

namespace
{
bool near (double actual, double expected, double tolerance = 1.0e-6)
{
    return std::abs (actual - expected) <= tolerance;
}
} // namespace

int main()
{
    using semantifx::distortion::blend;
    using semantifx::distortion::shape;

    assert (blend (-0.75f, 0.25f, 0.0f) == -0.75f);
    assert (blend (-0.75f, 0.25f, 1.0f) == 0.25f);

    for (const auto drive : { 0.0, 6.0, 18.0, 36.0, 48.0 })
        for (const auto character : { 0.0, 0.5, 1.0 })
        {
            assert (near (shape (-1.0, drive, character), -1.0));
            assert (near (shape (0.0, drive, character), 0.0));
            assert (near (shape (1.0, drive, character), 1.0));
            assert (near (shape (-0.37, drive, character), -shape (0.37, drive, character)));
        }

    assert (near (shape (0.25, 36.0, 0.0), 0.4715946557));
    assert (near (shape (0.25, 36.0, 1.0), 0.5659362049));
    assert (std::isfinite (shape (1000.0, 48.0, 1.0)));

    std::cout << "All Semantifx distortion-math tests passed\n";
    {
        semantifx::dsp::DistortionEffect effect;
        effect.prepare (48000.0, 32);
        std::array<float, 32> block;
        block.fill (1.0f);
        float* channels[] { block.data() };
        effect.process (channels, 1, static_cast<int> (block.size()));
        for (const auto sample : block)
            assert (std::isfinite (sample));
    }
}
