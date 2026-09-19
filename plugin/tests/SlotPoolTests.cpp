#include "SlotPool.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace semantifx::slots;

    assert (kSlotsPerType == 2);
    assert (kSlotCount == 8);
    assert (slotIndex ({ 0, 1 }) == 0);
    assert ((slotAt (7) == Slot { 3, 2 }));
    assert (slotName ({ 0, 1 }) == "eq-1");
    assert (slotName ({ 3, 2 }) == "reverb-2");
    assert ((parseSlotName ("eq-2") == Slot { 0, 2 }));
    assert (! parseSlotName ("eq-3").has_value());
    assert (parameterId ({ 0, 1 }, 0) == "eq1_low_shelf_gain_db");
    assert (parameterId ({ 3, 2 }, 2) == "reverb2_mix");
    assert (bypassId ({ 1, 2 }) == "distortion2_bypass");
    assert (slotTitle ({ 0, 1 }) == "EQ 1");
    assert (slotTitle ({ 2, 2 }) == "Compressor 2");

    const auto parsed = parseComposition ("eq-1,reverb-2,eq-2");
    assert (parsed.has_value());
    assert (serializeComposition (*parsed) == "eq-1,reverb-2,eq-2");
    assert (unpackComposition (packComposition (*parsed)) == *parsed);
    assert (! parseComposition ("eq-1,eq-1").has_value());
    assert (! parseComposition ("unknown-1").has_value());
    assert (! parseComposition ("eq-1,").has_value());

    const auto freeEq = freeSlotFor (0, Composition { { { 0, 1 } } });
    assert (freeEq.has_value() && (*freeEq == Slot { 0, 2 }));
    assert (! freeSlotFor (3, Composition { { { 3, 1 }, { 3, 2 } } }).has_value());

    std::cout << "All SemantiFX slot-pool tests passed\n";
}
