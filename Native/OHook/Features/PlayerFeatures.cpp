#include <cmath>
#include <algorithm>
#include <iostream>
#include "Configuration.h"
#include "Utils.h"
#include "Hotkey.h"
#include "SDKExt.h"
#include "ReadyornotOverlay.h"
#include "PlayerFeatures.h"
#include "WorldFeatures.h"

#include <SDK/ReadyOrNot_parameters.hpp>

#include "console.hpp"
#include "LocalizationManager.h"
#include "ImGuiExt.h"
#include "ActorCache.h"
#include <chrono>
#include <Windows.h>
#include <imgui.h>
#include <map>
#include <atomic>

using namespace SDK;

namespace PlayerFeatures {

    void DoNoClip() {
        if (IsLevelTransitioning.load()) {
            return;
        }
        
        static bool wasNoClipEnabled = false;

        auto RONCharacter = GetARONCharacter();
        if (!RONCharacter || !IsActorValid(RONCharacter)) {
            if (wasNoClipEnabled) {
                wasNoClipEnabled = false;
            }
            return;
        }

        AReadyOrNotPlayerController* PlayerController = GetARONPC();
        if (!PlayerController || !IsActorValid(PlayerController)) {
            if (wasNoClipEnabled) {
                wasNoClipEnabled = false;
            }
            return;
        }

        UCharacterMovementComponent* RONMovementComponent = RONCharacter->CharacterMovement;
        if (!RONMovementComponent || !IsComponentValid(RONMovementComponent) || RONMovementComponent->IsDefaultObject()) {
            if (wasNoClipEnabled) {
                wasNoClipEnabled = false;
            }
            return;
        }

        if (!RONCharacter->CapsuleComponent || !IsComponentValid(RONCharacter->CapsuleComponent)) {
            if (wasNoClipEnabled) {
                wasNoClipEnabled = false;
            }
            return;
        }

        if (!PlayerController->PlayerCameraManager || !IsActorValid(PlayerController->PlayerCameraManager)) {
            if (wasNoClipEnabled) {
                wasNoClipEnabled = false;
            }
            return;
        }

        if (Configuration::cfg_EnableNoClip && (Configuration::cfg_NoClipKey == 0 || Hotkey::IsPressed(Configuration::cfg_NoClipKey))) {
            if (!wasNoClipEnabled) {
                RONMovementComponent->SetMovementMode(EMovementMode::MOVE_Flying, 5);
                RONCharacter->CapsuleComponent->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
                wasNoClipEnabled = true;
            }

            const FRotator& CameraRot = PlayerController->PlayerCameraManager->GetCameraRotation();

            FVector CameraForward = UKismetMathLibrary::GetForwardVector(CameraRot);
            FVector CameraRight = UKismetMathLibrary::GetRightVector(CameraRot);
            constexpr FVector CameraUp = { 0.f, 0.f, 1.f };

            CameraForward.Normalize();
            CameraRight.Normalize();

            FVector MovementDirection = { 0.f, 0.f, 0.f };
            float FlySpeed = 800.0f;

            if (IsKeyHeld('W')) {
                MovementDirection += CameraForward * FlySpeed;
            }
            if (IsKeyHeld('S')) {
                MovementDirection -= CameraForward * FlySpeed;
            }
            if (IsKeyHeld('D')) {
                MovementDirection += CameraRight * FlySpeed;
            }
            if (IsKeyHeld('A')) {
                MovementDirection -= CameraRight * FlySpeed;
            }
            if (IsKeyHeld(VK_SPACE)) {
                MovementDirection += CameraUp * FlySpeed;
            }
            if (IsKeyHeld(VK_CONTROL)) {
                MovementDirection -= CameraUp * FlySpeed;
            }
            if (IsKeyHeld(VK_SHIFT)) {
                FlySpeed *= 2.0f;
            }

            MovementDirection.Normalize();
            MovementDirection *= FlySpeed;

            constexpr float DeltaTime = 1.0f / 60.0f;

            const FVector MovementDelta = MovementDirection * DeltaTime;

            FHitResult HitResult;
            RONCharacter->K2_SetActorLocation(RONCharacter->K2_GetActorLocation() + MovementDelta, false, &HitResult, false);
        }
        else if (wasNoClipEnabled) {
            if (RONMovementComponent && IsComponentValid(RONMovementComponent) && !RONMovementComponent->IsDefaultObject()) {
                RONMovementComponent->SetMovementMode(EMovementMode::MOVE_Walking, 1);
            }
            
            if (RONCharacter && IsActorValid(RONCharacter) && 
                RONCharacter->CapsuleComponent && IsComponentValid(RONCharacter->CapsuleComponent)) {
                RONCharacter->CapsuleComponent->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Block);
                RONCharacter->CapsuleComponent->SetCollisionResponseToChannel(ECollisionChannel::ECC_Pawn, ECollisionResponse::ECR_Block);
            }

            wasNoClipEnabled = false;
        }
    }

    void DoSpeedmod() {
        if (IsLevelTransitioning.load()) {
            return;
        }
        
        static bool wasSpeedhackEnabled = false;

        auto RONCharacter = GetARONCharacter();
        if (!RONCharacter || !IsActorValid(RONCharacter)) {
            if (wasSpeedhackEnabled) {
                wasSpeedhackEnabled = false;
            }
            return;
        }

        APlayerController* PlayerController = GetARONPC();
        if (!PlayerController || !IsActorValid(PlayerController)) {
            if (wasSpeedhackEnabled) {
                wasSpeedhackEnabled = false;
            }
            return;
        }

        AActor* PawnPlayer = PlayerController->K2_GetPawn();
        if (!PawnPlayer || !IsActorValid(PawnPlayer)) {
            if (wasSpeedhackEnabled) {
                wasSpeedhackEnabled = false;
            }
            return;
        }

        if (Configuration::cfg_EnableSpeedhack && (Configuration::cfg_SpeedKey == 0 || Hotkey::IsPressed(Configuration::cfg_SpeedKey))) {
            float SpeedMultiplier = Configuration::cfg_speedPlayerValue;
            
            if (SpeedMultiplier > 0.1f && SpeedMultiplier <= 10.0f) {
                PawnPlayer->CustomTimeDilation = SpeedMultiplier;
                wasSpeedhackEnabled = true;
            }
        }
        else if (wasSpeedhackEnabled) {
            if (PawnPlayer && IsActorValid(PawnPlayer)) {
                PawnPlayer->CustomTimeDilation = 1.0f;
            }
            wasSpeedhackEnabled = false;
        }
    }

    void DoCustomFOV() {
        if (!Configuration::cfg_EnableCustomFOV)
            return;
            
        if (IsLevelTransitioning.load()) {
            return;
        }
        
        SetCustomFOV(Configuration::cfg_CustomFOV);
    }

    void DoGodmode() {
        if (!Configuration::cfg_EnableGodmode) {
            return;
        }
            
        if (IsLevelTransitioning.load()) {
            return;
        }
            
        auto RONCharacter = GetARONCharacter();
        if (!RONCharacter || !IsActorValid(RONCharacter)) {
            return;
        }

        RONCharacter->bCanBeDamaged = false;
            
        UCharacterHealthComponent* HealthComponent = RONCharacter->CharacterHealth;
        if (!HealthComponent || !IsComponentValid(HealthComponent) || HealthComponent->IsDefaultObject()) {
            return;
        }

        const float godmodeHealth = 9999.0f;
        const int32 maxTickets = 100;

        ELimbType limbs[] = {
            ELimbType::LT_Head,
            ELimbType::LT_RightLeg,
            ELimbType::LT_LeftLeg,
            ELimbType::LT_RightArm,
            ELimbType::LT_LeftArm
        };

        for (const auto& limbType : limbs) {
            try {
                auto* limbDataPtr = &HealthComponent->Head;
                
                switch (limbType) {
                    case ELimbType::LT_Head:
                        limbDataPtr = &HealthComponent->Head;
                        break;
                    case ELimbType::LT_RightLeg:
                        limbDataPtr = &HealthComponent->RightLeg;
                        break;
                    case ELimbType::LT_LeftLeg:
                        limbDataPtr = &HealthComponent->LeftLeg;
                        break;
                    case ELimbType::LT_RightArm:
                        limbDataPtr = &HealthComponent->RightArm;
                        break;
                    case ELimbType::LT_LeftArm:
                        limbDataPtr = &HealthComponent->LeftArm;
                        break;
                }

                float* healthPtr = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(limbDataPtr) + 0x8);
                float* maxHealthPtr = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(limbDataPtr) + 0xC);
                int32* ticketsPtr = reinterpret_cast<int32*>(reinterpret_cast<uintptr_t>(limbDataPtr) + 0x20);
                int32* maxTicketsPtr = reinterpret_cast<int32*>(reinterpret_cast<uintptr_t>(limbDataPtr) + 0x1C);

                *healthPtr = godmodeHealth;
                *maxHealthPtr = godmodeHealth;
                *ticketsPtr = maxTickets;
                *maxTicketsPtr = maxTickets;

                HealthComponent->SetLimbHealth(limbType, godmodeHealth);
                HealthComponent->IncreaseLimbHealth(limbType, godmodeHealth);
                HealthComponent->ResetLimbTickets(limbType);
                HealthComponent->IncreaseLimbTickets(limbType, maxTickets);
                HealthComponent->SetCurrentLimbHealthToMax(limbType);
            }
            catch (...) {
            }
        }

        if (HealthComponent->GetHealthStatus() != EPlayerHealthStatus::HS_Healthy) {
            HealthComponent->SetHealthStatus(EPlayerHealthStatus::HS_Healthy);
        }

        if (RONCharacter->IsIncapacitated() || RONCharacter->IsDeadOrUnconscious() || RONCharacter->IsDowned()) {
            HealthComponent->SetHealthStatus(EPlayerHealthStatus::HS_Healthy);
        }

        HealthComponent->ResetReviveHealth();
        HealthComponent->SetReviveHealth(godmodeHealth);
        HealthComponent->SetRemainingRevives(maxTickets);
        HealthComponent->IncreaseRevive();
        HealthComponent->IncreaseReviveHealth(godmodeHealth);
    }

    void DisableGodmode() {
        auto RONCharacter = GetARONCharacter();
        if (!RONCharacter || !IsActorValid(RONCharacter)) {
            return;
        }

        RONCharacter->bCanBeDamaged = true;
    }

    void DoNoSuspectFirefight() {
        if (!Configuration::cfg_NoSuspectFirefight)
            return;

        if (IsLevelTransitioning.load()) {
            return;
        }

        const auto& allActors = ActorCache::GetInstance().GetCachedActors();
        
        for (const auto& actorInfo : allActors) {
            if (actorInfo.Category != EActorCategory::Suspect)
                continue;
                
            if (!actorInfo.Actor || !IsActorValid(actorInfo.Actor))
                continue;
                
            auto* CyberneticChar = static_cast<SDK::ACyberneticCharacter*>(actorInfo.Actor);
            if (!CyberneticChar || !IsActorValid(CyberneticChar))
                continue;
                
            if (!CyberneticChar->CharacterHealth || 
                !IsComponentValid(CyberneticChar->CharacterHealth) || 
                CyberneticChar->GetHealthStatus() == SDK::EPlayerHealthStatus::HS_Dead)
                continue;
            if (CyberneticChar->CombatState != SDK::ECombatState::CS_Unaware) {
                CyberneticChar->CombatState = SDK::ECombatState::CS_Unaware;
            }
        }
    }

    namespace {
        void ForceCharacterComply(SDK::ACyberneticCharacter* cyberneticChar, bool disarmWeapons) {
            if (!cyberneticChar || !IsActorValid(cyberneticChar)) return;
            if (!cyberneticChar->CharacterHealth || !IsComponentValid(cyberneticChar->CharacterHealth)) return;
            if (cyberneticChar->GetHealthStatus() == SDK::EPlayerHealthStatus::HS_Dead
                || cyberneticChar->IsDeadOrUnconscious()
                || cyberneticChar->IsArrested()) return;

            if (disarmWeapons) {
                cyberneticChar->bDrawingWeapon = false;
                cyberneticChar->bPickingUpWeapon = false;
                cyberneticChar->bIsRaisingWeapon = false;
                cyberneticChar->bIsLoweringWeapon = false;
                cyberneticChar->DrawingWeaponTime = 0.0f;
                cyberneticChar->PickingUpWeaponTime = 0.0f;
                cyberneticChar->RaisingWeaponTime = 0.0f;
                cyberneticChar->LoweringWeaponTime = 0.0f;
            }

            cyberneticChar->bHeardYellFromOfficer = true;
            cyberneticChar->TimeSinceHeardOfficerYell = 0.0f;
            cyberneticChar->ForceComplianceStrength = 1.0f;
            cyberneticChar->CombatState = SDK::ECombatState::CS_Unaware;
            cyberneticChar->Stress = 0.0f;
            cyberneticChar->HesitationTime = 0.0f;
            cyberneticChar->bIsFakeSurrender = false;

            const bool alreadyComplying = cyberneticChar->IsComplying()
                || cyberneticChar->IsSurrendered()
                || cyberneticChar->IsSurrenderComplete();
            if (!alreadyComplying) {
                cyberneticChar->Surrender();
            }

            cyberneticChar->bIsComplying = true;
            cyberneticChar->bIsWaiting = true;
        }

        void ApplyComplyToCategories(bool includeCivilians, bool disarmWeapons) {
            if (IsLevelTransitioning.load()) return;

            const auto& allActors = ActorCache::GetInstance().GetCachedActors();
            for (const auto& actorInfo : allActors) {
                const bool matchesCategory = actorInfo.Category == EActorCategory::Suspect
                    || (includeCivilians && actorInfo.Category == EActorCategory::Civilian);
                if (!matchesCategory) continue;
                if (!actorInfo.Actor || !IsActorValid(actorInfo.Actor)) continue;

                auto* cyberneticChar = static_cast<SDK::ACyberneticCharacter*>(actorInfo.Actor);
                ForceCharacterComply(cyberneticChar, disarmWeapons);
            }
        }

        bool IsTeleportTargetCategory(EActorCategory category) {
            switch (category) {
                case EActorCategory::Suspect:
                case EActorCategory::Civilian:
                case EActorCategory::Wounded:
                case EActorCategory::Player:
                case EActorCategory::SERT_Red:
                case EActorCategory::SERT_Blue:
                case EActorCategory::Squad:
                case EActorCategory::Evidence:
                    return true;
                default:
                    return false;
            }
        }

        bool TryComputeDirectionalDestination(
            SDK::AReadyOrNotPlayerController* controller,
            SDK::AActor* pawn,
            bool useRaycast,
            SDK::FVector& outDestination)
        {
            if (!controller || !controller->PlayerCameraManager || !IsActorValid(controller->PlayerCameraManager)) {
                return false;
            }

            const float meters = Configuration::cfg_TPNoTargetDistance > 0.0f
                ? Configuration::cfg_TPNoTargetDistance
                : 15.0f;
            const float distanceCm = meters * 100.0f;
            const FVector camLoc = controller->PlayerCameraManager->GetCameraLocation();
            const FRotator camRot = controller->PlayerCameraManager->GetCameraRotation();
            const FVector forward = UKismetMathLibrary::GetForwardVector(camRot);

            if (!useRaycast) {
                outDestination = camLoc + forward * distanceCm;
                outDestination.Z += Configuration::cfg_ActorTeleportOffset;
                return true;
            }

            UWorld* world = GetWorld();
            if (!world) return false;

            TArray<AActor*> ignoreActors;
            ignoreActors.Add(pawn);
            FHitResult hit{};
            const FVector traceStart = camLoc + forward * 30.0f;
            const FVector traceEnd = camLoc + forward * distanceCm;
            const bool hitSomething = UKismetSystemLibrary::LineTraceSingle(
                reinterpret_cast<UObject*>(world),
                traceStart,
                traceEnd,
                ETraceTypeQuery::TraceTypeQuery1,
                false,
                ignoreActors,
                EDrawDebugTrace::None,
                &hit,
                true,
                FLinearColor{ 0.f, 0.f, 0.f, 0.f },
                FLinearColor{ 0.f, 0.f, 0.f, 0.f },
                0.0f);

            if (hitSomething && hit.bBlockingHit) {
                outDestination = FVector{
                    hit.ImpactPoint.X + hit.ImpactNormal.X * 45.0f,
                    hit.ImpactPoint.Y + hit.ImpactNormal.Y * 45.0f,
                    hit.ImpactPoint.Z + hit.ImpactNormal.Z * 45.0f + Configuration::cfg_ActorTeleportOffset
                };
            } else {
                outDestination = traceEnd;
                outDestination.Z += Configuration::cfg_ActorTeleportOffset;
            }
            return true;
        }
    }

    void DoDisarmAllSuspects() {
        if (!Configuration::cfg_DisarmAllSuspects) return;
        ApplyComplyToCategories(false, true);
    }

    void DoForceSuspectSurrender() {
        if (!Configuration::cfg_ForceSuspectSurrender) return;
        ApplyComplyToCategories(false, false);
    }

    void DoAutoComplySuspects() {
        if (!Configuration::cfg_AutoComplySuspects) return;
        ApplyComplyToCategories(true, true);
    }

    void DoDisableAllTraps() {
        if (!Configuration::cfg_DisableAllTraps || IsLevelTransitioning.load()) {
            return;
        }

        auto* localChar = GetARONCharacter();
        const auto& allActors = ActorCache::GetInstance().GetCachedActors();
        for (const auto& actorInfo : allActors) {
            if (actorInfo.Category != EActorCategory::DoorTraps) continue;
            if (!actorInfo.Actor || !IsActorValid(actorInfo.Actor)) continue;

            auto* trapActor = static_cast<SDK::ATrapActor*>(actorInfo.Actor);
            if (!trapActor || !IsActorValid(trapActor)) continue;

            if (trapActor->TrapStatus != SDK::ETrapState::TS_Disabled) {
                trapActor->OnTrapDisarmed(localChar);
                trapActor->Server_OnTrapDisarmed();
                trapActor->Multicast_OnTrapDisarmed();
                trapActor->TrapStatus = SDK::ETrapState::TS_Disabled;
                trapActor->TrapDeInit();
            }

            if (trapActor->TripWireTriggerComponent && IsComponentValid(trapActor->TripWireTriggerComponent)) {
                trapActor->TripWireTriggerComponent->SetCollisionEnabled(SDK::ECollisionEnabled::NoCollision);
            }
        }
    }

    void UpdateTeleportTarget(ReadyornotOverlay* overlay, const SDK::AHUD* hud) {
        if (!overlay) return;

        auto clearTarget = [&]() {
            overlay->TeleportTargetActor = nullptr;
            overlay->TeleportTargetLocation = FVector{ 0.f, 0.f, 0.f };
            overlay->TeleportTargetName.clear();
        };

        const bool teleportEnabled = Configuration::cfg_TeleportToTarget || Configuration::cfg_EnableTPTarget;
        if (!teleportEnabled || !Configuration::cfg_EnableTPTarget || IsLevelTransitioning.load() || !hud || !hud->Canvas) {
            clearTarget();
            return;
        }

        if (Configuration::cfg_TPTargetKey != 0 && !Hotkey::IsPressed(Configuration::cfg_TPTargetKey)) {
            clearTarget();
            return;
        }

        auto* controller = GetARONPC();
        auto* localPawn = controller ? controller->K2_GetPawn() : nullptr;
        if (!controller || !IsActorValid(controller) || !localPawn || !IsActorValid(localPawn)) {
            clearTarget();
            return;
        }

        float radius = Configuration::cfg_TPFOVRadius;
        if (Configuration::cfg_SmallerTPTargeting) radius *= 0.5f;
        if (radius <= 0.0f) {
            clearTarget();
            return;
        }

        const FVector2D center{ hud->Canvas->SizeX * 0.5f, hud->Canvas->SizeY * 0.5f };
        const auto& allActors = ActorCache::GetInstance().GetCachedActors();

        AActor* bestActor = nullptr;
        FVector bestLoc{};
        std::string bestName;
        float bestScreenDist = radius;

        for (const auto& info : allActors) {
            if (!IsTeleportTargetCategory(info.Category)) continue;
            if (!info.Actor || !IsActorValid(info.Actor) || info.Actor == localPawn) continue;

            if (info.Category != EActorCategory::Evidence) {
                auto* character = static_cast<SDK::AReadyOrNotCharacter*>(info.Actor);
                if (!character->CharacterHealth || !IsComponentValid(character->CharacterHealth)) continue;
                if (character->GetHealthStatus() == EPlayerHealthStatus::HS_Dead) continue;
            } else {
                auto* evidence = static_cast<SDK::AEvidenceActor*>(info.Actor);
                if (!evidence->EvidenceComponent || !IsComponentValid(evidence->EvidenceComponent)) continue;
                if (evidence->EvidenceComponent->IsEvidenceCollected() || !evidence->EvidenceComponent->CanBeCollected()) continue;
            }

            const FVector worldLoc = info.Actor->K2_GetActorLocation();
            FVector2D screenPos{};
            if (!controller->ProjectWorldLocationToScreen(worldLoc, &screenPos, true)) continue;

            const float screenDist = CustomMath::Sqrt(
                CustomMath::Square(screenPos.X - center.X) +
                CustomMath::Square(screenPos.Y - center.Y));
            if (screenDist <= bestScreenDist) {
                bestScreenDist = screenDist;
                bestActor = info.Actor;
                bestLoc = worldLoc;
                bestName = info.CharacterName;
            }
        }

        if (bestActor) {
            overlay->TeleportTargetActor = bestActor;
            overlay->TeleportTargetLocation = bestLoc;
            overlay->TeleportTargetName = std::move(bestName);
        } else {
            clearTarget();
        }
    }

    void DoTeleportToTarget(ReadyornotOverlay* overlay) {
        const bool enabled = Configuration::cfg_TeleportToTarget || Configuration::cfg_EnableTPTarget;
        const int key = Configuration::cfg_TeleportKey != 0
            ? Configuration::cfg_TeleportKey
            : (Configuration::cfg_EnableTPTarget ? Configuration::cfg_TPTargetKey : 0);

        static bool wasPressed = false;
        if (!enabled || !overlay || key == 0 || IsLevelTransitioning.load() || !Hotkey::IsPressed(key)) {
            wasPressed = false;
            return;
        }
        if (wasPressed) return;
        wasPressed = true;

        auto* controller = GetARONPC();
        auto* pawn = controller ? controller->K2_GetPawn() : nullptr;
        if (!controller || !IsActorValid(controller) || !pawn || !IsActorValid(pawn)) return;

        AActor* target = (Configuration::cfg_EnableTPTarget && overlay->TeleportTargetActor && IsActorValid(overlay->TeleportTargetActor))
            ? overlay->TeleportTargetActor
            : nullptr;

        FVector destination{};
        const int method = Configuration::cfg_TeleportMethod;
        if (method == 1) {
            // Target Only: require a valid selected target.
            if (!target) return;
            destination = target->K2_GetActorLocation();
            destination.Z += Configuration::cfg_ActorTeleportOffset;
        } else if (method == 0 && target) {
            destination = target->K2_GetActorLocation();
            destination.Z += Configuration::cfg_ActorTeleportOffset;
        } else if (method == 2) {
            if (!TryComputeDirectionalDestination(controller, pawn, false, destination)) return;
        } else {
            // Method 0 fallback (no target) and Method 3 (crosshair raycast)
            if (!TryComputeDirectionalDestination(controller, pawn, true, destination)) return;
        }

        if (auto* ronChar = GetARONCharacter(); ronChar && IsActorValid(ronChar)
            && ronChar->CharacterMovement && IsComponentValid(ronChar->CharacterMovement)) {
            ronChar->CharacterMovement->Velocity = FVector{ 0.f, 0.f, 0.f };
        }

        const FRotator rot = pawn->K2_GetActorRotation();
        if (!pawn->K2_TeleportTo(destination, rot)) {
            FHitResult hit{};
            pawn->K2_SetActorLocation(destination, false, &hit, true);
        }
    }

    void DoWorldActionHotkeys() {
        if (IsLevelTransitioning.load()) return;

        static bool wasIncapacitatePressed = false;
        const bool incapacitatePressed = Hotkey::IsPressed(Configuration::cfg_IncapacitateAllKey);
        if (incapacitatePressed && !wasIncapacitatePressed) {
            WorldFeatures::IncapacitateAllCharacters();
        }
        wasIncapacitatePressed = incapacitatePressed;

        static bool wasCollectEvidencePressed = false;
        const bool collectEvidencePressed = Hotkey::IsPressed(Configuration::cfg_CollectAllEvidenceKey);
        if (collectEvidencePressed && !wasCollectEvidencePressed) {
            WorldFeatures::CollectAllEvidence();
        }
        wasCollectEvidencePressed = collectEvidencePressed;
    }
}
