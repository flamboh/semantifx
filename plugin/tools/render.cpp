// semantifx_render: offline, JUCE-free renderer for the SemantiFX catalogue DSP.
//
// Renders a WAV file through a serial chain of the exact C++ effects the
// plugin uses and writes 32-bit float WAV out. Used for listening tests and
// diagnostics; see inference/diagnostics/render_gallery.py.
//
// Usage:
//   semantifx_render <in.wav> <out.wav> [--block N]
//                  [--effect TYPE[:name=value,...]]...
//
// Effects are applied in the order given. Parameters not listed stay at their
// generated defaults (see src/generated/EffectParameters.h). Unknown effect
// types, unknown parameter names, and out-of-range values are rejected.

#include "dsp/EffectDefaults.h"
#include "dsp/EffectFactory.h"
#include "dsp/ReverbEffect.h"
#include "generated/EffectParameters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
#ifndef SEMANTIFX_NOISE_TABLE_PATH
#define SEMANTIFX_NOISE_TABLE_PATH ""
#endif

struct WavData
{
    int sampleRate {};
    int channels {};
    std::vector<std::vector<float>> samples; // per-channel, non-interleaved
};

uint32_t readU32LE (const unsigned char* bytes)
{
    return static_cast<uint32_t> (bytes[0])
         | (static_cast<uint32_t> (bytes[1]) << 8)
         | (static_cast<uint32_t> (bytes[2]) << 16)
         | (static_cast<uint32_t> (bytes[3]) << 24);
}

uint16_t readU16LE (const unsigned char* bytes)
{
    return static_cast<uint16_t> (bytes[0])
         | (static_cast<uint16_t> (bytes[1]) << 8);
}

void writeU32LE (std::ofstream& stream, uint32_t value)
{
    unsigned char bytes[4] = {
        static_cast<unsigned char> (value & 0xFF),
        static_cast<unsigned char> ((value >> 8) & 0xFF),
        static_cast<unsigned char> ((value >> 16) & 0xFF),
        static_cast<unsigned char> ((value >> 24) & 0xFF),
    };
    stream.write (reinterpret_cast<char*> (bytes), 4);
}

void writeU16LE (std::ofstream& stream, uint16_t value)
{
    unsigned char bytes[2] = {
        static_cast<unsigned char> (value & 0xFF),
        static_cast<unsigned char> ((value >> 8) & 0xFF),
    };
    stream.write (reinterpret_cast<char*> (bytes), 2);
}

