#include "dsp/EffectFactory.h"
#include "generated/EffectParameters.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
 #include <pmmintrin.h>
 #include <xmmintrin.h>
#endif

namespace
{
const char* gNoisePath = nullptr;
int gFailures = 0;
int gChecks = 0;

void report (const std::string& check, const std::string& effect, double sampleRate,
             int channels, const std::string& measured, bool passed)
{
    ++gChecks;
    if (! passed)
        ++gFailures;
    std::printf ("[%-22s] effect=%-10s sr=%5.0f ch=%d %s %s\n",
                 check.c_str(), effect.c_str(), sampleRate, channels,
                 measured.c_str(), passed ? "PASS" : "FAIL");
    std::fflush (stdout);
}

char scratch[1024];

struct ParamSets
{
    std::vector<float> defaults, minimum, maximum, mid;
};

ParamSets paramsFor (const std::string& type)
{
    using namespace semantifx::protocol;
    ParamSets sets;
    for (const auto& entry : effectTypes)
    {
        if (entry.name != type)
            continue;
        for (size_t index = 0; index < entry.parameterCount; ++index)
        {
            const auto& spec = parameters[entry.firstParameter + index];
            sets.defaults.push_back (spec.defaultValue);
            sets.minimum.push_back (spec.minimum);
            sets.maximum.push_back (spec.maximum);
            sets.mid.push_back (0.5f * (spec.minimum + spec.maximum));
        }
        return sets;
    }
    throw std::runtime_error ("unknown effect type: " + type);
}

std::unique_ptr<semantifx::dsp::Effect> fresh (const std::string& type, double sampleRate,
                                               int maxBlock, const std::vector<float>& params)
{
    semantifx::dsp::ReverbResources resources { gNoisePath };
    auto effect = semantifx::dsp::makeEffect (type, resources);
    effect->prepare (sampleRate, maxBlock);
    effect->setTargets (params.data());
    effect->snapSmoothers();
    if (auto* reverb = dynamic_cast<semantifx::dsp::ReverbEffect*> (effect.get()))
        reverb->rebuildImpulseResponse();
    return effect;
}

float noiseSample (std::uint64_t& state)
{
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    auto bits = (state >> 33) & 0x7fffffffull;
    return static_cast<float> (bits) / 1073741824.0f - 1.0f;
}

std::vector<float> testSignal (double sampleRate, double seconds = 2.0)
{
    const auto count = static_cast<size_t> (std::llround (sampleRate * seconds));
    std::vector<float> signal (count);
    std::uint64_t state = 0x243F6A8885A308D3ull;
    for (size_t index = 0; index < count; ++index)
    {
        const auto time = static_cast<double> (index) / sampleRate;
        const auto envelope = index < count / 4 ? 1.0 : 0.15;
        signal[index] = static_cast<float> (
            envelope * (0.4 * std::sin (2.0 * M_PI * 110.0 * time)
                      + 0.3 * std::sin (2.0 * M_PI * 440.0 * time)
                      + 0.1 * std::sin (2.0 * M_PI * 3000.0 * time)
                      + 0.05 * noiseSample (state)));
    }
    return signal;
}

std::vector<float> sineSignal (double sampleRate, double frequency, double peak,
                               double seconds = 2.0)
{
    const auto count = static_cast<size_t> (std::llround (sampleRate * seconds));
    std::vector<float> signal (count);
    for (size_t index = 0; index < count; ++index)
        signal[index] = static_cast<float> (
            peak * std::sin (2.0 * M_PI * frequency * index / sampleRate));
    return signal;
}

using Stereo = std::vector<std::vector<float>>; // [channel][sample]

Stereo processSweep (semantifx::dsp::Effect& effect, const std::vector<float>& mono,
                     int channels, const std::vector<int>& pattern)
{
    Stereo buffers ((size_t) channels, mono);
    std::array<float*, 2> pointers { buffers[0].data(),
                                     channels > 1 ? buffers[1].data() : nullptr };
    const auto total = mono.size();
    size_t offset = 0, step = 0;
    while (offset < total)
    {
        const auto block = std::min<size_t> (
            static_cast<size_t> (pattern[step++ % pattern.size()]), total - offset);
        std::array<float*, 2> chunk { pointers[0] + offset,
                                      pointers[1] != nullptr ? pointers[1] + offset : nullptr };
        effect.process (chunk.data(), channels, static_cast<int> (block));
        offset += block;
    }
    return buffers;
}

double rms (const std::vector<float>& signal, size_t skip = 0)
{
    double energy = 0.0;
    size_t count = 0;
    for (size_t index = skip; index < signal.size(); ++index)
    {
        energy += static_cast<double> (signal[index]) * signal[index];
        ++count;
    }
    return count == 0 ? 0.0 : std::sqrt (energy / static_cast<double> (count));
}

double peakAbs (const std::vector<float>& signal)
{
    double peak = 0.0;
    for (auto sample : signal)
        peak = std::max (peak, static_cast<double> (std::abs (sample)));
    return peak;
}

double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    double worst = 0.0;
    for (size_t index = 0; index < a.size() && index < b.size(); ++index)
    {
        const auto x = static_cast<double> (a[index]);
        const auto y = static_cast<double> (b[index]);
        if (! std::isfinite (x) || ! std::isfinite (y))
            return std::numeric_limits<double>::infinity();
        worst = std::max (worst, std::abs (x - y));
    }
    return worst;
}

bool allFinite (const std::vector<float>& signal)
{
    for (auto sample : signal)
        if (! std::isfinite (sample))
            return false;
    return true;
}

double toDb (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-30)); }

