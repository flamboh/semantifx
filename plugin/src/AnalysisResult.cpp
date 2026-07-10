#include "AnalysisResult.h"

#include <algorithm>
#include <cmath>

namespace semantifx::protocol
{
namespace
{
juce::Result requireStringProperty (juce::DynamicObject& object, const juce::Identifier& property,
                                    const juce::String& expected)
{
    const auto value = object.getProperty (property);
    if (! value.isString() || value.toString() != expected)
        return juce::Result::fail ("Expected \"" + property.toString() + "\" to be \"" + expected + "\".");
    return juce::Result::ok();
}

bool isNumeric (const juce::var& value) noexcept
{
    return value.isInt() || value.isInt64() || value.isDouble();
}

juce::String expectedUnit (const juce::String& name)
{
    if (name.endsWith ("_gain_db"))
        return "dB";
    if (name.endsWith ("_freq"))
        return "Hz";
    return "ratio";
}
} // namespace

juce::Result parseAnalysisResult (const juce::String& json, AnalysisResult& destination)
{
    juce::var root;
    if (const auto parsed = juce::JSON::parse (json, root); parsed.failed())
        return juce::Result::fail ("Invalid JSON: " + parsed.getErrorMessage());

    auto* object = root.getDynamicObject();
    if (object == nullptr)
        return juce::Result::fail ("Analysis result must be a JSON object.");

    for (const auto& expectation : std::array<std::pair<juce::Identifier, juce::String>, 3> {{
             { "protocol_version", "1" }, { "type", "analysis_result" }, { "status", "ok" } }})
        if (const auto result = requireStringProperty (*object, expectation.first, expectation.second); result.failed())
            return result;

    const auto parameters = object->getProperty ("parameters");
    const auto* array = parameters.getArray();
    if (array == nullptr)
        return juce::Result::fail ("Expected \"parameters\" to be an array.");

    std::array<bool, canonicalParameterNames.size()> found {};
    for (const auto& entry : *array)
    {
        auto* parameter = entry.getDynamicObject();
        if (parameter == nullptr)
            return juce::Result::fail ("Every parameter must be a JSON object.");

        const auto effect = parameter->getProperty ("effect");
        const auto nameValue = parameter->getProperty ("name");
        const auto numericValue = parameter->getProperty ("value");
        const auto unit = parameter->getProperty ("unit");
        if (! effect.isString() || effect.toString() != "eq")
            return juce::Result::fail ("Every imported parameter must have effect \"eq\".");
        if (! nameValue.isString())
            return juce::Result::fail ("Every imported parameter must have a string name.");
        if (! isNumeric (numericValue) || ! std::isfinite (static_cast<double> (numericValue)))
            return juce::Result::fail ("Parameter \"" + nameValue.toString() + "\" must have a finite numeric value.");

        const auto name = nameValue.toString();
        const auto iterator = std::find_if (canonicalParameterNames.begin(), canonicalParameterNames.end(),
                                            [&name] (const char* candidate) { return name == candidate; });
        if (iterator == canonicalParameterNames.end())
            return juce::Result::fail ("Unknown EQ parameter \"" + name + "\".");

        const auto index = static_cast<size_t> (std::distance (canonicalParameterNames.begin(), iterator));
        if (found[index])
            return juce::Result::fail ("Duplicate EQ parameter \"" + name + "\".");
        if (! unit.isString() || unit.toString() != expectedUnit (name))
            return juce::Result::fail ("Parameter \"" + name + "\" has the wrong unit.");

        destination.values[index] = static_cast<double> (numericValue);
        found[index] = true;
    }

    for (size_t index = 0; index < found.size(); ++index)
        if (! found[index])
            return juce::Result::fail ("Missing EQ parameter \""
                                       + juce::String { canonicalParameterNames[index] } + "\".");

    return juce::Result::ok();
}
} // namespace semantifx::protocol
