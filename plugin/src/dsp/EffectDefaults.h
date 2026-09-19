#pragma once

#include "../generated/EffectParameters.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace semantifx::dsp
{
template <size_t Count>
std::array<float, Count> defaultsFor (std::string_view effectType)
{
    for (const auto& type : protocol::effectTypes)
    {
        if (type.name != effectType)
            continue;
        if (type.parameterCount != Count)
            throw std::logic_error ("Effect parameter count does not match its implementation");

        std::array<float, Count> defaults {};
        for (size_t index = 0; index < Count; ++index)
            defaults[index] = protocol::parameters[type.firstParameter + index].defaultValue;
        return defaults;
    }

    throw std::invalid_argument ("Unknown effect type");
}
} // namespace semantifx::dsp
