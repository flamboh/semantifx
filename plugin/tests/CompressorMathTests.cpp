#include "dsp/CompressorMath.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
bool near (double actual, double expected, double tolerance = 1.0e-12)
{
    return std::abs (actual - expected) <= tolerance;
}
} // namespace

int main()
{
    using semantifx::dsp::compressor::attackCoefficient;
    using semantifx::dsp::compressor::staticCurveDb;

    assert (near (staticCurveDb (-30.0, -20.0, 4.0, 6.0), -30.0));
    assert (near (staticCurveDb (-20.0, -20.0, 4.0, 6.0), -20.5625));
    assert (near (staticCurveDb (-17.0, -20.0, 4.0, 6.0), -19.25));
    assert (near (staticCurveDb (-10.0, -20.0, 4.0, 6.0), -17.5));
    assert (near (staticCurveDb (-23.0, -20.0, 4.0, 6.0), -23.0));
    assert (near (staticCurveDb (-17.0, -20.0, 4.0, 6.0), -19.25));

    assert (near (attackCoefficient (44100.0, 20.0), 0.9975119160965218, 1.0e-15));
    assert (near (attackCoefficient (48000.0, 5.0), 0.9908866779461135, 1.0e-15));

    std::cout << "All Semantifx compressor-math tests passed\n";
}