std::vector<double> harmonicMags (const std::vector<float>& signal, size_t start,
                                  double sampleRate, double f0, int harmonics = 8)
{
    const auto count = signal.size() - start;
    std::vector<double> mags ((size_t) harmonics + 1, 0.0);
    for (int k = 1; k <= harmonics; ++k)
    {
        double re = 0.0, im = 0.0;
        for (size_t n = 0; n < count; ++n)
        {
            const auto phase = 2.0 * M_PI * f0 * k * (start + n) / sampleRate;
            re += signal[start + n] * std::cos (phase);
            im += signal[start + n] * std::sin (phase);
        }
        mags[(size_t) k] = 2.0 * std::sqrt (re * re + im * im) / count;
    }
    return mags;
}

double rt60 (const std::vector<float>& ir, double sampleRate)
{
    const auto n = ir.size();
    std::vector<double> cumulative (n + 1, 0.0);
    for (size_t index = n; index-- > 0;)
        cumulative[index] = cumulative[index + 1] + ir[index] * ir[index];
    const auto total = cumulative[0];
    if (! (total > 0.0))
        return std::numeric_limits<double>::quiet_NaN();
    double sumT = 0.0, sumL = 0.0, sumTT = 0.0, sumTL = 0.0;
    size_t points = 0;
    double floor = 10.0 * std::log10 (cumulative[n - 1] / total);
    const auto low = std::max (-35.0, floor);
    for (size_t index = 0; index < n; ++index)
    {
        const auto level = 10.0 * std::log10 (cumulative[index] / total);
        if (level > -5.0 || level < low)
            continue;
        const auto t = index / sampleRate;
        sumT += t; sumL += level; sumTT += t * t; sumTL += t * level;
        ++points;
    }
    if (points < 16)
        return std::numeric_limits<double>::quiet_NaN();
    const auto denom = points * sumTT - sumT * sumT;
    if (denom == 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    const auto slope = (points * sumTL - sumT * sumL) / denom; // dB per second
    if (slope >= 0.0)
        return std::numeric_limits<double>::quiet_NaN();
    return -60.0 / slope;
}

std::vector<float> captureIr (const std::string& type, double sampleRate,
                              const std::vector<float>& params, double seconds = 2.0)
{
    auto effect = fresh (type, sampleRate, 512, params);
    const auto count = static_cast<size_t> (std::llround (sampleRate * seconds));
    std::vector<float> signal (count, 0.0f);
    signal[0] = 1.0f;
    size_t offset = 0;
    while (offset < count)
    {
        const auto block = std::min<size_t> (512, count - offset);
        std::array<float*, 2> pointers { signal.data() + offset, nullptr };
        effect->process (pointers.data(), 1, static_cast<int> (block));
        offset += block;
    }
    return signal;
}

double goertzel (const std::vector<float>& signal, size_t start, size_t end,
                 double sampleRate, double frequency)
{
    const auto w = 2.0 * M_PI * frequency / sampleRate;
    const auto c = 2.0 * std::cos (w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (size_t index = start; index < end; ++index)
    {
        s0 = signal[index] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const auto power = s1 * s1 + s2 * s2 - c * s1 * s2;
    return std::sqrt (std::max (0.0, power)) / (end - start);
}

void checkBlockSize (const std::string& type, double sampleRate, int channels)
{
    const auto params = paramsFor (type).defaults;
    const auto input = testSignal (sampleRate);
    const auto reference = processSweep (
        *fresh (type, sampleRate, 2048, params), input, channels, { 512 });
    double worst = 0.0;
    std::string worstName;
    std::vector<int> walk;
    {
        std::uint64_t state = 0x9E3779B97F4A7C15ull;
        for (int i = 0; i < 64; ++i)
        {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            walk.push_back (1 + (int) ((state >> 33) % 2048));
        }
    }
    const std::vector<std::pair<std::string, std::vector<int>>> variants {
        { "b1", { 1 } }, { "b7", { 7 } }, { "b64", { 64 } }, { "b2048", { 2048 } },
        { "walk", walk },
    };
    for (const auto& [name, pattern] : variants)
    {
        const auto candidate = processSweep (
            *fresh (type, sampleRate, 2048, params), input, channels, pattern);
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto diff = maxAbsDiff (candidate[(size_t) ch], reference[(size_t) ch]);
            if (diff > worst) { worst = diff; worstName = name; }
        }
    }
    {
        const auto candidate = processSweep (
            *fresh (type, sampleRate, 64, params), input, channels, { 64 });
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto diff = maxAbsDiff (candidate[(size_t) ch], reference[(size_t) ch]);
            if (diff > worst) { worst = diff; worstName = "max64"; }
        }
    }
    std::snprintf (scratch, sizeof scratch,
                   "worst=%s maxAbsDiff=%.3e tol=1e-05", worstName.c_str(), worst);
    report ("blocksize", type, sampleRate, channels, scratch, worst < 1.0e-5);
}

double expectedEqDb (const std::vector<float>& params, double sampleRate, double frequency)
{
    double result = 0.0;
    for (size_t band = 0; band < 6; ++band)
    {
        const auto offset = band * 3;
        const auto a = std::pow (10.0, params[offset] / 40.0);
        const auto u = std::tan (M_PI * frequency / sampleRate)
                     / std::tan (M_PI * params[offset + 1] / sampleRate);
        const auto damping = u / params[offset + 2];
        double power = 1.0;
        if (band == 0)
            power = a * a * (std::pow (a - u * u, 2) + a * damping * damping)
                    / (std::pow (1.0 - a * u * u, 2) + a * damping * damping);
        else if (band == 5)
            power = a * a * (std::pow (1.0 - a * u * u, 2) + a * damping * damping)
                    / (std::pow (a - u * u, 2) + a * damping * damping);
        else
            power = (std::pow (1.0 - u * u, 2) + std::pow (a * damping, 2))
                    / (std::pow (1.0 - u * u, 2) + std::pow (damping / a, 2));
        result += 10.0 * std::log10 (power);
    }
    return result;
}

void checkSampleRate (const std::string& type)
{
    const auto sets = paramsFor (type);
    const std::vector<std::pair<std::string, std::vector<float>>> cases {
        { "defaults", sets.defaults }, { "minimum", sets.minimum },
        { "maximum", sets.maximum }, { "mid", sets.mid },
    };
    for (const auto& [name, params] : cases)
    {
        if (type == "reverb")
        {
            auto wet = params;
            wet[2] = 1.0f;
            const auto ir44 = captureIr (type, 44100.0, wet);
            const auto ir48 = captureIr (type, 48000.0, wet);
            const auto t44 = rt60 (ir44, 44100.0);
            const auto t48 = rt60 (ir48, 48000.0);
            const auto rel = std::abs (t48 - t44) / t44;
            std::snprintf (scratch, sizeof scratch,
                           "set=%s rt60_44100=%.4f rt60_48000=%.4f relDiff=%.4f tol=0.10",
                           name.c_str(), t44, t48, rel);
            report ("samplerate", type, 48000, 1, scratch,
                    std::isfinite (t44) && std::isfinite (t48) && rel <= 0.10);
            continue;
        }
        double worstDb = 0.0, responseError = 0.0;
        for (int channels : { 1, 2 })
        {
            const auto in44 = sineSignal (44100.0, 1000.0, 0.5);
            const auto in48 = sineSignal (48000.0, 1000.0, 0.5);
            const auto out44 = processSweep (
                *fresh (type, 44100.0, 512, params), in44, channels, { 512 });
            const auto out48 = processSweep (
                *fresh (type, 48000.0, 512, params), in48, channels, { 512 });
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto r44 = rms (out44[(size_t) ch], 44100);
                const auto r48 = rms (out48[(size_t) ch], 48000);
                worstDb = std::max (worstDb, std::abs (toDb (r48 / r44)));
                if (type == "eq")
                    responseError = std::max ({ responseError,
                        std::abs (toDb (r44 / rms (in44, 44100)) - expectedEqDb (params, 44100.0, 1000.0)),
                        std::abs (toDb (r48 / rms (in48, 48000)) - expectedEqDb (params, 48000.0, 1000.0)) });
            }
        }
        if (type == "eq")
        {
            std::snprintf (scratch, sizeof scratch,
                           "set=%s rawRmsDiff=%.4f dB bilinearResponseError=%.4f dB tol=0.01dB",
                           name.c_str(), worstDb, responseError);
            report ("samplerate", type, 48000, 2, scratch, responseError <= 0.01);
            continue;
        }
        std::snprintf (scratch, sizeof scratch,
                       "set=%s worstRmsDiff=%.4f dB tol=0.5dB", name.c_str(), worstDb);
        report ("samplerate", type, 48000, 2, scratch, worstDb <= 0.5);
    }
}

