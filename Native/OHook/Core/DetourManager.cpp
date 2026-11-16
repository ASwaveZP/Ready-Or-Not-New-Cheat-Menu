#include "DetourManager.h"
#include "ReadyornotOverlay.h"
#include "Configuration.h"
#include "MenuPipe.h"
#include "SDKExt.h"
#include "Utils.h"
#include "InputOwnership.h"
#include "Features/AimFeatures.h"
#include "Features/PlayerFeatures.h"
#include "Features/WorldFeatures.h"
#include "../ReadyornotSDK/SDK.hpp"
#include "console.hpp"

#include <SDK/ReadyOrNot_parameters.hpp>

#include <algorithm>
#include <vector>
#include <format>
#include <functional>
#include <locale>
#include <codecvt>
#include <unordered_set>
#include <unordered_map>
#include <Windows.h>

using namespace SDK;

// ESP coloring
ImU32 PlayerESPColorVisible = IM_COL32(0, 255, 0, 255);                  // Green visible players
ImU32 PlayerESPColorAimed = IM_COL32(0, 0, 255, 255);                    // Blue aimed players
ImU32 PlayerESPColorNonVisible = IM_COL32(255, 0, 0, 255);               // Red non-visible players

ImU32 HealthBarColorGreen = IM_COL32(0, 255, 0, 255);                    // Green
ImU32 HealthBarColorYellow = IM_COL32(255, 255, 0, 255);                 // Yellow
ImU32 HealthBarColorRed = IM_COL32(255, 0, 0, 255);                      // Red
// Ready or Not Character Team Colors
ImU32 SERTRedESPColor = IM_COL32(220, 20, 60, 255);                      // Crimson for SERT Red team
ImU32 SERTBlueESPColor = IM_COL32(30, 144, 255, 255);                    // Dodger blue for SERT Blue team
ImU32 SuspectESPColor = IM_COL32(255, 69, 0, 255);                       // Orange red for suspects
ImU32 CivilianESPColor = IM_COL32(255, 215, 0, 255);                     // Gold for civilians
ImU32 SquadESPColor = IM_COL32(124, 252, 0, 255);                        // Lawn green for squad members
ImU32 WoundedESPColor = IM_COL32(255, 20, 147, 255);                     // Deep pink for wounded characters
// Object and interactive element colors
ImU32 EvidenceESPColor = IM_COL32(138, 43, 226, 255);                    // Blue violet for evidence
ImU32 CollectedEvidenceESPColor = IM_COL32(75, 0, 130, 255);             // Indigo for collected evidence
ImU32 ItemsESPColor = IM_COL32(153, 153, 153, 255);                      // Light gray for items
ImU32 DoorsESPColor = IM_COL32(139, 69, 19, 255);                        // Saddle brown for doors
ImU32 TrapsESPColor = IM_COL32(255, 0, 255, 255);                        // Magenta for traps (danger!)
ImU32 SecurablesESPColor = IM_COL32(0, 255, 255, 255);                   // Cyan for securables
// Legacy item color
ImU32 ItemESPColorDefault = IM_COL32(153, 153, 153, 255);                // Neutral gray for unknown items

