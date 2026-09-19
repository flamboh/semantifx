#include "EffectFactory.h"

#include "CompressorEffect.h"
#include "DistortionEffect.h"
#include "EqEffect.h"

#include "../generated/EffectParameters.h"

#include <string>
#include <stdexcept>
#include <utility>

namespace semantifx::dsp
{
std::unique_ptr<Effect> makeEffect (std::string_view effectType,
                                    const ReverbResources& reverbResources)
{
    static_assert (semantifx::protocol::kEffectTypeCount == 4,
                   "add a factory case");

    if (effectType == "eq")
        return std::make_unique<EqEffect>();
    if (effectType == "distortion")
        return std::make_unique<DistortionEffect>();
    if (effectType == "compressor")
        return std::make_unique<CompressorEffect>();
    if (effectType == "reverb")
    {
        auto convolver = reverbResources.convolverFactory != nullptr
            ? reverbResources.convolverFactory()
            : std::make_unique<DirectConvolver>();
        return std::make_unique<ReverbEffect> (std::move (convolver),
                                               reverbResources.noiseTablePath);
    }
    throw std::invalid_argument ("Unknown SemantiFX effect type: " + std::string (effectType));
}
} // namespace semantifx::dsp
