#include "AnalysisResult.h"

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

bool isInteger (const juce::var& value) noexcept
{
    return value.isInt() || value.isInt64();
}

juce::Result parseEffectOrder (const juce::var& value, EffectOrder& destination)
{
    const auto* order = value.getArray();
    if (order == nullptr || order->size() != 2
        || ! (*order)[0].isString() || ! (*order)[1].isString())
        return juce::Result::fail ("Expected chain_order to contain two effect instance IDs.");

    const auto first = (*order)[0].toString();
    const auto second = (*order)[1].toString();
    if (first == "eq-1" && second == "distortion-1")
        destination = EffectOrder::eqThenDistortion;
    else if (first == "distortion-1" && second == "eq-1")
        destination = EffectOrder::distortionThenEq;
    else
        return juce::Result::fail ("chain_order must contain eq-1 and distortion-1 exactly once.");
    return juce::Result::ok();
}

juce::Result parseParameters (const juce::var& parameters, AnalysisResult& destination)
{
    const auto* array = parameters.getArray();
    if (array == nullptr)
        return juce::Result::fail ("Expected \"parameters\" to be an array.");

    if (array->size() != static_cast<int> (chainParameterSpecs.size()))
        return juce::Result::fail ("Protocol-v2 results must contain exactly "
                                   + juce::String { chainParameterSpecs.size() } + " parameters.");

    for (size_t index = 0; index < chainParameterSpecs.size(); ++index)
    {
        const auto& entry = (*array)[static_cast<int> (index)];
        auto* parameter = entry.getDynamicObject();
        if (parameter == nullptr)
            return juce::Result::fail ("Every parameter must be a JSON object.");

        const auto instanceValue = parameter->getProperty ("effect_instance_id");
        const auto effectValue = parameter->getProperty ("effect");
        const auto nameValue = parameter->getProperty ("name");
        const auto numericValue = parameter->getProperty ("value");
        const auto unitValue = parameter->getProperty ("unit");
        if (! instanceValue.isString() || ! effectValue.isString() || ! nameValue.isString())
            return juce::Result::fail ("Every protocol-v2 parameter needs string instance, effect, and name fields.");
        if (! isNumeric (numericValue) || ! std::isfinite (static_cast<double> (numericValue)))
            return juce::Result::fail ("Parameter \"" + nameValue.toString() + "\" must have a finite numeric value.");

        const auto instance = instanceValue.toString();
        const auto effect = effectValue.toString();
        const auto name = nameValue.toString();
        const auto& expected = chainParameterSpecs[index];
        if (instance != expected.effectInstanceId || effect != expected.effectType || name != expected.name)
            return juce::Result::fail ("Expected chain parameter \""
                                       + juce::String { expected.effectInstanceId } + ":" + expected.name
                                       + "\" at index " + juce::String { index } + ".");
        if (! unitValue.isString() || unitValue.toString() != expected.unit)
            return juce::Result::fail ("Parameter \"" + instance + ":" + name + "\" has the wrong unit.");

        destination.values[index] = static_cast<double> (numericValue);
    }

    return juce::Result::ok();
}

juce::Result parseVersion (juce::DynamicObject& object)
{
    const auto version = object.getProperty ("protocol_version");
    if (! version.isString() || version.toString() != "2")
        return juce::Result::fail ("Expected \"protocol_version\" to be \"2\".");
    return juce::Result::ok();
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

    destination = {};
    if (const auto result = parseVersion (*object); result.failed())
        return result;
    for (const auto& expectation : std::array<std::pair<juce::Identifier, juce::String>, 2> {{
             { "type", "analysis_result" }, { "status", "ok" } }})
        if (const auto result = requireStringProperty (*object, expectation.first, expectation.second); result.failed())
            return result;

    const auto requestId = object->getProperty ("request_id");
    if (! requestId.isString() || requestId.toString().trim().isEmpty())
        return juce::Result::fail ("Analysis result request_id must be a non-empty string.");
    destination.requestId = requestId.toString();
    if (const auto result = parseEffectOrder (object->getProperty ("chain_order"), destination.effectOrder);
        result.failed())
        return result;
    return parseParameters (object->getProperty ("parameters"), destination);
}

juce::Result parseAnalysisProgress (const juce::String& json, AnalysisProgress& destination)
{
    juce::var root;
    if (const auto parsed = juce::JSON::parse (json, root); parsed.failed())
        return juce::Result::fail ("Invalid JSON: " + parsed.getErrorMessage());

    auto* object = root.getDynamicObject();
    if (object == nullptr)
        return juce::Result::fail ("Analysis progress must be a JSON object.");

    destination = {};
    if (const auto result = parseVersion (*object); result.failed())
        return result;
    if (const auto result = requireStringProperty (*object, "type", "analysis_progress"); result.failed())
        return result;

    const auto phase = object->getProperty ("phase");
    const auto requestId = object->getProperty ("request_id");
    const auto step = object->getProperty ("step");
    const auto totalSteps = object->getProperty ("total_steps");
    if (! requestId.isString() || requestId.toString().trim().isEmpty())
        return juce::Result::fail ("Analysis progress request_id must be a non-empty string.");
    if (! phase.isString() || phase.toString().trim().isEmpty())
        return juce::Result::fail ("Analysis progress phase must be a non-empty string.");
    if (! isInteger (step) || ! isInteger (totalSteps))
        return juce::Result::fail ("Analysis progress steps must be integers.");

    const auto stepValue = static_cast<juce::int64> (step);
    const auto totalStepsValue = static_cast<juce::int64> (totalSteps);
    if (totalStepsValue < 1 || totalStepsValue > 10000
        || stepValue < 0 || stepValue > totalStepsValue)
        return juce::Result::fail ("Analysis progress steps are out of range.");

    destination.requestId = requestId.toString();
    destination.phase = phase.toString();
    destination.step = static_cast<int> (stepValue);
    destination.totalSteps = static_cast<int> (totalStepsValue);
    if (const auto result = parseEffectOrder (object->getProperty ("chain_order"),
                                              destination.preview.effectOrder);
        result.failed())
        return result;

    const auto parameters = object->getProperty ("parameters");
    destination.hasParameters = ! parameters.isVoid();
    if (destination.hasParameters)
        return parseParameters (parameters, destination.preview);
    return juce::Result::ok();
}

juce::Result parseAnalysisCancellation (const juce::String& json, juce::String& requestId)
{
    juce::var root;
    if (const auto parsed = juce::JSON::parse (json, root); parsed.failed())
        return juce::Result::fail ("Invalid JSON: " + parsed.getErrorMessage());

    auto* object = root.getDynamicObject();
    if (object == nullptr)
        return juce::Result::fail ("Analysis cancellation must be a JSON object.");
    if (const auto result = parseVersion (*object); result.failed())
        return result;
    if (const auto result = requireStringProperty (*object, "type", "analysis_cancelled"); result.failed())
        return result;

    const auto value = object->getProperty ("request_id");
    if (! value.isString() || value.toString().trim().isEmpty())
        return juce::Result::fail ("Analysis cancellation request_id must be a non-empty string.");
    requestId = value.toString();
    return juce::Result::ok();
}
} // namespace semantifx::protocol
