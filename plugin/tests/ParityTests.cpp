#include "dsp/EffectFactory.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
struct Fixture
{
    std::filesystem::path directory;
    std::string type;
    std::string name;
    int sampleRate {};
    int channels {};
    int samples {};
    std::vector<std::pair<std::string, float>> parameters;
    std::vector<float> input;
    std::vector<float> expected;
};

std::vector<float> readFloats (const std::filesystem::path& path, size_t count)
{
    std::ifstream stream (path, std::ios::binary);
    if (! stream)
        throw std::runtime_error ("Could not open fixture file: " + path.string());
    std::vector<float> values (count);
    stream.read (reinterpret_cast<char*> (values.data()),
                 static_cast<std::streamsize> (values.size() * sizeof (float)));
    if (! stream || stream.gcount() != static_cast<std::streamsize> (values.size() * sizeof (float)))
        throw std::runtime_error ("Fixture has the wrong length: " + path.string());
    return values;
}

Fixture readFixture (const std::filesystem::path& directory)
{
    Fixture fixture;
    fixture.directory = directory;
    fixture.type = directory.parent_path().filename().string();
    fixture.name = directory.filename().string();

    std::ifstream description (directory / "case.txt");
    if (! description)
        throw std::runtime_error ("Could not open case description: "
                                   + (directory / "case.txt").string());

    std::string line;
    while (std::getline (description, line))
    {
        std::istringstream words (line);
        std::string key;
        words >> key;
        if (key.empty())
            continue;
        if (key == "sample_rate")
            words >> fixture.sampleRate;
        else if (key == "channels")
            words >> fixture.channels;
        else if (key == "samples")
            words >> fixture.samples;
        else if (key == "param")
        {
            std::string name;
            float value {};
            words >> name >> value;
            fixture.parameters.emplace_back (std::move (name), value);
        }
        else
            throw std::runtime_error ("Unknown case.txt line: " + line);
    }

    if (fixture.sampleRate <= 0 || fixture.channels != 1 || fixture.samples <= 0
        || fixture.parameters.empty())
        throw std::runtime_error ("Incomplete fixture metadata: " + directory.string());
    fixture.input = readFloats (directory / "input.f32", static_cast<size_t> (fixture.samples));
    fixture.expected = readFloats (directory / "expected.f32", static_cast<size_t> (fixture.samples));
    return fixture;
}

std::vector<Fixture> findFixtures (const std::filesystem::path& root)
{
    std::vector<Fixture> fixtures;
    if (! std::filesystem::exists (root))
        throw std::runtime_error ("Fixture root does not exist: " + root.string());
    for (const auto& entry : std::filesystem::recursive_directory_iterator (root))
        if (entry.is_regular_file() && entry.path().filename() == "case.txt")
            fixtures.push_back (readFixture (entry.path().parent_path()));
    std::sort (fixtures.begin(), fixtures.end(), [] (const Fixture& left, const Fixture& right)
    {
        return left.directory < right.directory;
    });
    return fixtures;
}

std::vector<float> render (const Fixture& fixture, const std::string& noisePath, int blockSize)
{
    semantifx::dsp::ReverbResources resources { noisePath.c_str() };
    auto effect = semantifx::dsp::makeEffect (fixture.type, resources);
    if (static_cast<size_t> (effect->parameterCount()) != fixture.parameters.size())
        throw std::runtime_error ("Parameter count mismatch in " + fixture.directory.string());

    std::vector<float> output = fixture.input;
    std::vector<float> parameters;
    parameters.reserve (fixture.parameters.size());
    for (const auto& parameter : fixture.parameters)
        parameters.push_back (parameter.second);

    effect->prepare (fixture.sampleRate, 512);
    effect->setTargets (parameters.data());
    effect->snapSmoothers();
    if (auto* reverb = dynamic_cast<semantifx::dsp::ReverbEffect*> (effect.get()))
        reverb->rebuildImpulseResponse();
    for (int offset = 0; offset < fixture.samples; offset += blockSize)
    {
        const auto count = std::min (blockSize, fixture.samples - offset);
        float* channelPointers[] { output.data() + offset };
        effect->process (channelPointers, fixture.channels, count);
    }
    return output;
}

double maxAbsDifference (const std::vector<float>& actual, const std::vector<float>& expected)
{
    if (actual.size() != expected.size())
        return INFINITY;
    double maximum = 0.0;
    for (size_t index = 0; index < actual.size(); ++index)
    {
        if (! std::isfinite (actual[index]))
            return INFINITY;
        maximum = std::max (maximum, std::abs (static_cast<double> (actual[index])
                                               - static_cast<double> (expected[index])));
    }
    return maximum;
}

double toleranceFor (const std::string& type)
{
    if (type == "distortion")
        return 1.0e-5;
    if (type == "eq")
        return 1.0e-4;
    if (type == "compressor")
        return 1.0e-4;
    if (type == "reverb")
        return 1.0e-3;
    throw std::runtime_error ("No tolerance for effect type: " + type);
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: semantifx_parity_tests FIXTURE_ROOT NOISE_TABLE\n";
        return 2;
    }

    try
    {
        const auto fixtures = findFixtures (argv[1]);
        std::map<std::string, const Fixture*> firstByType;
        std::cout << "type case maxAbsDiff tolerance PASS/FAIL\n";
        bool allPassed = true;
        for (const auto& fixture : fixtures)
        {
            const auto actual = render (fixture, argv[2], 512);
            const auto difference = maxAbsDifference (actual, fixture.expected);
            const auto tolerance = toleranceFor (fixture.type);
            const auto passed = difference < tolerance;
            allPassed = allPassed && passed;
            firstByType.emplace (fixture.type, &fixture);
            std::cout << fixture.type << ' ' << fixture.name << ' '
                      << std::scientific << std::setprecision (9) << difference << ' '
                      << tolerance << ' ' << (passed ? "PASS" : "FAIL") << '\n';
        }

        bool blockSizesPassed = true;
        for (const auto& [type, fixture] : firstByType)
        {
            const auto reference = render (*fixture, argv[2], 512);
            for (const auto blockSize : { 1, 4096 })
            {
                const auto candidate = render (*fixture, argv[2], blockSize);
                if (candidate.size() != reference.size()
                    || std::memcmp (candidate.data(), reference.data(),
                                    candidate.size() * sizeof (float)) != 0)
                {
                    blockSizesPassed = false;
                    std::cerr << "block-size independence failed for " << type
                              << " at block size " << blockSize << '\n';
                }
            }
        }

        std::cout << "block-size independence "
                  << (blockSizesPassed ? "PASS" : "FAIL") << '\n';
        return allPassed && blockSizesPassed ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "parity test error: " << error.what() << '\n';
        return 1;
    }
}
