#include "AnalysisResult.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
enum class Mutation { none, missing, unknown, nonNumeric };

juce::String unitFor (const juce::String& name)
{
    return name.endsWith ("_gain_db") ? "dB" : name.endsWith ("_freq") ? "Hz" : "ratio";
}

juce::String makeResult (Mutation mutation = Mutation::none)
{
    auto root = juce::var { new juce::DynamicObject {} };
    auto* rootObject = root.getDynamicObject();
    rootObject->setProperty ("protocol_version", "1");
    rootObject->setProperty ("type", "analysis_result");
    rootObject->setProperty ("request_id", "result-test");
    rootObject->setProperty ("status", "ok");

    juce::Array<juce::var> entries;
    const auto count = semantifx::protocol::canonicalParameterNames.size() - (mutation == Mutation::missing ? 1 : 0);
    for (size_t index = 0; index < count; ++index)
    {
        auto entry = juce::var { new juce::DynamicObject {} };
        auto* object = entry.getDynamicObject();
        auto name = juce::String { semantifx::protocol::canonicalParameterNames[index] };
        if (mutation == Mutation::unknown && index == 0)
            name = "mystery_gain";
        object->setProperty ("effect", "eq");
        object->setProperty ("name", name);
        object->setProperty ("value", mutation == Mutation::nonNumeric && index == 0
                                          ? juce::var { "loud" } : juce::var { static_cast<double> (index) });
        object->setProperty ("unit", unitFor (name));
        entries.add (entry);
    }
    rootObject->setProperty ("parameters", entries);
    return juce::JSON::toString (root);
}

juce::String makeProgress (int step = 17, int totalSteps = 600, bool includeParameters = true)
{
    auto result = juce::JSON::parse (makeResult());
    auto* object = result.getDynamicObject();
    object->setProperty ("type", "analysis_progress");
    object->removeProperty ("status");
    object->setProperty ("request_id", "progress-test");
    object->setProperty ("phase", "optimizing");
    object->setProperty ("step", step);
    object->setProperty ("total_steps", totalSteps);
    if (! includeParameters)
        object->removeProperty ("parameters");
    return juce::JSON::toString (result);
}
} // namespace

int main()
{
    using namespace semantifx::protocol;
    static_assert (canonicalParameterNames.size() == 18);
    assert (juce::String { canonicalParameterNames.front() } == "low_shelf_gain_db");
    assert (juce::String { canonicalParameterNames.back() } == "high_shelf_q_factor");

    AnalysisResult parsed;
    assert (parseAnalysisResult (makeResult(), parsed).wasOk());
    assert (parsed.requestId == "result-test");
    for (size_t index = 0; index < parsed.values.size(); ++index)
        assert (std::abs (parsed.values[index] - static_cast<double> (index)) < 1.0e-12);

    assert (parseAnalysisResult ("not json", parsed).failed());
    assert (parseAnalysisResult (makeResult (Mutation::missing), parsed).failed());
    assert (parseAnalysisResult (makeResult (Mutation::unknown), parsed).failed());
    assert (parseAnalysisResult (makeResult (Mutation::nonNumeric), parsed).failed());

    auto wrongVersion = makeResult().replace ("\"protocol_version\": \"1\"", "\"protocol_version\": \"2\"");
    assert (parseAnalysisResult (wrongVersion, parsed).failed());

    AnalysisProgress progress;
    assert (parseAnalysisProgress (makeProgress(), progress).wasOk());
    assert (progress.requestId == "progress-test");
    assert (progress.phase == "optimizing");
    assert (progress.step == 17);
    assert (progress.totalSteps == 600);
    assert (progress.hasParameters);
    for (size_t index = 0; index < progress.preview.values.size(); ++index)
        assert (std::abs (progress.preview.values[index] - static_cast<double> (index)) < 1.0e-12);

    assert (parseAnalysisProgress (makeProgress (0, 600, false), progress).wasOk());
    assert (! progress.hasParameters);
    assert (parseAnalysisProgress (makeProgress (601, 600), progress).failed());

    auto hugeStep = makeProgress().replace ("\"step\": 17", "\"step\": 4294967297");
    assert (parseAnalysisProgress (hugeStep, progress).failed());

    juce::String cancelledRequest;
    assert (parseAnalysisCancellation (
        R"({"protocol_version":"1","type":"analysis_cancelled","request_id":"cancel-test"})",
        cancelledRequest).wasOk());
    assert (cancelledRequest == "cancel-test");
    assert (parseAnalysisCancellation (makeResult(), cancelledRequest).failed());

    std::cout << "All Semantifx analysis-result tests passed\n";
}
