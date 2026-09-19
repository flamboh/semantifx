#pragma once

#include "SlotPool.h"

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

namespace semantifx::protocol
{
struct InstanceValues
{
    juce::String instanceId;
    int effectTypeIndex {};
    std::vector<double> values;
};

struct ExpectedChain
{
    std::vector<std::pair<juce::String, int>> items;
};

struct AnalysisResult
{
    juce::String requestId;
    std::vector<InstanceValues> instances;
};

struct AnalysisProgress
{
    juce::String requestId;
    juce::String phase;
    int step {};
    int totalSteps {};
    bool hasParameters {};
    std::vector<InstanceValues> instances;
};

struct AssemblyResult
{
    juce::String requestId;
    std::vector<InstanceValues> chain;
    juce::StringArray warnings;
};

struct ErrorResult
{
    juce::String requestId;
    juce::String type;
    juce::String code;
    juce::String message;
    juce::StringArray remediation;
};

juce::Result parseAnalysisResult (const juce::String& json,
                                  const ExpectedChain& expected,
                                  AnalysisResult& destination);

juce::Result parseAnalysisProgress (const juce::String& json,
                                    const ExpectedChain& expected,
                                    AnalysisProgress& destination);

juce::Result parseAssemblyResult (const juce::String& json, AssemblyResult& destination);

juce::Result parseErrorResult (const juce::String& json, ErrorResult& destination);

juce::Result parseAnalysisCancellation (const juce::String& json, juce::String& requestId);
} // namespace semantifx::protocol
