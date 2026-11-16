#include "WorldFeatures.h"
#include "Configuration.h"
#include "Utils.h"
#include "SDKExt.h"
#include "LocalizationManager.h"
#include "ImGuiExt.h"
#include "ActorCache.h"
#include <chrono>
#include <unordered_set>
#include <Windows.h>

using namespace SDK;

namespace WorldFeatures {

    void Func_UpdateCurrentLevel(ReadyornotOverlay* Overlay) {
        if (!Overlay)
            return;

        UWorld* GWorld = GetWorld();
        if (!GWorld)
            return;
    
        if (Overlay->CurrentLevel != GWorld->PersistentLevel) {
            Overlay->CurrentLevel = GWorld->PersistentLevel;
            
            FString LevelName = UGameplayStatics::GetCurrentLevelName(GWorld, false);
            std::string levelNameStr = LevelName.ToString();
            
            if (levelNameStr != Overlay->CurrentLevelName) {
                Overlay->CurrentLevelName = levelNameStr;

                if (levelNameStr.find("MainMenu_V2") != std::string::npos) {
                    Overlay->CurrentMap = "Startup/Login";
                } else if (levelNameStr.find("TransitionMap") != std::string::npos) {
                    Overlay->CurrentMap = "Loading...";
                } else if (levelNameStr.find("RoN_Station_DLC02_Core") != std::string::npos) {
                    Overlay->CurrentMap = "Los Suenos Police HQ";
                } else {
                    Overlay->CurrentMap = levelNameStr;
                }
            }
        }
    }

    void Func_UnlitMode() {
        UWorld* GWorld = GetWorld();
        if (!GWorld)
            return;

        if (!GWorld->OwningGameInstance)
            return;

        APlayerController* PlayerController = GetAPC();
        if (!PlayerController || !IsActorValid(PlayerController))
            return;
            
        if (!PlayerController->PlayerCameraManager || !IsActorValid(PlayerController->PlayerCameraManager))
            return;

        UGameInstance* GameInstance = GWorld->OwningGameInstance;
        if (!GameInstance || !GameInstance->LocalPlayers || GameInstance->LocalPlayers.Num() <= 0) {
            return;
        }

        const ULocalPlayer* LocalPlayer = GameInstance->LocalPlayers[0];
        if (!LocalPlayer || !LocalPlayer->PlayerController || !LocalPlayer->PlayerController->IsA(APlayerController::StaticClass())) {
            return;
        }

        UGameViewportClient* GameViewportClient = LocalPlayer->ViewportClient;
        if (!GameViewportClient) {
            return;
        }

        uint8_t* ViewportClientPtr = reinterpret_cast<uint8_t*>(GameViewportClient);
        uint8_t* ViewModePtr = ViewportClientPtr + 0xB0;
        
        uint8_t currentViewMode = *ViewModePtr;
        uint8_t desiredViewMode = Configuration::cfg_EnableUnlitMode ? 0x1 : 0x3;
        
        if (currentViewMode != desiredViewMode) {
            *ViewModePtr = desiredViewMode;
        }
    }

    namespace {
        void UnlockDoorActor(Adoor* door) {
            if (!door || !IsActorValid(door)) return;
            door->bAlwaysLocked = false;
            door->bLocked = false;
            door->UnlockDoor(true);
            door->SetLocked(false);
            door->Multicast_SetLocked(false);
        }

        bool IsFriendlyOrPlayer(AReadyOrNotCharacter* character, AActor* localPlayer) {
            if (!character || character == localPlayer) return true;
            if (character->IsA(ABasePlayer_C::StaticClass()) || character->IsA(ASWATCharacter::StaticClass())) return true;
            auto* controller = character->GetController();
            if (controller && controller->IsA(AReadyOrNotPlayerController::StaticClass())) return true;
            const auto team = character->GetTeam();
            return team == ETeamType::TT_SERT_RED || team == ETeamType::TT_SERT_BLUE || team == ETeamType::TT_SQUAD;
        }
    }

    bool UnlockAllDoors() {
        UWorld* world = GetWorld();
        if (!world) return false;

        std::unordered_set<Adoor*> doors;
        TArray<AActor*> foundDoors;
        UGameplayStatics::GetAllActorsOfClass(reinterpret_cast<UObject*>(world), Adoor::StaticClass(), &foundDoors);
        for (AActor* actor : foundDoors) {
            if (actor && IsActorValid(actor)) doors.insert(static_cast<Adoor*>(actor));
        }

        if (auto* gameState = UReadyOrNotStatics::GetReadyOrNotGameState(); gameState && IsActorValid(gameState)) {
            for (Adoor* door : gameState->AllDoors) {
                if (door && IsActorValid(door)) doors.insert(door);
            }
        }

        Adoor::SetAllElectronicLocks(reinterpret_cast<UObject*>(world), false);
        for (Adoor* door : doors) {
            UnlockDoorActor(door);
            UnlockDoorActor(door->DriveSubDoor);
        }
        return true;
    }