void checkSilence (const std::string& type, double sampleRate, int channels)
{
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    const auto savedCsr = _mm_getcsr();
    _mm_setcsr (savedCsr
                & ~static_cast<unsigned> (_MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON));
    const auto cleared = (_mm_getcsr()
                          & static_cast<unsigned> (_MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON))
                       == 0;
#elif defined(__aarch64__)
    std::uint64_t savedFpcr {};
    asm volatile ("mrs %0, fpcr" : "=r" (savedFpcr));
    const auto gradualFpcr = savedFpcr & ~(std::uint64_t { 1 } << 24);
    asm volatile ("msr fpcr, %0" :: "r" (gradualFpcr));
    const bool cleared = true;
#else
    const bool cleared = true;
#endif
    const auto params = paramsFor (type).defaults;
    const auto total = static_cast<size_t> (std::llround (sampleRate * 5.0));
    std::vector<float> silence (total, 0.0f);
    auto effect = fresh (type, sampleRate, 512, params);
    auto t0 = std::chrono::steady_clock::now();
    const auto out = processSweep (*effect, silence, channels, { 512 });
    auto t1 = std::chrono::steady_clock::now();
    const auto loud = testSignal (sampleRate);
    auto effectLoud = fresh (type, sampleRate, 512, params);
    auto t2 = std::chrono::steady_clock::now();
    (void) processSweep (*effectLoud, loud, channels, { 512 });
    auto t3 = std::chrono::steady_clock::now();
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    _mm_setcsr (savedCsr);
#elif defined(__aarch64__)
    asm volatile ("msr fpcr, %0" :: "r" (savedFpcr));
#endif
    const auto skip = static_cast<size_t> (std::llround (sampleRate * 0.1));
    bool finite = true;
    double worst = 0.0;
    for (int ch = 0; ch < channels; ++ch)
    {
        finite = finite && allFinite (out[(size_t) ch]);
        for (size_t i = skip; i < out[(size_t) ch].size(); ++i)
            worst = std::max (worst, (double) std::abs (out[(size_t) ch][i]));
    }
    const auto silentPerBlock = std::chrono::duration<double> (t1 - t0).count()
                              / (total / 512.0);
    const auto loudPerBlock = std::chrono::duration<double> (t3 - t2).count()
                            / (loud.size() / 512.0);
    const auto ratio = silentPerBlock / std::max (loudPerBlock, 1e-12);
    std::snprintf (scratch, sizeof scratch,
                   "ftzCleared=%d maxTailAbs=%.3e tol=1e-09 timeRatio=%.3f tol=3.0",
                   (int) cleared, worst, ratio);
    report ("silence", type, sampleRate, channels, scratch,
            cleared && finite && worst < 1.0e-9 && ratio < 3.0);
}

void spikesRef (std::vector<float>& buffer, size_t index)
{
    buffer[index] = (index % 2 == 0) ? 1.0f : -1.0f;
}

