#pragma once

#include "generated/EffectParameters.h"

#include <juce_core/juce_core.h>

#include <array>

namespace semantifx::protocol
{
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

/** Parses a complete, successful chain result. */
juce::Result parseAnalysisResult (const juce::String& json, AnalysisResult& destination);

/** Parses an advisory progress snapshot produced while an analysis is running. */
juce::Result parseAnalysisProgress (const juce::String& json, AnalysisProgress& destination);

/** Parses the terminal acknowledgement for a cooperatively interrupted request. */
juce::Result parseAnalysisCancellation (const juce::String& json, juce::String& requestId);
} // namespace semantifx::protocol
