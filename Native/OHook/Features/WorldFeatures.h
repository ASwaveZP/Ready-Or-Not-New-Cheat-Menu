#pragma once
#include "SDKExt.h"
#include "ReadyornotOverlay.h"
#include "Configuration.h"
#include <SDK.hpp>

namespace WorldFeatures {
    void Func_UpdateCurrentLevel(ReadyornotOverlay* Overlay);
    void Func_UnlitMode();
    bool UnlockAllDoors();
    bool KillCharactersByTeam(bool killSuspects, bool killCivilians);
    bool CollectAllEvidence();
    bool IncapacitateAllCharacters();
}
