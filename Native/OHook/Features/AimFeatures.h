#pragma once
#include "SDKExt.h"
#include "ReadyornotOverlay.h"
#include "Configuration.h"
#include <SDK.hpp>

using namespace SDK;

class ReadyornotOverlay;

namespace AimFeatures {
    void DoTargeting(ReadyornotOverlay* Overlay, const SDK::AHUD* HUD);
    void DoAimbot(ReadyornotOverlay* Overlay, const SDK::AHUD* HUD);
    void DoInfiniteAmmo();
    void ApplyWeaponOptions();
    void DoTriggerBot();
    bool TryAuthorityWallHit(SDK::ABaseMagazineWeapon* weapon, const SDK::FHitscanShot& shot, bool localOnly);
    void TryAuthorityProjectileWallHit(SDK::ABaseMagazineWeapon* weapon, const SDK::FVector& origin, const SDK::FRotator& rotation);
    void RemoveWeaponEffects();
}
