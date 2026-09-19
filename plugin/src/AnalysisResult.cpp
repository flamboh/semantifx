#include "AnalysisResult.h"

#include <cmath>

namespace semantifx::protocol
{
namespace
{
using Object = juce::DynamicObject;

juce::Result parseJsonObject (const juce::String& json, juce::var& root, Object*& object,
                             const char* description)
{
    if (const auto parsed = juce::JSON::parse (json, root); parsed.failed())
        return juce::Result::fail ("Invalid JSON: " + parsed.getErrorMessage());
    object = root.getDynamicObject();
    if (object == nullptr)
        return juce::Result::fail (juce::String { description } + " must be a JSON object.");
    return juce::Result::ok();
}

juce::Result requireString (const Object& object, const char* property, const char* expected)
{
    const auto value = object.getProperty (property);
    if (! value.isString() || value.toString() != expected)
        return juce::Result::fail ("Expected \"" + juce::String { property }
                                   + "\" to be \"" + expected + "\".");
    return juce::Result::ok();
}

bool numeric (const juce::var& value) noexcept
{
    return (value.isInt() || value.isInt64() || value.isDouble())
        && std::isfinite (static_cast<double> (value));
}

bool integer (const juce::var& value) noexcept
{
    return value.isInt() || value.isInt64();
}

juce::Result readRequestId (const Object& object, juce::String& destination, bool allowNull = false)
{
    const auto value = object.getProperty ("request_id");
    if (allowNull && value.isVoid())
    {
        destination.clear();
        return juce::Result::ok();
    }
    if (! value.isString() || value.toString().trim().isEmpty())
        return juce::Result::fail ("request_id must be a non-empty string.");
    destination = value.toString();
    return juce::Result::ok();
}

juce::Result chainOrder (const Object& object, const ExpectedChain& expected)
{
    const auto* order = object.getProperty ("chain_order").getArray();
    if (order == nullptr || order->size() != static_cast<int> (expected.items.size()))
        return juce::Result::fail ("chain_order does not match the expected effect chain.");
    for (size_t index = 0; index < expected.items.size(); ++index)
    {
        const auto& value = (*order)[static_cast<int> (index)];
        if (! value.isString() || value.toString() != expected.items[index].first)
            return juce::Result::fail ("chain_order does not match the expected effect chain.");
    }
    return juce::Result::ok();
}

juce::Result instanceParameters (const juce::Array<juce::var>& entries, size_t& offset,
                                 const juce::String& instanceId, int typeIndex,
                                 InstanceValues& destination)
{
    if (typeIndex < 0 || typeIndex >= protocol::kEffectTypeCount)
        return juce::Result::fail ("Unknown effect type index.");
    const auto& type = protocol::effectTypes[static_cast<size_t> (typeIndex)];
    destination.instanceId = instanceId;
    destination.effectTypeIndex = typeIndex;
    destination.values.clear();
    destination.values.reserve (type.parameterCount);

    for (size_t parameterIndex = 0; parameterIndex < type.parameterCount; ++parameterIndex)
    {
        if (offset >= static_cast<size_t> (entries.size()))
            return juce::Result::fail ("The result is missing a chain parameter.");
        auto* parameter = entries[static_cast<int> (offset++)].getDynamicObject();
        if (parameter == nullptr)
            return juce::Result::fail ("Every chain parameter must be a JSON object.");

        const auto instance = parameter->getProperty ("effect_instance_id");
        const auto effect = parameter->getProperty ("effect");
        const auto name = parameter->getProperty ("name");
        const auto unit = parameter->getProperty ("unit");
        const auto value = parameter->getProperty ("value");
        const auto& spec = protocol::parameters[type.firstParameter + parameterIndex];
        if (! instance.isString() || instance.toString() != instanceId
            || ! effect.isString() || effect.toString() != type.name
            || ! name.isString() || name.toString() != spec.name
            || ! unit.isString() || unit.toString() != spec.unit)
            return juce::Result::fail ("A chain parameter has the wrong instance, effect, name, or unit.");
        if (! numeric (value))
            return juce::Result::fail ("Parameter \"" + instanceId + ":" + spec.name
                                       + "\" must have a finite numeric value.");
        destination.values.push_back (static_cast<double> (value));
    }
    return juce::Result::ok();
}

juce::Result parseParameterArray (const juce::var& value, const ExpectedChain& expected,
                                  std::vector<InstanceValues>& destination)
{
    const auto* array = value.getArray();
    if (array == nullptr)
        return juce::Result::fail ("Expected \"parameters\" to be an array.");

    size_t expectedCount = 0;
    for (const auto& item : expected.items)
        expectedCount += protocol::effectTypes[static_cast<size_t> (item.second)].parameterCount;
    if (static_cast<size_t> (array->size()) != expectedCount)
        return juce::Result::fail ("The result does not contain the complete parameter set.");

    destination.clear();
    destination.reserve (expected.items.size());
    size_t offset = 0;
    for (const auto& item : expected.items)
    {
        InstanceValues instance;
        if (const auto result = instanceParameters (*array, offset, item.first, item.second, instance);
            result.failed())
            return result;
        destination.push_back (std::move (instance));
    }
    return juce::Result::ok();
}

juce::Result parseWarnings (const juce::var& value, juce::StringArray& destination)
{
    const auto* array = value.getArray();
    if (array == nullptr)
        return juce::Result::fail ("warnings must be an array.");
    destination.clear();
    for (const auto& item : *array)
    {
        if (! item.isString())
            return juce::Result::fail ("warnings must contain strings.");
        destination.add (item.toString());
    }
    return juce::Result::ok();
}

juce::Result parseAssemblyChain (const juce::var& value, std::vector<InstanceValues>& destination)
{
    const auto* array = value.getArray();
    if (array == nullptr || array->size() > slots::kSlotCount || array->size() > 8)
        return juce::Result::fail ("Assembly chain must contain between zero and eight items.");

    destination.clear();
    for (const auto& item : *array)
    {
        auto* object = item.getDynamicObject();
        if (object == nullptr)
            return juce::Result::fail ("Every assembly chain item must be a JSON object.");
        const auto id = object->getProperty ("instance_id");
        const auto effect = object->getProperty ("effect_type");
        const auto* parametersObject = object->getProperty ("parameters").getDynamicObject();
        if (! id.isString() || ! effect.isString() || parametersObject == nullptr)
            return juce::Result::fail ("Every assembly chain item needs an instance, type, and parameters.");
        const auto slot = slots::parseSlotName (id.toString().toStdString());
        if (! slot.has_value())
            return juce::Result::fail ("Assembly result contains an unknown slot name.");
        const auto& type = protocol::effectTypes[static_cast<size_t> (slot->typeIndex)];
        if (effect.toString() != type.name)
            return juce::Result::fail ("Assembly result effect type does not match its slot name.");
        for (const auto& existing : destination)
            if (existing.instanceId == id.toString())
                return juce::Result::fail ("Assembly result contains a duplicate slot.");

        InstanceValues parsed;
        parsed.instanceId = id.toString();
        parsed.effectTypeIndex = slot->typeIndex;
        parsed.values.reserve (type.parameterCount);
        if (parametersObject->getProperties().size() != static_cast<int> (type.parameterCount))
            return juce::Result::fail ("Assembly result has an incomplete parameter object.");
        for (size_t parameterIndex = 0; parameterIndex < type.parameterCount; ++parameterIndex)
        {
            const auto& spec = protocol::parameters[type.firstParameter + parameterIndex];
            const auto parameter = parametersObject->getProperty (spec.name);
            if (parameter.isVoid() || ! numeric (parameter)
                || static_cast<double> (parameter) < spec.minimum
                || static_cast<double> (parameter) > spec.maximum)
                return juce::Result::fail ("Assembly result is missing a finite parameter value.");
            parsed.values.push_back (static_cast<double> (parameter));
        }
        destination.push_back (std::move (parsed));
    }
    return juce::Result::ok();
}
} // namespace

juce::Result parseAnalysisResult (const juce::String& json, const ExpectedChain& expected,
                                  AnalysisResult& destination)
{
    juce::var root;
    Object* object = nullptr;
    if (const auto result = parseJsonObject (json, root, object, "Analysis result"); result.failed())
        return result;
    if (const auto result = requireString (*object, "type", "analysis_result"); result.failed())
        return result;
    if (const auto result = requireString (*object, "status", "ok"); result.failed())
        return result;
    destination = {};
    if (const auto result = readRequestId (*object, destination.requestId); result.failed())
        return result;
    if (const auto result = chainOrder (*object, expected); result.failed())
        return result;
    if (const auto result = parseParameterArray (object->getProperty ("parameters"), expected,
                                                 destination.instances); result.failed())
        return result;
    return juce::Result::ok();
}

juce::Result parseAnalysisProgress (const juce::String& json, const ExpectedChain& expected,
                                    AnalysisProgress& destination)
{
    juce::var root;
    Object* object = nullptr;
    if (const auto result = parseJsonObject (json, root, object, "Analysis progress"); result.failed())
        return result;
    if (const auto result = requireString (*object, "type", "analysis_progress"); result.failed())
        return result;
    destination = {};
    if (const auto result = readRequestId (*object, destination.requestId); result.failed())
        return result;
    const auto phase = object->getProperty ("phase");
    const auto step = object->getProperty ("step");
    const auto total = object->getProperty ("total_steps");
    if (! phase.isString() || phase.toString().trim().isEmpty() || ! integer (step) || ! integer (total))
        return juce::Result::fail ("Analysis progress has invalid phase or step fields.");
    const auto stepValue = static_cast<juce::int64> (step);
    const auto totalValue = static_cast<juce::int64> (total);
    if (totalValue < 1 || totalValue > 10000 || stepValue < 0 || stepValue > totalValue)
        return juce::Result::fail ("Analysis progress steps are out of range.");
    if (const auto result = chainOrder (*object, expected); result.failed())
        return result;

    destination.phase = phase.toString();
    destination.step = static_cast<int> (stepValue);
    destination.totalSteps = static_cast<int> (totalValue);
    destination.hasParameters = object->hasProperty ("parameters");
    if (destination.hasParameters)
        return parseParameterArray (object->getProperty ("parameters"), expected,
                                    destination.instances);
    return juce::Result::ok();
}

juce::Result parseAssemblyResult (const juce::String& json, AssemblyResult& destination)
{
    juce::var root;
    Object* object = nullptr;
    if (const auto result = parseJsonObject (json, root, object, "Assembly result"); result.failed())
        return result;
    if (const auto result = requireString (*object, "type", "assembly_result"); result.failed())
        return result;
    if (const auto result = requireString (*object, "status", "ok"); result.failed())
        return result;
    destination = {};
    if (const auto result = readRequestId (*object, destination.requestId); result.failed())
        return result;
    if (const auto result = parseAssemblyChain (object->getProperty ("chain"), destination.chain);
        result.failed())
        return result;
    const auto warnings = object->getProperty ("warnings");
    if (warnings.isVoid())
        return juce::Result::fail ("Assembly result is missing warnings.");
    return parseWarnings (warnings, destination.warnings);
}

juce::Result parseErrorResult (const juce::String& json, ErrorResult& destination)
{
    juce::var root;
    Object* object = nullptr;
    if (const auto result = parseJsonObject (json, root, object, "Error result"); result.failed())
        return result;
    const auto type = object->getProperty ("type");
    const auto status = object->getProperty ("status");
    if (! type.isString() || ! status.isString() || status.toString() != "error")
        return juce::Result::fail ("Expected an error result envelope.");
    const auto error = object->getProperty ("error").getDynamicObject();
    if (error == nullptr || ! error->getProperty ("code").isString()
        || ! error->getProperty ("message").isString())
        return juce::Result::fail ("Error result is missing error.code or error.message.");
    const auto remediation = error->getProperty ("remediation");
    if (remediation.isVoid() || remediation.getArray() == nullptr)
        return juce::Result::fail ("Error result is missing error.remediation.");
    destination = {};
    destination.type = type.toString();
    if (const auto result = readRequestId (*object, destination.requestId, true); result.failed())
        return result;
    destination.code = error->getProperty ("code").toString();
    destination.message = error->getProperty ("message").toString();
    return parseWarnings (remediation, destination.remediation);
}

juce::Result parseAnalysisCancellation (const juce::String& json, juce::String& requestId)
{
    juce::var root;
    Object* object = nullptr;
    if (const auto result = parseJsonObject (json, root, object, "Analysis cancellation"); result.failed())
        return result;
    if (const auto result = requireString (*object, "type", "analysis_cancelled"); result.failed())
        return result;
    return readRequestId (*object, requestId);
}
} // namespace semantifx::protocol
