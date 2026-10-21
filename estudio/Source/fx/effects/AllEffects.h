#pragma once
#include "fx/NativeFx.h"

namespace racoon::fx
{
// every effect besides the six that came from the old VST family registers itself here
void appendAllEffects (std::vector<FxDef>&);
}