    bool KillCharactersByTeam(bool killSuspects, bool killCivilians) {
        UWorld* world = GetWorld();
        auto* controller = GetARONPCForESP();
        if (!world || !controller || !IsActorValid(controller)) return false;

        AActor* localPlayer = controller->K2_GetPawn();
        TArray<AActor*> foundCharacters;
        UGameplayStatics::GetAllActorsOfClass(reinterpret_cast<UObject*>(world), AReadyOrNotCharacter::StaticClass(), &foundCharacters);

        for (AActor* actor : foundCharacters) {
            auto* character = static_cast<AReadyOrNotCharacter*>(actor);
            if (!character || !IsActorValid(character) || character == localPlayer) continue;
            if (character->GetHealthStatus() == EPlayerHealthStatus::HS_Dead) continue;

            const auto team = character->GetTeam();
            const bool isSuspect = character->IsA(ASuspectCharacter::StaticClass()) || team == ETeamType::TT_SUSPECT;
            const bool isCivilian = character->IsA(ACivilianCharacter::StaticClass()) || team == ETeamType::TT_CIVILIAN;
            if (!((killSuspects && isSuspect) || (killCivilians && isCivilian))) continue;

            character->bCanBeDamaged = true;
            character->Server_Kill();
            character->Kill();
            if (character->GetHealthStatus() != EPlayerHealthStatus::HS_Dead) {
                UGameplayStatics::ApplyDamage(character, 100000.0f, controller, localPlayer, UDamageType::StaticClass());
            }
        }
        return true;
    }

    bool CollectAllEvidence() {
        UWorld* world = GetWorld();
        auto* controller = GetARONPCForESP();
        auto* player = controller ? controller->K2_GetPawn() : nullptr;
        if (!world || !player || !IsActorValid(player)) return false;

        auto* ronPlayer = static_cast<AReadyOrNotCharacter*>(player);

        TArray<AActor*> evidenceActors;
        UGameplayStatics::GetAllActorsOfClass(reinterpret_cast<UObject*>(world), AEvidenceActor::StaticClass(), &evidenceActors);
        for (AActor* actor : evidenceActors) {
            auto* evidence = static_cast<AEvidenceActor*>(actor);
            if (!evidence || !IsActorValid(evidence) || !evidence->EvidenceComponent || !IsComponentValid(evidence->EvidenceComponent)) continue;
            if (!evidence->EvidenceComponent->IsEvidenceCollected() && evidence->EvidenceComponent->CanBeCollected()) {
                ronPlayer->PickupEvidence(evidence);
            }
        }

        TArray<AActor*> weapons;
        UGameplayStatics::GetAllActorsOfClass(reinterpret_cast<UObject*>(world), ABaseWeapon::StaticClass(), &weapons);
        for (AActor* actor : weapons) {
            auto* weapon = static_cast<ABaseWeapon*>(actor);
            if (!weapon || !IsActorValid(weapon) || weapon->IsEquipped()) continue;
            auto* evidenceComp = weapon->EvidenceComponent;
            if (!evidenceComp || !IsComponentValid(evidenceComp)) continue;
            if (!evidenceComp->IsEvidenceCollected() && evidenceComp->CanBeCollected()) {
                ronPlayer->PickupEvidence(weapon);
            }
        }

        ronPlayer->CollectPendingEvidence();
        return true;
    }

    bool IncapacitateAllCharacters() {
        UWorld* world = GetWorld();
        auto* controller = GetARONPCForESP();
        if (!world || !controller || !IsActorValid(controller)) return false;

        AActor* localPlayer = controller->K2_GetPawn();
        TArray<AActor*> foundCharacters;
        UGameplayStatics::GetAllActorsOfClass(reinterpret_cast<UObject*>(world), AReadyOrNotCharacter::StaticClass(), &foundCharacters);

        for (AActor* actor : foundCharacters) {
            auto* character = static_cast<AReadyOrNotCharacter*>(actor);
            if (!character || !IsActorValid(character) || IsFriendlyOrPlayer(character, localPlayer)) continue;
            if (character->IsDeadOrUnconscious() || character->IsIncapacitated()) continue;

            character->Server_Incapacitate();
            character->Incapacitate();
            if (character->CharacterHealth && IsComponentValid(character->CharacterHealth)
                && character->GetHealthStatus() == EPlayerHealthStatus::HS_Healthy) {
                character->CharacterHealth->SetHealthStatus(EPlayerHealthStatus::HS_Incapacitated);
            }
        }
        return true;
    }
}