WavData readWav (const std::string& path)
{
    std::ifstream stream (path, std::ios::binary);
    if (! stream)
        throw std::runtime_error ("Could not open input file: " + path);

    unsigned char riff[12];
    stream.read (reinterpret_cast<char*> (riff), 12);
    if (! stream || std::memcmp (riff, "RIFF", 4) != 0
        || std::memcmp (riff + 8, "WAVE", 4) != 0)
        throw std::runtime_error ("Not a RIFF/WAVE file: " + path);

    int audioFormat = -1, channels = 0, sampleRate = 0, bitsPerSample = 0;
    bool haveFormat = false;
    std::vector<unsigned char> audioBytes;

    while (stream)
    {
        unsigned char header[8];
        stream.read (reinterpret_cast<char*> (header), 8);
        if (! stream)
            break;
        const auto chunkId = std::string (reinterpret_cast<char*> (header), 4);
        auto chunkSize = readU32LE (header + 4);
        auto remaining = static_cast<std::streamsize> (chunkSize);
        // Chunks are word-aligned; a pad byte follows odd-sized chunks.
        const bool padded = (chunkSize % 2) == 1;

        if (chunkId == "fmt ")
        {
            std::vector<unsigned char> format (chunkSize);
            stream.read (reinterpret_cast<char*> (format.data()), remaining);
            if (! stream || chunkSize < 16)
                throw std::runtime_error ("Truncated fmt chunk in: " + path);
            audioFormat = readU16LE (format.data());
            channels = readU16LE (format.data() + 2);
            sampleRate = static_cast<int> (readU32LE (format.data() + 4));
            bitsPerSample = readU16LE (format.data() + 14);
            haveFormat = true;
        }
        else if (chunkId == "data")
        {
            audioBytes.resize (chunkSize);
            stream.read (reinterpret_cast<char*> (audioBytes.data()), remaining);
            if (! stream)
                throw std::runtime_error ("Truncated data chunk in: " + path);
        }
        else
        {
            stream.ignore (remaining);
        }
        if (padded)
            stream.ignore (1);
    }

    if (! haveFormat)
        throw std::runtime_error ("No fmt chunk in: " + path);
    if (audioBytes.empty())
        throw std::runtime_error ("No data chunk in: " + path);
    if (channels != 1 && channels != 2)
        throw std::runtime_error ("Only mono and stereo input are supported (channels="
                                  + std::to_string (channels) + " in " + path + ")");
    if (sampleRate <= 0)
        throw std::runtime_error ("Invalid sample rate in: " + path);

    const size_t bytesPerSample = (audioFormat == 1)
        ? static_cast<size_t> (bitsPerSample / 8) : 0;
    WavData data;
    data.sampleRate = sampleRate;
    data.channels = channels;

    const auto frameCount = [&]
    {
        if (audioFormat == 3 && bitsPerSample == 32)
            return audioBytes.size() / (4 * static_cast<size_t> (channels));
        if (audioFormat == 1 && (bitsPerSample == 16 || bitsPerSample == 24))
            return audioBytes.size()
                 / (bytesPerSample * static_cast<size_t> (channels));
        throw std::runtime_error ("Unsupported WAV format in " + path
                                  + " (format=" + std::to_string (audioFormat)
                                  + " bits=" + std::to_string (bitsPerSample)
                                  + "); want 16/24-bit PCM or 32-bit float");
    }();

    data.samples.assign (static_cast<size_t> (channels),
                         std::vector<float> (frameCount));
    const unsigned char* raw = audioBytes.data();
    for (size_t frame = 0; frame < frameCount; ++frame)
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto* bytes = raw
                + (frame * static_cast<size_t> (channels)
                   + static_cast<size_t> (channel))
                * (audioFormat == 3 ? 4 : bytesPerSample);
            float value = 0.0f;
            if (audioFormat == 3)
            {
                std::memcpy (&value, bytes, 4);
            }
            else if (bitsPerSample == 16)
            {
                const auto word = static_cast<int16_t> (readU16LE (bytes));
                value = static_cast<float> (word) / 32768.0f;
            }
            else
            {
                int32_t word = static_cast<int32_t> (bytes[0])
                             | (static_cast<int32_t> (bytes[1]) << 8)
                             | (static_cast<int32_t> (bytes[2]) << 16);
                if (word & 0x800000)
                    word |= ~0xFFFFFF;
                value = static_cast<float> (word) / 8388608.0f;
            }
            data.samples[static_cast<size_t> (channel)][frame] = value;
        }
    }
    return data;
}

void writeFloatWav (const std::string& path, const WavData& data)
{
    std::ofstream stream (path, std::ios::binary);
    if (! stream)
        throw std::runtime_error ("Could not open output file: " + path);
    const auto frameCount = data.samples.empty() ? 0 : data.samples[0].size();
    const uint32_t dataBytes = static_cast<uint32_t> (
        frameCount * static_cast<size_t> (data.channels) * 4);

    stream.write ("RIFF", 4);
    writeU32LE (stream, 36 + dataBytes);
    stream.write ("WAVE", 4);
    stream.write ("fmt ", 4);
    writeU32LE (stream, 16);
    writeU16LE (stream, 3); // IEEE float
    writeU16LE (stream, static_cast<uint16_t> (data.channels));
    writeU32LE (stream, static_cast<uint32_t> (data.sampleRate));
    writeU32LE (stream, static_cast<uint32_t> (data.sampleRate * data.channels * 4));
    writeU16LE (stream, static_cast<uint16_t> (data.channels * 4));
    writeU16LE (stream, 32);
    stream.write ("data", 4);
    writeU32LE (stream, dataBytes);
    for (size_t frame = 0; frame < frameCount; ++frame)
        for (int channel = 0; channel < data.channels; ++channel)
        {
            const float value = data.samples[static_cast<size_t> (channel)][frame];
            stream.write (reinterpret_cast<const char*> (&value), 4);
        }
    if (! stream)
        throw std::runtime_error ("Failed writing output file: " + path);
}

