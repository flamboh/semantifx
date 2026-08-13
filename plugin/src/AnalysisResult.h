#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace semantifx::protocol
{
inline constexpr std::array<const char*, 18> canonicalParameterNames {{
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

struct AnalysisResult
{
    juce::String requestId;
    std::array<double, canonicalParameterNames.size()> values {};
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

/** Parses a complete, successful protocol-v1 EQ result without applying it. */
juce::Result parseAnalysisResult (const juce::String& json, AnalysisResult& destination);

/** Parses an advisory progress snapshot produced while an analysis is running. */
juce::Result parseAnalysisProgress (const juce::String& json, AnalysisProgress& destination);

/** Parses the terminal acknowledgement for a cooperatively interrupted request. */
juce::Result parseAnalysisCancellation (const juce::String& json, juce::String& requestId);
} // namespace semantifx::protocol