// Detouring
void DetourManager::ProcessEventDetour(const UObject* Class, const UFunction* Function, void* Params) {
    if (!Function || !Class) {
        if (OriginalProcessEvent) OriginalProcessEvent(Class, Function, Params);
        return;
    }
    // Most engine events are irrelevant. Cache classification without building
    // full object-path strings or recording every invocation on the hot path.
    enum class EventKind { Other, Hitscan, Fire, HUD };
    struct EventEntry { FName name; int32 index; EventKind kind; };
    static thread_local std::unordered_map<const UFunction*, EventEntry> eventKinds;
    auto found = eventKinds.find(Function);
    if (found == eventKinds.end() || !(found->second.name == Function->Name) || found->second.index != Function->Index) {
        const auto name = Function->GetName();
        const auto kind = name == "Multicast_PerformHitscan" ? EventKind::Hitscan
            : name == "Server_OnFire" ? EventKind::Fire
            : name == "ReceiveDrawHUD" ? EventKind::HUD : EventKind::Other;
        if (eventKinds.size() >= 4096) eventKinds.clear();
        found = eventKinds.insert_or_assign(Function, EventEntry{ Function->Name, Function->Index, kind }).first;
    }
    const auto eventKind = found->second.kind;
    if (eventKind == EventKind::Other) {
        if (OriginalProcessEvent) OriginalProcessEvent(Class, Function, Params);
        return;
    }

    if (Configuration::cfg_BulletPenetration && Params && eventKind == EventKind::Hitscan
        && Class->IsA(ABaseMagazineWeapon::StaticClass())) {
        auto* weapon = const_cast<ABaseMagazineWeapon*>(static_cast<const ABaseMagazineWeapon*>(Class));
        const auto* shot = reinterpret_cast<Params::BaseMagazineWeapon_Multicast_PerformHitscan*>(Params);
        if (AimFeatures::TryAuthorityWallHit(weapon, shot->HitscanShot, shot->bLocalOnly)) return;
    }

    if (Configuration::cfg_ForcedCrosshairShot && eventKind == EventKind::Fire && Params
        && Class && Class->IsA(ABaseMagazineWeapon::StaticClass())) {
        auto* weapon = const_cast<ABaseMagazineWeapon*>(static_cast<const ABaseMagazineWeapon*>(Class));
        auto* localCharacter = GetARONCharacter();
        auto* controller = GetARONPC();
        if (weapon && localCharacter && weapon->Owner == localCharacter && controller && IsActorValid(controller)) {
            int32 width = 0, height = 0;
            FVector cameraLocation{}, cameraDirection{};
            controller->GetViewportSize(&width, &height);
            if (width > 0 && height > 0 && controller->DeprojectScreenPositionToWorld(width * 0.5f, height * 0.5f, &cameraLocation, &cameraDirection)) {
                auto* fire = reinterpret_cast<Params::BaseMagazineWeapon_Server_OnFire*>(Params);
                fire->SpawnLoc = cameraLocation;
                fire->Direction = UKismetMathLibrary::Conv_VectorToRotator(cameraDirection);
            }
        }
    }

    if (Configuration::cfg_BulletPenetration && Params && eventKind == EventKind::Fire
        && Class->IsA(ABaseMagazineWeapon::StaticClass())) {
        auto* weapon = const_cast<ABaseMagazineWeapon*>(static_cast<const ABaseMagazineWeapon*>(Class));
        if (weapon && !weapon->bHitScan) {
            const auto* fire = reinterpret_cast<Params::BaseMagazineWeapon_Server_OnFire*>(Params);
            AimFeatures::TryAuthorityProjectileWallHit(weapon, fire->SpawnLoc, fire->Direction);
        }
    }

    // Update once per HUD frame, not once for every ticking world actor.
    if (eventKind == EventKind::HUD && Class->IsA(AHUD::StaticClass())) {
        const auto Overlay = dynamic_cast<ReadyornotOverlay*>(OverlayBase::Instance);
        WorldFeatures::Func_UpdateCurrentLevel(Overlay);
        ActorCache::GetInstance().Update();
        // Apply queued menu visibility transitions and pipe commands on the game thread.
        // ChangeInputMode and UE actor/world calls are game-thread state and
        // must not be touched from the render/pipe thread.
        InputOwnership::EnsureDisplayMode();
        InputOwnership::ApplyPending();
        MenuPipe::ApplyPending();

        AimFeatures::DoTargeting(Overlay, reinterpret_cast<const AHUD*>(Class));
        AimFeatures::DoAimbot(Overlay, reinterpret_cast<const AHUD*>(Class));
        PlayerFeatures::UpdateTeleportTarget(Overlay, reinterpret_cast<const AHUD*>(Class));
        PlayerFeatures::DoTeleportToTarget(Overlay);
        PlayerFeatures::DoWorldActionHotkeys();
        AimFeatures::DoInfiniteAmmo();
        AimFeatures::ApplyWeaponOptions();
        AimFeatures::RemoveWeaponEffects();
        AimFeatures::DoTriggerBot();

        // Gamemode features
        WorldFeatures::Func_UnlitMode();
        // Player features
        PlayerFeatures::DoSpeedmod();
        PlayerFeatures::DoNoClip();
        PlayerFeatures::DoNoSuspectFirefight();
        PlayerFeatures::DoDisarmAllSuspects();
        PlayerFeatures::DoForceSuspectSurrender();
        PlayerFeatures::DoAutoComplySuspects();
        PlayerFeatures::DoDisableAllTraps();
        PlayerFeatures::DoCustomFOV();

        if (Configuration::cfg_EnableGodmode) {
            PlayerFeatures::DoGodmode();
        } else {
            PlayerFeatures::DisableGodmode();
        }
    }

    if (OriginalProcessEvent) {
        OriginalProcessEvent(Class, Function, Params);
    }
}

// Essentials
void DetourManager::SetupDetour(void* Instance, void (*DetourFunc)(const UObject*, const UFunction*, void*)) {
    const void** Vtable = *static_cast<const void***>(Instance);

    DWORD OldProtection;
    VirtualProtect(Vtable, sizeof(DWORD) * 1024, PAGE_EXECUTE_READWRITE, &OldProtection);

    const int32_t Idx = Offsets::ProcessEventIdx;
    OriginalProcessEvent = reinterpret_cast<void(*)(const UObject*, const UFunction*, void*)>(reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr)) + Offsets::ProcessEvent);
    Vtable[Idx] = DetourFunc;

    HookedClient = Instance;
    VirtualProtect(Vtable, sizeof(DWORD) * 1024, OldProtection, &OldProtection);
}

void DetourManager::SetupDetour(void* Instance) {
    SetupDetour(Instance, &DetourManager::ProcessEventDetour);
}

void DetourManager::ProcessEventDetourCallback(const UObject* Class, const UFunction* Function, void* Params, const DetourManager* manager) {
    manager->ProcessEventDetour(Class, Function, Params);
}