void checkHostile (const std::string& type, double sampleRate, int channels)
{
    const auto params = paramsFor (type).defaults;
    const auto resumeSeconds = 0.5;
    const auto in = sineSignal (sampleRate, 440.0, 0.5, resumeSeconds);

    auto runCase = [&] (const std::string& name, std::vector<float> hostile)
    {
        auto effect = fresh (type, sampleRate, 2048, params);
        auto outHostile = processSweep (*effect, hostile, channels, { 512 });
        auto outResume = processSweep (*effect, in, channels, { 512 });
        bool finite = true;
        double peak = 0.0;
        for (int ch = 0; ch < channels; ++ch)
        {
            finite = finite && allFinite (outHostile[(size_t) ch])
                   && allFinite (outResume[(size_t) ch]);
            peak = std::max ({ peak, peakAbs (outHostile[(size_t) ch]),
                               peakAbs (outResume[(size_t) ch]) });
        }
        std::snprintf (scratch, sizeof scratch,
                       "case=%s finite=%d peak=%.3e", name.c_str(), (int) finite, peak);
        report ("hostile", type, sampleRate, channels, scratch, finite);
    };

    { // +/-1e6 spikes, alternating every sample.
        const auto n = static_cast<size_t> (std::llround (sampleRate * 0.5));
        std::vector<float> spikes (n);
        for (size_t i = 0; i < n; ++i)
            spikes[i] = (i % 2 == 0) ? 1.0e6f : -1.0e6f;
        runCase ("spikes", std::move (spikes));
    }
    { // Full-scale DC block.
        runCase ("dc", std::vector<float> (
            static_cast<size_t> (std::llround (sampleRate * 0.5)), 1.0f));
    }
    { // Full-scale Nyquist square wave.
        const auto n = static_cast<size_t> (std::llround (sampleRate * 0.5));
        std::vector<float> nyquist (n);
        for (size_t i = 0; i < n; ++i)
            spikesRef (nyquist, i);
        runCase ("nyquist", std::move (nyquist));
    }

    for (float bad : { std::numeric_limits<float>::quiet_NaN(),
                       std::numeric_limits<float>::infinity(),
                       -std::numeric_limits<float>::infinity() })
    {
        const auto name = std::isnan (bad) ? "nan" : bad > 0.0f ? "+inf" : "-inf";
        auto effect = fresh (type, sampleRate, 2048, params);
        std::vector<float> block = sineSignal (sampleRate, 440.0, 0.5, 512.0 / sampleRate);
        block[256] = bad;
        auto outBlock = processSweep (*effect, block, channels, { 512 });
        bool leakedPastBlock = false;
        for (int ch = 0; ch < channels; ++ch)
            for (size_t i = 257; i < outBlock[(size_t) ch].size(); ++i)
                leakedPastBlock = leakedPastBlock || ! std::isfinite (outBlock[(size_t) ch][i]);
        auto outResume = processSweep (*effect, in, channels, { 512 });
        bool recovered = true;
        for (int ch = 0; ch < channels; ++ch)
            for (size_t i = 0; i < outResume[(size_t) ch].size(); ++i)
                recovered = recovered && std::isfinite (outResume[(size_t) ch][i]);
        block[256] = 0.0f;
        auto reference = fresh (type, sampleRate, 2048, params);
        const auto expectedBlock = processSweep (*reference, block, channels, { 512 });
        const auto expectedResume = processSweep (*reference, in, channels, { 512 });
        bool matchesZero = true;
        for (int ch = 0; ch < channels; ++ch)
            matchesZero = matchesZero
                && maxAbsDiff (outBlock[(size_t) ch], expectedBlock[(size_t) ch]) == 0.0
                && maxAbsDiff (outResume[(size_t) ch], expectedResume[(size_t) ch]) == 0.0;
        recovered = recovered && matchesZero;
        std::snprintf (scratch, sizeof scratch,
                       "case=%s leakedPastBlock=%d recoveredInNextBlock=%d",
                       name, (int) leakedPastBlock, (int) recovered);
        report ("hostile", type, sampleRate, channels, scratch, ! leakedPastBlock && recovered);
    }
}

void checkExtremes (const std::string& type, double sampleRate, int channels)
{
    const auto sets = paramsFor (type);
    const auto input = testSignal (sampleRate);
    const double peakTol = std::pow (10.0, 24.0 / 20.0); // +24 dBFS
    std::vector<std::pair<std::string, std::vector<float>>> cases {
        { "allmin", sets.minimum }, { "allmax", sets.maximum },
    };
    for (size_t i = 0; i < sets.defaults.size(); ++i)
    {
        char name[64];
        std::snprintf (name, sizeof name, "p%zu-min", i);
        auto v = sets.defaults;
        v[i] = sets.minimum[i];
        cases.emplace_back (name, v);
        std::snprintf (name, sizeof name, "p%zu-max", i);
        v = sets.defaults;
        v[i] = sets.maximum[i];
        cases.emplace_back (name, v);
    }
    for (const auto& [name, params] : cases)
    {
        auto effect = fresh (type, sampleRate, 2048, params);
        const auto out = processSweep (*effect, input, channels, { 512 });
        bool finite = true;
        double peak = 0.0;
        for (int ch = 0; ch < channels; ++ch)
        {
            finite = finite && allFinite (out[(size_t) ch]);
            peak = std::max (peak, peakAbs (out[(size_t) ch]));
        }
        auto rerun = fresh (type, sampleRate, 2048, params);
        std::vector<float> prefix (input.begin(),
                                   input.begin() + (ptrdiff_t) std::llround (sampleRate * 1.0));
        (void) processSweep (*rerun, prefix, channels, { 512 });
        rerun->reset();
        std::vector<float> head (input.begin(), input.begin() + 512);
        const auto afterReset = processSweep (*rerun, head, channels, { 512 });
        const auto freshOut = processSweep (
            *fresh (type, sampleRate, 2048, params), head, channels, { 512 });
        double resetDiff = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            resetDiff = std::max (resetDiff,
                                  maxAbsDiff (afterReset[(size_t) ch], freshOut[(size_t) ch]));
        std::snprintf (scratch, sizeof scratch,
                       "case=%s finite=%d peak=%.3f tol=%.2f resetDiff=%.3e",
                       name.c_str(), (int) finite, peak, peakTol, resetDiff);
        report ("extremes", type, sampleRate, channels, scratch,
                finite && peak < peakTol && resetDiff == 0.0);
    }
}

