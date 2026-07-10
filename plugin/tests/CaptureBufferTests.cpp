#include "CaptureBuffer.h"

#include <array>
#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    semantifx::CaptureBuffer capture;
    capture.prepare (10.0, 2, 1.0);
    assert (capture.getState() == semantifx::CaptureBuffer::State::idle);
    assert (capture.start());
    assert (! capture.start());

    const std::array<float, 6> left { 1, 2, 3, 4, 5, 6 };
    const std::array<float, 6> right { -1, -2, -3, -4, -5, -6 };
    const float* firstBlock[] { left.data(), right.data() };
    capture.push (firstBlock, 2, 6);
    assert (capture.getState() == semantifx::CaptureBuffer::State::capturing);
    assert (std::abs (capture.getProgress() - 0.6f) < 0.001f);

    const std::array<float, 6> tail { 7, 8, 9, 10, 11, 12 };
    const float* secondBlock[] { tail.data() };
    capture.push (secondBlock, 1, 6);
    assert (capture.getState() == semantifx::CaptureBuffer::State::ready);
    assert (capture.getProgress() == 1.0f);
    assert (capture.getReadPointer (0)[9] == 10.0f);
    assert (capture.getReadPointer (1)[9] == 10.0f); // Mono input is duplicated.

    assert (capture.start());
    capture.cancel();
    assert (capture.getState() == semantifx::CaptureBuffer::State::idle);
    assert (capture.getProgress() == 0.0f);

    std::cout << "All Semantifx capture-buffer tests passed\n";
}
