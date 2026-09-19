#include "AnalysisResult.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
using namespace semantifx;
using namespace semantifx::protocol;

ExpectedChain expectedChain()
{
    return { { { "eq-1", 0 }, { "distortion-1", 1 } } };
}

juce::var parameterObject (int typeIndex, bool wrongUnit = false)
{
    const auto& type = protocol::effectTypes[static_cast<size_t> (typeIndex)];
    auto object = juce::var { new juce::DynamicObject {} };
    for (size_t index = 0; index < type.parameterCount; ++index)
    {
        const auto& spec = protocol::parameters[type.firstParameter + index];
        object.getDynamicObject()->setProperty (spec.name, spec.defaultValue);
    }
    juce::ignoreUnused (wrongUnit);
    return object;
}

juce::String makeAnalysis (bool wrongOrder = false, bool missingInstance = false,
                           bool wrongUnit = false, bool progress = false,
                           bool includeParameters = true)
{
    const auto expected = expectedChain();
    auto root = juce::var { new juce::DynamicObject {} };
    auto* object = root.getDynamicObject();
    object->setProperty ("type", progress ? "analysis_progress" : "analysis_result");
    object->setProperty ("request_id", progress ? "progress-test" : "analysis-test");
    object->setProperty ("status", "ok");
    object->setProperty ("phase", "optimizing");
    object->setProperty ("step", 4);
    object->setProperty ("total_steps", 10);
    juce::Array<juce::var> order;
    for (const auto& item : expected.items)
        order.add (item.first);
    if (wrongOrder)
        order.swap (0, 1);
    object->setProperty ("chain_order", order);

    juce::Array<juce::var> entries;
    for (size_t instanceIndex = 0; instanceIndex < expected.items.size(); ++instanceIndex)
    {
        const auto typeIndex = expected.items[instanceIndex].second;
        const auto& type = protocol::effectTypes[static_cast<size_t> (typeIndex)];
        for (size_t parameterIndex = 0; parameterIndex < type.parameterCount; ++parameterIndex)
        {
            const auto& spec = protocol::parameters[type.firstParameter + parameterIndex];
            auto entry = juce::var { new juce::DynamicObject {} };
            auto* entryObject = entry.getDynamicObject();
            entryObject->setProperty ("effect_instance_id",
                                      missingInstance && instanceIndex == 0 ? "other-1"
                                                                            : expected.items[instanceIndex].first);
            entryObject->setProperty ("effect", type.name);
            entryObject->setProperty ("name", spec.name);
            entryObject->setProperty ("value", static_cast<double> (spec.defaultValue));
            entryObject->setProperty ("unit", wrongUnit && instanceIndex == 0 && parameterIndex == 0
                                                  ? "Hz" : spec.unit);
            entries.add (entry);
        }
    }
    if (includeParameters)
        object->setProperty ("parameters", entries);
    return juce::JSON::toString (root);
}

juce::String makeAssembly (bool unknownSlot = false)
{
    auto root = juce::var { new juce::DynamicObject {} };
    auto* object = root.getDynamicObject();
    object->setProperty ("type", "assembly_result");
    object->setProperty ("request_id", "assembly-test");
    object->setProperty ("status", "ok");
    juce::Array<juce::var> chain;
    for (const auto& item : expectedChain().items)
    {
        auto chainItem = juce::var { new juce::DynamicObject {} };
        auto* chainObject = chainItem.getDynamicObject();
        chainObject->setProperty ("instance_id", unknownSlot ? "chorus-1" : item.first);
        chainObject->setProperty ("effect_type", protocol::effectTypes[static_cast<size_t> (item.second)].name);
        chainObject->setProperty ("parameters", parameterObject (item.second));
        chain.add (chainItem);
    }
    object->setProperty ("chain", chain);
    juce::Array<juce::var> warnings;
    warnings.add ("test warning");
    object->setProperty ("warnings", warnings);
    return juce::JSON::toString (root);
}
} // namespace

int main()
{
    using namespace semantifx::protocol;
    AnalysisResult result;
    const auto expected = expectedChain();
    assert (parseAnalysisResult (makeAnalysis(), expected, result).wasOk());
    assert (result.requestId == "analysis-test");
    assert (result.instances.size() == 2);
    assert (result.instances[0].instanceId == "eq-1");
    assert (result.instances[0].effectTypeIndex == 0);
    assert (result.instances[0].values.size() == 18);
    assert (std::abs (result.instances[1].values[0]) < 1.0e-12);
    assert (parseAnalysisResult ("not json", expected, result).failed());
    assert (parseAnalysisResult (makeAnalysis (true), expected, result).failed());
    assert (parseAnalysisResult (makeAnalysis (false, true), expected, result).failed());
    assert (parseAnalysisResult (makeAnalysis (false, false, true), expected, result).failed());
    assert (parseAnalysisResult (makeAnalysis (false, false, false, false, false), expected, result).failed());

    AnalysisProgress progress;
    assert (parseAnalysisProgress (makeAnalysis (false, false, false, true), expected, progress).wasOk());
    assert (progress.requestId == "progress-test");
    assert (progress.step == 4 && progress.totalSteps == 10 && progress.hasParameters);
    assert (progress.instances.size() == 2);
    assert (parseAnalysisProgress (makeAnalysis (false, false, false, true, false), expected, progress).wasOk());
    assert (! progress.hasParameters && progress.instances.empty());

    AssemblyResult assembly;
    assert (parseAssemblyResult (makeAssembly(), assembly).wasOk());
    assert (assembly.chain.size() == 2 && assembly.warnings.size() == 1);
    assert (parseAssemblyResult (makeAssembly (true), assembly).failed());

    ErrorResult error;
    const auto errorJson = juce::String {
        R"({"type":"assembly_result","request_id":"assembly-test","status":"error","error":{"code":"assembly_unavailable","message":"No planner","remediation":["Set TYPESAFE_API_KEY.","Retry with mock."]}})" };
    assert (parseErrorResult (errorJson, error).wasOk());
    assert (error.code == "assembly_unavailable");
    assert (error.remediation[0] == "Set TYPESAFE_API_KEY.");

    juce::String cancelled;
    assert (parseAnalysisCancellation (R"({"type":"analysis_cancelled","request_id":"cancel-test"})",
                                       cancelled).wasOk());
    assert (cancelled == "cancel-test");
    std::cout << "All SemantiFX analysis-result tests passed\n";
}
