#pragma once

#include <cstdint>

#if defined (__i386__) || defined (__x86_64__) || defined (_M_IX86) || defined (_M_X64)
 #include <pmmintrin.h>
 #include <xmmintrin.h>
#endif

namespace semantifx::dsp
{
/** Keeps denormal handling local to standalone DSP calls without depending on JUCE. */
class DenormalGuard final
{
public:
    DenormalGuard() noexcept
    {
#if defined (__i386__) || defined (__x86_64__) || defined (_M_IX86) || defined (_M_X64)
        previousMxcsr = _mm_getcsr();
        _mm_setcsr (previousMxcsr | _MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON);
        active = true;
#elif defined (__aarch64__)
        asm volatile ("mrs %0, fpcr" : "=r" (previousFpcr));
        asm volatile ("msr fpcr, %0" :: "r" (previousFpcr | (1u << 24)));
        active = true;
#endif
    }

    ~DenormalGuard()
    {
#if defined (__i386__) || defined (__x86_64__) || defined (_M_IX86) || defined (_M_X64)
        if (active)
            _mm_setcsr (previousMxcsr);
#elif defined (__aarch64__)
        if (active)
            asm volatile ("msr fpcr, %0" :: "r" (previousFpcr));
#endif
    }

    DenormalGuard (const DenormalGuard&) = delete;
    DenormalGuard& operator= (const DenormalGuard&) = delete;

private:
#if defined (__i386__) || defined (__x86_64__) || defined (_M_IX86) || defined (_M_X64)
    std::uint32_t previousMxcsr {};
#elif defined (__aarch64__)
    std::uint64_t previousFpcr {};
#endif
    bool active {};
};
} // namespace semantifx::dsp