void checkSmoothing (const std::string& type, double sampleRate, int channels)
{
    const auto sets = paramsFor (type);
    auto input = sineSignal (sampleRate, 1000.0, 0.5);
    auto jumped = fresh (type, sampleRate, 2048, sets.minimum);
    {
        const auto tick = static_cast<size_t> (std::llround (sampleRate * 0.1));
        Stereo buffers ((size_t) channels, input);
        std::vector<float> current = sets.minimum;
        bool atMax = false;
        size_t offset = 0;
        int tickIndex = 0;
        std::array<float*, 2> base { buffers[0].data(),
                                     channels > 1 ? buffers[1].data() : nullptr };
        while (offset < input.size())
        {
            if (tickIndex > 0)
            {
                atMax = ! atMax;
                current = atMax ? sets.maximum : sets.minimum;
                jumped->setTargets (current.data());
            }
            const auto block = std::min (tick, input.size() - offset);
            size_t done = 0;
            while (done < block)
            {
                const auto n = std::min<size_t> (512, block - done);
                std::array<float*, 2> chunk { base[0] + offset + done,
                                              base[1] != nullptr ? base[1] + offset + done : nullptr };
                jumped->process (chunk.data(), channels, static_cast<int> (n));
                done += n;
            }
            offset += block;
            ++tickIndex;
        }
        double jumpMax = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            for (size_t i = 1; i < buffers[(size_t) ch].size(); ++i)
                jumpMax = std::max (jumpMax, (double) std::abs (
                    buffers[(size_t) ch][i] - buffers[(size_t) ch][i - 1]));
        auto referenceParams = sets.maximum;
        if (type == "reverb")
        {
            referenceParams = sets.minimum;
            referenceParams[2] = 1.0f;
        }
        const auto ref = processSweep (
            *fresh (type, sampleRate, 2048, referenceParams), input, channels, { 512 });
        double refMax = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            for (size_t i = 1; i < ref[(size_t) ch].size(); ++i)
                refMax = std::max (refMax, (double) std::abs (
                    ref[(size_t) ch][i] - ref[(size_t) ch][i - 1]));
        if (type == "reverb")
            for (size_t i = 1; i < input.size(); ++i)
                refMax = std::max (refMax, static_cast<double> (std::abs (input[i] - input[i - 1])));
        const auto ratio = jumpMax / std::max (refMax, 1e-12);
        std::snprintf (scratch, sizeof scratch,
                       "zipperMax=%.4f refMax=%.4f ratio=%.3f tol=4.0", jumpMax, refMax, ratio);
        report ("smoothing", type, sampleRate, channels, scratch,
                allFinite (buffers[0]) && (channels == 1 || allFinite (buffers[1])) && ratio < 4.0);
    }

    if (type == "reverb")
    {
        auto effect = fresh (type, sampleRate, 2048, sets.defaults);
        std::vector<float> pre = sineSignal (sampleRate, 1000.0, 0.5, 0.5);
        const auto outPre = processSweep (*effect, pre, channels, { 512 });
        std::vector<float> targets = sets.defaults;
        targets[0] = 1.0f;
        effect->setTargets (targets.data());
        dynamic_cast<semantifx::dsp::ReverbEffect*> (effect.get())->rebuildImpulseResponse();
        std::vector<float> post = sineSignal (sampleRate, 1000.0, 0.5, 512.0 / sampleRate);
        for (size_t i = 0; i < post.size(); ++i)
            post[i] = 0.5f * std::sin (2.0 * M_PI * 1000.0 * (pre.size() + i) / sampleRate);
        const auto outPost = processSweep (*effect, post, channels, { 512 });
        double step = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            step = std::max (step, static_cast<double> (std::abs (
                outPost[(size_t) ch][0] - outPre[(size_t) ch].back())));
        for (int ch = 0; ch < channels; ++ch)
            for (size_t i = 1; i < outPost[(size_t) ch].size(); ++i)
                step = std::max (step, (double) std::abs (
                    outPost[(size_t) ch][i] - outPost[(size_t) ch][i - 1]));
        std::snprintf (scratch, sizeof scratch,
                       "rebuildStep=%.4f tol=0.1", step);
        report ("smoothing", type, sampleRate, channels, scratch, step <= 0.1);
    }

    {
        const auto& targets = sets.maximum;
        auto aged = fresh (type, sampleRate, 2048, sets.defaults);
        std::vector<float> runIn = sineSignal (sampleRate, 1000.0, 0.5, 1.0);
        (void) processSweep (*aged, runIn, channels, { 512 });
        aged->setTargets (targets.data());
        aged->snapSmoothers();
        if (type == "reverb")
            dynamic_cast<semantifx::dsp::ReverbEffect*> (aged.get())->rebuildImpulseResponse();
        aged->reset();
        std::vector<float> head = sineSignal (sampleRate, 1000.0, 0.5, 512.0 / sampleRate);
        const auto outAged = processSweep (*aged, head, channels, { 512 });
        const auto outFresh = processSweep (
            *fresh (type, sampleRate, 2048, targets), head, channels, { 512 });
        double diff = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            diff = std::max (diff, maxAbsDiff (outAged[(size_t) ch], outFresh[(size_t) ch]));
        auto agedNoReset = fresh (type, sampleRate, 2048, sets.defaults);
        (void) processSweep (*agedNoReset, runIn, channels, { 512 });
        agedNoReset->setTargets (targets.data());
        agedNoReset->snapSmoothers();
        if (type == "reverb")
            dynamic_cast<semantifx::dsp::ReverbEffect*> (agedNoReset.get())->rebuildImpulseResponse();
        const auto outNoReset = processSweep (*agedNoReset, head, channels, { 512 });
        double literalDiff = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            literalDiff = std::max (literalDiff,
                                    maxAbsDiff (outNoReset[(size_t) ch], outFresh[(size_t) ch]));
        std::snprintf (scratch, sizeof scratch,
                       "snapDiff=%.3e literalNoResetDiff=%.3e tol=0", diff, literalDiff);
        report ("smoothing", type, sampleRate, channels, scratch, diff == 0.0);
    }
}

