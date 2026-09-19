#pragma once

#include "generated/EffectParameters.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace semantifx::slots
{
inline constexpr int kSlotsPerType = 2;
inline constexpr int kSlotCount = protocol::kEffectTypeCount * kSlotsPerType;

struct Slot
{
    int typeIndex {};
    int ordinal {};

    friend constexpr bool operator== (Slot left, Slot right) noexcept
    {
        return left.typeIndex == right.typeIndex && left.ordinal == right.ordinal;
    }
};

inline constexpr bool isValid (Slot slot) noexcept
{
    return slot.typeIndex >= 0 && slot.typeIndex < protocol::kEffectTypeCount
        && slot.ordinal >= 1 && slot.ordinal <= kSlotsPerType;
}

inline constexpr int slotIndex (Slot slot) noexcept
{
    return slot.typeIndex * kSlotsPerType + slot.ordinal - 1;
}

inline constexpr Slot slotAt (int index) noexcept
{
    return { index / kSlotsPerType, index % kSlotsPerType + 1 };
}

inline std::string slotName (Slot slot)
{
    if (! isValid (slot))
        return {};
    return std::string { protocol::effectTypes[static_cast<size_t> (slot.typeIndex)].name }
         + "-" + std::to_string (slot.ordinal);
}

inline std::optional<Slot> parseSlotName (std::string_view value)
{
    for (int typeIndex = 0; typeIndex < protocol::kEffectTypeCount; ++typeIndex)
    {
        const auto typeName = std::string_view {
            protocol::effectTypes[static_cast<size_t> (typeIndex)].name };
        const auto prefix = std::string { typeName } + "-";
        if (value.size() != prefix.size() + 1 || value.substr (0, prefix.size()) != prefix)
            continue;
        const auto ordinal = static_cast<int> (value.back() - '0');
        if (ordinal >= 1 && ordinal <= kSlotsPerType)
            return Slot { typeIndex, ordinal };
    }
    return std::nullopt;
}

inline std::string parameterId (Slot slot, int parameterIndexWithinType)
{
    if (! isValid (slot))
        return {};
    const auto& type = protocol::effectTypes[static_cast<size_t> (slot.typeIndex)];
    if (parameterIndexWithinType < 0
        || parameterIndexWithinType >= static_cast<int> (type.parameterCount))
        return {};
    const auto& parameter = protocol::parameters[type.firstParameter
                                                 + static_cast<size_t> (parameterIndexWithinType)];
    return std::string { type.name } + std::to_string (slot.ordinal) + "_" + parameter.name;
}

inline std::string bypassId (Slot slot)
{
    if (! isValid (slot))
        return {};
    return std::string { protocol::effectTypes[static_cast<size_t> (slot.typeIndex)].name }
         + std::to_string (slot.ordinal) + "_bypass";
}

inline std::string slotTitle (Slot slot)
{
    if (! isValid (slot))
        return {};
    std::string title { protocol::effectTypes[static_cast<size_t> (slot.typeIndex)].name };
    if (title == "eq")
        title = "EQ";
    else if (! title.empty())
        title[0] = static_cast<char> (title[0] - ('a' - 'A'));
    title += " " + std::to_string (slot.ordinal);
    return title;
}

struct Composition
{
    std::vector<Slot> items;

    friend bool operator== (const Composition& left, const Composition& right) noexcept
    {
        return left.items == right.items;
    }
};

inline std::optional<Composition> parseComposition (std::string_view value)
{
    Composition result;
    if (value.empty())
        return result;

    size_t start = 0;
    while (start <= value.size())
    {
        const auto end = value.find (',', start);
        const auto token = value.substr (start, end == std::string_view::npos
                                                   ? value.size() - start : end - start);
        const auto parsed = parseSlotName (token);
        if (! parsed.has_value())
            return std::nullopt;
        for (const auto existing : result.items)
            if (existing == *parsed)
                return std::nullopt;
        result.items.push_back (*parsed);
        if (result.items.size() > static_cast<size_t> (kSlotCount)
            || result.items.size() > 8)
            return std::nullopt;
        if (end == std::string_view::npos)
            break;
        start = end + 1;
        if (start == value.size())
            return std::nullopt;
    }
    return result;
}

inline std::string serializeComposition (const Composition& composition)
{
    std::string result;
    for (const auto slot : composition.items)
    {
        if (! result.empty())
            result += ',';
        result += slotName (slot);
    }
    return result;
}

inline std::uint32_t packComposition (const Composition& composition) noexcept
{
    std::uint32_t packed {};
    const auto count = std::min (composition.items.size(), static_cast<size_t> (8));
    for (size_t index = 0; index < count; ++index)
    {
        const auto slot = composition.items[index];
        const auto encoded = isValid (slot) ? static_cast<std::uint32_t> (slotIndex (slot) + 1) : 0u;
        packed |= (encoded & 0x0fu) << (index * 4);
    }
    return packed;
}

inline Composition unpackComposition (std::uint32_t packed)
{
    Composition result;
    for (int index = 0; index < 8; ++index)
    {
        const auto encoded = (packed >> (index * 4)) & 0x0fu;
        if (encoded == 0)
            break;
        if (encoded <= static_cast<std::uint32_t> (kSlotCount))
            result.items.push_back (slotAt (static_cast<int> (encoded - 1)));
    }
    return result;
}

inline std::optional<Slot> freeSlotFor (int typeIndex, const Composition& composition)
{
    if (typeIndex < 0 || typeIndex >= protocol::kEffectTypeCount)
        return std::nullopt;
    for (int ordinal = 1; ordinal <= kSlotsPerType; ++ordinal)
    {
        const Slot candidate { typeIndex, ordinal };
        bool used = false;
        for (const auto item : composition.items)
            used = used || item == candidate;
        if (! used)
            return candidate;
    }
    return std::nullopt;
}
} // namespace semantifx::slots
