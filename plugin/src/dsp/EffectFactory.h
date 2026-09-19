#pragma once

#include "Effect.h"
#include "ReverbEffect.h"

#include <memory>
#include <string_view>

namespace semantifx::dsp
{
std::unique_ptr<Effect> makeEffect (std::string_view effectType,
                                    const ReverbResources& reverbResources);
} // namespace semantifx::dsp