void checkStereo (const std::string& type, double sampleRate)
{
    const auto params = paramsFor (type).defaults;
    const auto input = testSignal (sampleRate);
    {
        auto effect = fresh (type, sampleRate, 2048, params);
        const auto out = processSweep (*effect, input, 2, { 512 });
        const auto diff = maxAbsDiff (out[0], out[1]);
        if (type == "reverb")
        {
            const auto repeat = processSweep (*fresh (type, sampleRate, 2048, params), input, 2, { 512 });
            const auto repeatDiff = std::max (maxAbsDiff (out[0], repeat[0]),
                                              maxAbsDiff (out[1], repeat[1]));
            std::snprintf (scratch, sizeof scratch,
                           "decorrelatedDiff=%.3e repeatDiff=%.3e", diff, repeatDiff);
            report ("stereo-decorrelation", type, sampleRate, 2, scratch,
                    allFinite (out[0]) && allFinite (out[1]) && diff > 1.0e-3 && repeatDiff == 0.0);
        }
        else
        {
            std::snprintf (scratch, sizeof scratch, "symDiff=%.3e tol=1e-07", diff);
            report ("stereo-sym", type, sampleRate, 2, scratch, diff <= 1.0e-7);
        }
    }
    { // Signal in L only -> R behaviour (design: no cross-feed anywhere, so
        auto effect = fresh (type, sampleRate, 2048, params);
        Stereo buffers { input, std::vector<float> (input.size(), 0.0f) };
        std::array<float*, 2> base { buffers[0].data(), buffers[1].data() };
        size_t offset = 0;
        while (offset < input.size())
        {
            const auto block = std::min<size_t> (512, input.size() - offset);
            std::array<float*, 2> chunk { base[0] + offset, base[1] + offset };
            effect->process (chunk.data(), 2, static_cast<int> (block));
            offset += block;
        }
        const auto peakR = peakAbs (buffers[1]);
        std::snprintf (scratch, sizeof scratch,
                       "lOnlyPeakR=%.3e tol=0 (design: no cross-feed)", peakR);
        report ("stereo-iso", type, sampleRate, 2, scratch, peakR == 0.0);
    }
}

void checkSpecificEq (double sampleRate, int channels)
{
    auto base = paramsFor ("eq").defaults;
    auto boosted = base;
    boosted[3] = 12.0f; boosted[4] = 1000.0f; boosted[5] = 2.0f;
    for (double freq : { 1000.0, 100.0 })
    {
        const auto in = sineSignal (sampleRate, freq, 0.5);
        const auto out = processSweep (
            *fresh ("eq", sampleRate, 2048, boosted), in, channels, { 512 });
        const auto skip = static_cast<size_t> (std::llround (sampleRate * 1.0));
        if (freq == 1000.0)
        {
            const auto outDb = toDb (rms (out[0], skip) / rms (in, skip));
            std::snprintf (scratch, sizeof scratch,
                           "boost1k gain=%.3f dB expect=12+-0.5", outDb);
            report ("specific", "eq", sampleRate, channels, scratch,
                    std::abs (outDb - 12.0) <= 0.5);
        }
        else
        {
            const auto outDb = toDb (rms (out[0], skip) / rms (in, skip));
            std::snprintf (scratch, sizeof scratch,
                           "boost1k@100Hz gain=%.4f dB tol=0.5", outDb);
            report ("specific", "eq", sampleRate, channels, scratch,
                    std::abs (outDb) < 0.5);
        }
    }
    auto cut = base;
    cut[3] = -12.0f; cut[4] = 1000.0f; cut[5] = 2.0f;
    {
        const auto in = sineSignal (sampleRate, 1000.0, 0.5);
        const auto out = processSweep (
            *fresh ("eq", sampleRate, 2048, cut), in, channels, { 512 });
        const auto skip = static_cast<size_t> (std::llround (sampleRate * 1.0));
        const auto outDb = toDb (rms (out[0], skip) / rms (in, skip));
        std::snprintf (scratch, sizeof scratch,
                       "cut1k gain=%.3f dB expect=-12+-0.5", outDb);
        report ("specific", "eq", sampleRate, channels, scratch,
                std::abs (outDb + 12.0) <= 0.5);
    }
}

