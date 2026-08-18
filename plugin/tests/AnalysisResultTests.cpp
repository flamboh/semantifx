#include "AnalysisResult.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
enum class Mutation { none, missing, unknown, nonNumeric, reordered };

juce::String makeResult (
    Mutation mutation = Mutation::none,
    semantifx::protocol::EffectOrder effectOrder = semantifx::protocol::EffectOrder::eqThenDistortion)
{
    auto root = juce::var { new juce::DynamicObject {} };
    auto* rootObject = root.getDynamicObject();
    rootObject->setProperty ("protocol_version", "2");
    rootObject->setProperty ("type", "analysis_result");
    rootObject->setProperty ("request_id", "chain-result-test");
    rootObject->setProperty ("status", "ok");
    juce::Array<juce::var> order;
    if (effectOrder == semantifx::protocol::EffectOrder::distortionThenEq)
    {
        order.add ("distortion-1");
        order.add ("eq-1");
    }
    else
    {
        order.add ("eq-1");
        order.add ("distortion-1");
    }
    rootObject->setProperty ("chain_order", order);

    juce::Array<juce::var> entries;
    const auto count = semantifx::protocol::chainParameterSpecs.size()
                     - (mutation == Mutation::missing ? 1 : 0);
    for (size_t index = 0; index < count; ++index)
    {
        const auto& spec = semantifx::protocol::chainParameterSpecs[index];
        auto entry = juce::var { new juce::DynamicObject {} };
        auto* object = entry.getDynamicObject();
        object->setProperty ("effect_instance_id", spec.effectInstanceId);
        object->setProperty ("effect", spec.effectType);
        object->setProperty ("name", mutation == Mutation::unknown && index == 0
                                          ? juce::var { "mystery_gain" } : juce::var { spec.name });
        object->setProperty ("value", mutation == Mutation::nonNumeric && index == 0
                                          ? juce::var { "loud" } : juce::var { static_cast<double> (index) });
        object->setProperty ("unit", spec.unit);
        entries.add (entry);
    }
    if (mutation == Mutation::reordered)
        entries.swap (0, 1);
    rootObject->setProperty ("parameters", entries);
    return juce::JSON::toString (root);
}

juce::String makeProgress (int step = 4, int totalSteps = 10, bool includeParameters = true)
{
    auto result = juce::JSON::parse (makeResult());
    auto* object = result.getDynamicObject();
    object->setProperty ("type", "analysis_progress");
    object->removeProperty ("status");
    object->setProperty ("request_id", "chain-progress-test");
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
    static_assert (eqParameterNames.size() == 18);
    static_assert (chainParameterSpecs.size() == 22);
    assert (juce::String { eqParameterNames.front() } == "low_shelf_gain_db");
    assert (juce::String { eqParameterNames.back() } == "high_shelf_q_factor");

    AnalysisResult parsed;
    assert (parseAnalysisResult (makeResult(), parsed).wasOk());
    assert (parsed.requestId == "chain-result-test");
    assert (parsed.effectOrder == EffectOrder::eqThenDistortion);
    for (size_t index = 0; index < chainParameterSpecs.size(); ++index)
        assert (std::abs (parsed.values[index] - static_cast<double> (index)) < 1.0e-12);

    assert (parseAnalysisResult ("not json", parsed).failed());
    assert (parseAnalysisResult (makeResult (Mutation::missing), parsed).failed());
    assert (parseAnalysisResult (makeResult (Mutation::unknown), parsed).failed());
    assert (parseAnalysisResult (makeResult (Mutation::nonNumeric), parsed).failed());
    assert (parseAnalysisResult (makeResult (Mutation::reordered), parsed).failed());
    assert (parseAnalysisResult (
        makeResult (Mutation::none, EffectOrder::distortionThenEq), parsed).wasOk());
    assert (parsed.effectOrder == EffectOrder::distortionThenEq);
    AnalysisProgress progress;
    assert (parseAnalysisProgress (makeProgress(), progress).wasOk());
    assert (progress.requestId == "chain-progress-test");
    assert (progress.phase == "optimizing");
    assert (progress.step == 4);
    assert (progress.totalSteps == 10);
    assert (progress.hasParameters);
    assert (progress.preview.effectOrder == EffectOrder::eqThenDistortion);
    for (size_t index = 0; index < chainParameterSpecs.size(); ++index)
        assert (std::abs (progress.preview.values[index] - static_cast<double> (index)) < 1.0e-12);

    assert (parseAnalysisProgress (makeProgress (0, 10, false), progress).wasOk());
    assert (! progress.hasParameters);
    assert (parseAnalysisProgress (makeProgress (11, 10), progress).failed());

    juce::String cancelledRequest;
    assert (parseAnalysisCancellation (
        R"({"protocol_version":"2","type":"analysis_cancelled","request_id":"cancel-test"})",
        cancelledRequest).wasOk());
    assert (cancelledRequest == "cancel-test");
    assert (parseAnalysisCancellation (makeResult(), cancelledRequest).failed());

    std::cout << "All Semantifx analysis-result tests passed\n";
}
