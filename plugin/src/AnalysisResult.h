#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace semantifx::protocol
{
inline constexpr std::array<const char*, 18> eqParameterNames {{
    "low_shelf_gain_db",
    "low_shelf_cutoff_freq",
    "low_shelf_q_factor",
    "band0_gain_db",
    "band0_cutoff_freq",
    "band0_q_factor",
    "band1_gain_db",
    "band1_cutoff_freq",
    "band1_q_factor",
    "band2_gain_db",
    "band2_cutoff_freq",
    "band2_q_factor",
    "band3_gain_db",
    "band3_cutoff_freq",
    "band3_q_factor",
    "high_shelf_gain_db",
    "high_shelf_cutoff_freq",
    "high_shelf_q_factor",
}};

inline constexpr std::array<const char*, 4> distortionParameterNames {{
    "drive_db",
    "character",
    "tone_db",
    "mix",
}};

struct ParameterSpec
{
    const char* effectInstanceId;
    const char* effectType;
    const char* name;
    const char* unit;
    const char* pluginParameterId;
};

inline constexpr std::array<ParameterSpec, 22> chainParameterSpecs {{
    { "eq-1", "eq", eqParameterNames[0], "dB", eqParameterNames[0] },
    { "eq-1", "eq", eqParameterNames[1], "Hz", eqParameterNames[1] },
    { "eq-1", "eq", eqParameterNames[2], "ratio", eqParameterNames[2] },
    { "eq-1", "eq", eqParameterNames[3], "dB", eqParameterNames[3] },
    { "eq-1", "eq", eqParameterNames[4], "Hz", eqParameterNames[4] },
    { "eq-1", "eq", eqParameterNames[5], "ratio", eqParameterNames[5] },
    { "eq-1", "eq", eqParameterNames[6], "dB", eqParameterNames[6] },
    { "eq-1", "eq", eqParameterNames[7], "Hz", eqParameterNames[7] },
    { "eq-1", "eq", eqParameterNames[8], "ratio", eqParameterNames[8] },
    { "eq-1", "eq", eqParameterNames[9], "dB", eqParameterNames[9] },
    { "eq-1", "eq", eqParameterNames[10], "Hz", eqParameterNames[10] },
    { "eq-1", "eq", eqParameterNames[11], "ratio", eqParameterNames[11] },
    { "eq-1", "eq", eqParameterNames[12], "dB", eqParameterNames[12] },
    { "eq-1", "eq", eqParameterNames[13], "Hz", eqParameterNames[13] },
    { "eq-1", "eq", eqParameterNames[14], "ratio", eqParameterNames[14] },
    { "eq-1", "eq", eqParameterNames[15], "dB", eqParameterNames[15] },
    { "eq-1", "eq", eqParameterNames[16], "Hz", eqParameterNames[16] },
    { "eq-1", "eq", eqParameterNames[17], "ratio", eqParameterNames[17] },
    { "distortion-1", "distortion", distortionParameterNames[0], "dB", "distortion_drive_db" },
    { "distortion-1", "distortion", distortionParameterNames[1], "ratio", "distortion_character" },
    { "distortion-1", "distortion", distortionParameterNames[2], "dB", "distortion_tone_db" },
    { "distortion-1", "distortion", distortionParameterNames[3], "ratio", "distortion_mix" },
}};

enum class EffectOrder { eqThenDistortion, distortionThenEq };

struct AnalysisResult
{
    juce::String requestId;
    EffectOrder effectOrder { EffectOrder::eqThenDistortion };
    std::array<double, chainParameterSpecs.size()> values {};
};

struct AnalysisProgress
{
    juce::String requestId;
    juce::String phase;
    int step {};
    int totalSteps {};
    bool hasParameters {};
    AnalysisResult preview;
};

/** Parses a complete, successful protocol-v2 chain result. */
juce::Result parseAnalysisResult (const juce::String& json, AnalysisResult& destination);

/** Parses an advisory progress snapshot produced while an analysis is running. */
juce::Result parseAnalysisProgress (const juce::String& json, AnalysisProgress& destination);

/** Parses the terminal acknowledgement for a cooperatively interrupted request. */
juce::Result parseAnalysisCancellation (const juce::String& json, juce::String& requestId);
} // namespace semantifx::protocol