void checkSpecificCompressor (double sampleRate, int channels)
{
    const std::vector<float> params { -20.0f, 4.0f, 20.0f, 6.0f, 0.0f };
    const auto peak = std::pow (10.0, -10.0 / 20.0);
    const std::vector<float> step (static_cast<size_t> (sampleRate * 0.5), static_cast<float> (peak));
    const auto out = processSweep (*fresh ("compressor", sampleRate, 2048, params), step, channels, { 512 });
    const auto sidechainDb = -10.0 + toDb (channels);
    const auto finalGrDb = (-20.0 - sidechainDb) * 0.75;
    const auto expectedDb = -10.0 + finalGrDb;
    const auto level = toDb (rms (out[0], static_cast<size_t> (sampleRate * 0.25)));
    std::snprintf (scratch, sizeof scratch,
                   "hot DC out=%.3f dBFS expected=%.3f tol=0.01", level, expectedDb);
    report ("specific", "compressor", sampleRate, channels, scratch,
            std::abs (level - expectedDb) <= 0.01);

    double t10 = -1.0, t63 = -1.0, t90 = -1.0;
    for (size_t i = 0; i < step.size(); ++i)
    {
        const auto fraction = toDb (out[0][i] / step[i]) / finalGrDb;
        const auto timeMs = (i + 1) * 1000.0 / sampleRate;
        if (t10 < 0.0 && fraction >= 0.1) t10 = timeMs;
        if (t63 < 0.0 && fraction >= 1.0 - std::exp (-1.0)) t63 = timeMs;
        if (t90 < 0.0 && fraction >= 0.9) t90 = timeMs;
    }
    const auto expectedT63 = 20.0 / std::log (9.0);
    const auto sampleMs = 1000.0 / sampleRate;
    std::snprintf (scratch, sizeof scratch,
                   "attack t10-90=%.3f ms expected=20 t63=%.3f expected=%.3f tol=one sample",
                   t90 - t10, t63, expectedT63);
    report ("specific", "compressor", sampleRate, channels, scratch,
            t10 >= 0.0 && std::abs (t90 - t10 - 20.0) <= sampleMs
            && std::abs (t63 - expectedT63) <= sampleMs);

    const auto quiet = sineSignal (sampleRate, 1000.0, std::pow (10.0, -40.0 / 20.0));
    const auto quietOut = processSweep (*fresh ("compressor", sampleRate, 2048, params), quiet, channels, { 512 });
    const auto quietError = maxAbsDiff (quietOut[0], quiet);
    std::snprintf (scratch, sizeof scratch, "quiet maxAbsDiff=%.3e tol=1e-7", quietError);
    report ("specific", "compressor", sampleRate, channels, scratch, quietError <= 1.0e-7);
}

void checkSpecificDistortion (double sampleRate, int channels)
{
    { // drive 0 / mix 0 is unity within 1e-4.
        const std::vector<float> params { 0.0f, 0.5f, 0.0f, 0.0f };
        const auto in = testSignal (sampleRate);
        const auto out = processSweep (
            *fresh ("distortion", sampleRate, 2048, params), in, channels, { 512 });
        double worst = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            worst = std::max (worst, maxAbsDiff (out[(size_t) ch], in));
        std::snprintf (scratch, sizeof scratch,
                       "dryUnity maxAbsDiff=%.3e tol=1e-04", worst);
        report ("specific", "distortion", sampleRate, channels, scratch, worst <= 1.0e-4);
    }
    { // drive 48 on a full-scale sine: peak < 0 dBFS and THD > 5 %.
        const std::vector<float> params { 48.0f, 0.5f, 0.0f, 1.0f };
        const auto in = sineSignal (sampleRate, 1000.0, 1.0);
        const auto out = processSweep (
            *fresh ("distortion", sampleRate, 2048, params), in, channels, { 512 });
        const auto peak = peakAbs (out[0]);
        const auto start = static_cast<size_t> (std::llround (sampleRate * 1.0));
        std::vector<float> windowed (out[0].begin() + static_cast<ptrdiff_t> (start), out[0].end());
        const auto mags = harmonicMags (windowed, 0, sampleRate, 1000.0);
        double harmonics = 0.0;
        for (int k = 2; k <= 8; ++k)
            harmonics += mags[(size_t) k] * mags[(size_t) k];
        const auto thd = std::sqrt (harmonics) / mags[1];
        std::snprintf (scratch, sizeof scratch,
                       "drive48 peak=%.4f tol=1.0 THD=%.4f tol=0.05", peak, thd);
        report ("specific", "distortion", sampleRate, channels, scratch,
                peak <= 1.0 && thd > 0.05);
    }
    { // Harmonic ratio vs character: energy in H5-H8 relative to H2-H4.
        double previous = std::numeric_limits<double>::infinity();
        bool monotonic = true;
        std::string curve;
        char point[64];
        for (float c : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            const std::vector<float> params { 24.0f, c, 0.0f, 1.0f };
            const auto in = sineSignal (sampleRate, 1000.0, 0.9);
            const auto out = processSweep (
                *fresh ("distortion", sampleRate, 2048, params), in, 1, { 512 });
            const auto start = static_cast<size_t> (std::llround (sampleRate * 0.5));
            std::vector<float> windowed (out[0].begin() + static_cast<ptrdiff_t> (start), out[0].end());
            const auto mags = harmonicMags (windowed, 0, sampleRate, 1000.0);
            double lo = 0.0, hi = 0.0;
            for (int k = 2; k <= 4; ++k)
                lo += mags[(size_t) k] * mags[(size_t) k];
            for (int k = 5; k <= 8; ++k)
                hi += mags[(size_t) k] * mags[(size_t) k];
            const auto ratio = hi / std::max (lo, 1e-30);
            std::snprintf (point, sizeof point, "c%.2f=%.4f ", c, ratio);
            curve += point;
            if (! (ratio < previous))
                monotonic = false;
            previous = ratio;
        }
        std::snprintf (scratch, sizeof scratch,
                       "characterCurve %smonotonic-decreasing", curve.c_str());
        report ("specific", "distortion", sampleRate, channels, scratch, monotonic);
    }
}