struct LevelStats
{
    double peak {};
    double rms {};
};

LevelStats analyseLevels (const WavData& data)
{
    double peak = 0.0, sumSquares = 0.0;
    size_t count = 0;
    for (const auto& channel : data.samples)
        for (const auto value : channel)
        {
            const auto magnitude = std::abs (static_cast<double> (value));
            peak = std::max (peak, magnitude);
            sumSquares += magnitude * magnitude;
            ++count;
        }
    LevelStats stats;
    stats.peak = peak;
    stats.rms = count > 0 ? std::sqrt (sumSquares / static_cast<double> (count)) : 0.0;
    return stats;
}

std::string formatDb (double linear)
{
    if (linear <= 0.0)
        return "-inf";
    char buffer[32];
    std::snprintf (buffer, sizeof (buffer), "%.2f",
                   20.0 * std::log10 (linear));
    return buffer;
}

struct EffectRequest
{
    std::string type;
    std::vector<std::pair<std::string, float>> overrides;
};

const semantifx::protocol::EffectTypeSpec* findType (const std::string& name)
{
    for (const auto& type : semantifx::protocol::effectTypes)
        if (name == type.name)
            return &type;
    return nullptr;
}

const semantifx::protocol::ParameterSpec* findParameter (
    const semantifx::protocol::EffectTypeSpec& type, const std::string& name)
{
    for (size_t index = 0; index < type.parameterCount; ++index)
    {
        const auto& spec = semantifx::protocol::parameters[type.firstParameter + index];
        if (name == spec.name)
            return &spec;
    }
    return nullptr;
}

std::vector<std::string> split (const std::string& text, char separator)
{
    std::vector<std::string> parts;
    std::string current;
    for (const auto character : text)
    {
        if (character == separator)
        {
            parts.push_back (current);
            current.clear();
        }
        else
            current.push_back (character);
    }
    parts.push_back (current);
    return parts;
}

float parseFloat (const std::string& text, const std::string& context)
{
    char* end = nullptr;
    const float value = std::strtof (text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || ! std::isfinite (value))
        throw std::runtime_error ("Invalid number '" + text + "' in " + context);
    return value;
}

EffectRequest parseEffect (const std::string& spec)
{
    EffectRequest request;
    const auto colon = spec.find (':');
    const auto typeName = colon == std::string::npos ? spec : spec.substr (0, colon);
    const auto* type = findType (typeName);
    if (type == nullptr)
        throw std::runtime_error ("Unknown effect type '" + typeName
                                  + "'; want one of eq, distortion, compressor, reverb");
    request.type = typeName;
    if (colon == std::string::npos)
        return request;

    for (const auto& assignment : split (spec.substr (colon + 1), ','))
    {
        if (assignment.empty())
            continue;
        const auto equals = assignment.find ('=');
        if (equals == std::string::npos)
            throw std::runtime_error ("Bad parameter assignment '" + assignment
                                      + "'; want name=value");
        const auto name = assignment.substr (0, equals);
        const auto value = parseFloat (assignment.substr (equals + 1),
                                       "parameter '" + name + "'");
        const auto* parameter = findParameter (*type, name);
        if (parameter == nullptr)
            throw std::runtime_error ("Unknown parameter '" + name
                                      + "' for effect '" + typeName + "'");
        if (value < parameter->minimum || value > parameter->maximum)
            throw std::runtime_error ("Parameter '" + name + "' value "
                                      + std::to_string (value) + " out of range ["
                                      + std::to_string (parameter->minimum) + ", "
                                      + std::to_string (parameter->maximum) + "]");
        request.overrides.emplace_back (name, value);
    }
    return request;
}

