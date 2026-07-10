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
} // namespace

int main()
{
    using namespace semantifx::protocol;
    static_assert (canonicalParameterNames.size() == 18);
    assert (juce::String { canonicalParameterNames.front() } == "low_shelf_gain_db");
    assert (juce::String { canonicalParameterNames.back() } == "high_shelf_q_factor");

    AnalysisResult parsed;
    assert (parseAnalysisResult (makeResult(), parsed).wasOk());
    for (size_t index = 0; index < parsed.values.size(); ++index)
        assert (std::abs (parsed.values[index] - static_cast<double> (index)) < 1.0e-12);

    assert (parseAnalysisResult ("not json", parsed).failed());
    assert (parseAnalysisResult (makeResult (Mutation::missing), parsed).failed());
    assert (parseAnalysisResult (makeResult (Mutation::unknown), parsed).failed());
    assert (parseAnalysisResult (makeResult (Mutation::nonNumeric), parsed).failed());

    auto wrongVersion = makeResult().replace ("\"protocol_version\": \"1\"", "\"protocol_version\": \"2\"");
    assert (parseAnalysisResult (wrongVersion, parsed).failed());

    std::cout << "All Semantifx analysis-result tests passed\n";
}