void checkSpecificReverb (double sampleRate, int channels)
{
    const auto sets = paramsFor ("reverb").defaults;
    { // mix 0 is unity.
        auto neutral = sets;
        neutral[2] = 0.0f;
        const auto in = testSignal (sampleRate);
        const auto out = processSweep (
            *fresh ("reverb", sampleRate, 2048, neutral), in, channels, { 512 });
        double worst = 0.0;
        for (int ch = 0; ch < channels; ++ch)
            worst = std::max (worst, maxAbsDiff (out[(size_t) ch], in));
        const auto inRms = rms (in);
        const auto errDb = toDb (worst / std::sqrt (2.0) / inRms);
        std::snprintf (scratch, sizeof scratch,
                       "dryUnity err=%.1f dB tol=-80dB", errDb);
        report ("specific", "reverb", sampleRate, channels, scratch, errDb <= -80.0);
    }
    { // Tail is monotonic-decaying in 50 ms energy windows after 100 ms.
        auto wet = sets;
        wet[2] = 1.0f;
        const auto ir = captureIr ("reverb", sampleRate, wet);
        const auto win = static_cast<size_t> (std::llround (sampleRate * 0.05));
        const auto start = static_cast<size_t> (std::llround (sampleRate * 0.1));
        bool monotonic = true;
        double previous = std::numeric_limits<double>::infinity();
        for (size_t base = start; base + win <= ir.size(); base += win)
        {
            double energy = 0.0;
            for (size_t i = base; i < base + win; ++i)
                energy += ir[i] * ir[i];
            if (! (energy <= previous))
                monotonic = false;
            previous = energy;
        }
        std::snprintf (scratch, sizeof scratch,
                       "tailMonotonic=%d windows=%zu", (int) monotonic,
                       (ir.size() - start) / win);
        report ("specific", "reverb", sampleRate, channels, scratch, monotonic);
    }
    { // decay 1.0 gives a longer RT60 than decay 0.0.
        auto fast = sets;
        fast[0] = 0.0f; fast[2] = 1.0f;
        auto slow = sets;
        slow[0] = 1.0f; slow[2] = 1.0f;
        const auto tFast = rt60 (captureIr ("reverb", sampleRate, fast), sampleRate);
        const auto tSlow = rt60 (captureIr ("reverb", sampleRate, slow), sampleRate);
        std::snprintf (scratch, sizeof scratch,
                       "rt60decay0=%.4f rt60decay1=%.4f", tFast, tSlow);
        report ("specific", "reverb", sampleRate, channels, scratch,
                std::isfinite (tFast) && std::isfinite (tSlow) && tSlow > tFast);
    }
    { // damping 1.0 has less energy above 4 kHz in the tail than damping 0.0.
        auto hfFrac = [&] (float damping)
        {
            auto wet = sets;
            wet[1] = damping; wet[2] = 1.0f;
            const auto ir = captureIr ("reverb", sampleRate, wet);
            const auto start = static_cast<size_t> (std::llround (sampleRate * 0.1));
            double lo = 0.0, hi = 0.0;
            for (double f : { 500.0, 1000.0, 2000.0, 3000.0 })
                lo += std::pow (goertzel (ir, start, ir.size(), sampleRate, f), 2.0);
            for (double f : { 5000.0, 7000.0, 10000.0, 14000.0 })
                hi += std::pow (goertzel (ir, start, ir.size(), sampleRate, f), 2.0);
            return hi / std::max (lo + hi, 1e-30);
        };
        const auto dry = hfFrac (0.0f);
        const auto damped = hfFrac (1.0f);
        std::snprintf (scratch, sizeof scratch,
                       "hfFracDamp0=%.4f hfFracDamp1=%.4f", dry, damped);
        report ("specific", "reverb", sampleRate, channels, scratch, damped < dry);
    }
}

void checkUnity (const std::string& type, double sampleRate, int channels)
{
    auto neutral = paramsFor (type).defaults;
    if (type == "eq")
        for (size_t i = 0; i < neutral.size(); i += 3)
            neutral[i] = 0.0f;
    else if (type == "distortion") { neutral[0] = 0.0f; neutral[3] = 0.0f; }
    else if (type == "compressor") { neutral[0] = 0.0f; neutral[1] = 1.0f; neutral[4] = 0.0f; }
    else if (type == "reverb") { neutral[2] = 0.0f; }
    const auto in = testSignal (sampleRate);
    const auto out = processSweep (
        *fresh (type, sampleRate, 2048, neutral), in, channels, { 512 });
    double worstDb = -std::numeric_limits<double>::infinity();
    const auto inRms = rms (in);
    for (int ch = 0; ch < channels; ++ch)
    {
        double energy = 0.0;
        for (size_t i = 0; i < in.size(); ++i)
        {
            const auto d = (double) out[(size_t) ch][i] - in[i];
            energy += d * d;
        }
        worstDb = std::max (worstDb, toDb (std::sqrt (energy / in.size()) / inRms));
    }
    std::snprintf (scratch, sizeof scratch,
                   "unityErr=%.1f dB tol=-80dB", worstDb);
    report ("unity", type, sampleRate, channels, scratch, worstDb <= -80.0);
}

void checkSpecific (const std::string& type, double sampleRate, int channels)
{
    if (type == "eq")
        checkSpecificEq (sampleRate, channels);
    else if (type == "distortion")
        checkSpecificDistortion (sampleRate, channels);
    else if (type == "compressor")
        checkSpecificCompressor (sampleRate, channels);
    else
        checkSpecificReverb (sampleRate, channels);
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf (stderr, "usage: semantifx_audio_quality_tests NOISE_TABLE\n");
        return 2;
    }
    gNoisePath = argv[1];

    const std::vector<std::string> types { "eq", "distortion", "compressor", "reverb" };
    const std::vector<double> rates { 44100.0, 48000.0 };
    const std::vector<int> channelCounts { 1, 2 };

    try
    {
        for (const auto& type : types)
        {
            checkSampleRate (type);
            for (auto rate : rates)
            {
                for (auto channels : channelCounts)
                {
                    checkBlockSize (type, rate, channels);
                    checkSilence (type, rate, channels);
                    checkHostile (type, rate, channels);
                    checkExtremes (type, rate, channels);
                    checkSmoothing (type, rate, channels);
                    checkSpecific (type, rate, channels);
                    checkUnity (type, rate, channels);
                }
                checkStereo (type, rate);
            }
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf (stderr, "audio quality test error: %s\n", error.what());
        return 1;
    }

    std::printf ("audio quality: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