void usage()
{
    std::cerr << "usage: semantifx_render <in.wav> <out.wav> [--block N] "
                 "[--effect TYPE[:name=value,...]]...\n";
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        usage();
        return 2;
    }
    const std::string inputPath = argv[1];
    const std::string outputPath = argv[2];

    int blockSize = 512;
    std::vector<EffectRequest> chain;
    try
    {
        for (int index = 3; index < argc; ++index)
        {
            std::string flag = argv[index];
            auto valueOf = [&](const char* flagName) -> std::string
            {
                const auto name = std::string (flagName);
                if (flag == name)
                {
                    if (index + 1 >= argc)
                        throw std::runtime_error (std::string ("Missing value for ")
                                                  + flagName);
                    return argv[++index];
                }
                if (flag.rfind (name + "=", 0) == 0)
                    return flag.substr (name.size() + 1);
                throw std::runtime_error ("Unknown argument '" + flag + "'");
            };
            if (flag == "--block" || flag.rfind ("--block=", 0) == 0)
            {
                const auto text = valueOf ("--block");
                char* end = nullptr;
                const long value = std::strtol (text.c_str(), &end, 10);
                if (end == text.c_str() || *end != '\0' || value <= 0
                    || value > 1048576)
                    throw std::runtime_error ("Invalid block size '" + text
                                              + "'; want a positive integer");
                blockSize = static_cast<int> (value);
            }
            else if (flag == "--effect" || flag.rfind ("--effect=", 0) == 0)
            {
                chain.push_back (parseEffect (valueOf ("--effect")));
            }
            else
            {
                usage();
                std::cerr << "Unknown argument '" << flag << "'\n";
                return 2;
            }
        }

        WavData audio = readWav (inputPath);
        const auto inputLevels = analyseLevels (audio);
        const auto frameCount = audio.samples.empty() ? 0 : audio.samples[0].size();

        semantifx::dsp::ReverbResources resources { SEMANTIFX_NOISE_TABLE_PATH };
        std::vector<std::unique_ptr<semantifx::dsp::Effect>> effects;
        for (const auto& request : chain)
        {
            auto effect = semantifx::dsp::makeEffect (request.type, resources);
            const auto* type = findType (request.type);
            std::vector<float> values;
            values.reserve (type->parameterCount);
            for (size_t offset = 0; offset < type->parameterCount; ++offset)
            {
                const auto& spec = semantifx::protocol::parameters[type->firstParameter
                                                                  + offset];
                auto value = spec.defaultValue;
                for (const auto& override : request.overrides)
                    if (override.first == spec.name)
                        value = override.second;
                values.push_back (value);
            }
            effect->prepare (static_cast<double> (audio.sampleRate), blockSize);
            effect->setTargets (values.data());
            effect->snapSmoothers();
            if (request.type == "reverb")
            {
                auto* reverb = dynamic_cast<semantifx::dsp::ReverbEffect*> (effect.get());
                if (reverb == nullptr)
                    throw std::runtime_error ("Reverb effect has the wrong class");
                if (std::string (SEMANTIFX_NOISE_TABLE_PATH).empty())
                    throw std::runtime_error ("Reverb noise table path is not configured");
                reverb->rebuildImpulseResponse();
                if (reverb->impulseResponseDirty())
                    throw std::runtime_error ("Reverb noise table could not be loaded from "
                                              + std::string (SEMANTIFX_NOISE_TABLE_PATH));
            }
            effects.push_back (std::move (effect));
        }

        std::array<float*, 2> pointers { nullptr, nullptr };
        for (size_t offset = 0; offset < frameCount; offset += static_cast<size_t> (blockSize))
        {
            const auto count = std::min (static_cast<size_t> (blockSize),
                                         frameCount - offset);
            for (int channel = 0; channel < audio.channels; ++channel)
                pointers[static_cast<size_t> (channel)]
                    = audio.samples[static_cast<size_t> (channel)].data() + offset;
            for (auto& effect : effects)
                effect->process (pointers.data(), audio.channels,
                                 static_cast<int> (count));
        }

        const auto outputLevels = analyseLevels (audio);
        writeFloatWav (outputPath, audio);

        std::cout << "input: peak " << formatDb (inputLevels.peak) << " dBFS,"
                  << " rms " << formatDb (inputLevels.rms) << " dBFS\n";
        std::cout << "output: peak " << formatDb (outputLevels.peak) << " dBFS,"
                  << " rms " << formatDb (outputLevels.rms) << " dBFS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "semantifx_render: error: " << error.what() << '\n';
        return 1;
    }
}
