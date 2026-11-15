#include "AimFeatures.h"
#include "ActorCache.h"
#include "Utils.h"
#include "Hotkey.h"
#include "console.hpp"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <unordered_map>
#include <vector>
#include <Windows.h>

using namespace SDK;

namespace AimFeatures {

    bool TryAuthorityWallHit(ABaseMagazineWeapon* weapon, const FHitscanShot& shot, bool localOnly) {
        if (!Configuration::cfg_BulletPenetration || IsLevelTransitioning.load()
            || !weapon || !IsActorValid(weapon) || !weapon->bHitScan) return false;
        auto* player = GetARONCharacter();
        auto* controller = GetARONPC();
        if (!player || !IsActorValid(player) || !controller || !IsActorValid(controller)
            || weapon->Owner != player || !player->HasAuthority() || !weapon->HasAuthority()) return false;
        // Local prediction must never apply a second authoritative damage event.
        if (localOnly) return true;
        FVector direction = shot.Direction;
        const double length = std::sqrt(direction.X * direction.X + direction.Y * direction.Y + direction.Z * direction.Z);
        if (!std::isfinite(length) || length < 0.000001) return false;
        direction = direction * (1.0 / length);

        // Query character meshes individually: world geometry never enters this
        // trace. Trace only a short segment around each target, avoiding a fixed
        // world-distance cap and precision loss from enormous trace endpoints.
        const auto suspects = ActorCache::GetInstance().GetCachedActors();
        AReadyOrNotCharacter* nearest = nullptr;
        FHitResult nearestHit{};
        double nearestDistance = DBL_MAX;
        for (const auto& info : suspects) {
            if (info.Category != EActorCategory::Suspect) continue;
            auto* actor = info.Actor;
            if (!actor || !IsActorValid(actor)) continue;
            auto* target = static_cast<AReadyOrNotCharacter*>(actor);
            if (target->IsDeadOrUnconscious() || !target->Mesh) continue;
            FVector center{}, extent{};
            float sphereRadius = 0.0f;
            UKismetSystemLibrary::GetComponentBounds(target->Mesh, &center, &extent, &sphereRadius);
            const FVector delta = center - shot.Location;
            const double along = delta.X * direction.X + delta.Y * direction.Y + delta.Z * direction.Z;
            const double radius = sphereRadius + 10.0;
            if (!std::isfinite(along) || !std::isfinite(radius) || radius <= 0.0 || along + radius <= 0.0) continue;
            const FVector closest = shot.Location + direction * along;
            if (center.GetDistanceTo(closest) > radius) continue;
            const FVector start = shot.Location + direction * (std::max)(0.0, along - radius);
            const FVector end = shot.Location + direction * (along + radius);
            FVector location{}, normal{};
            FName bone{};
            FHitResult hit{};
            if (!target->Mesh->K2_LineTraceComponent(start, end, true, false, false, &location, &normal, &bone, &hit)) continue;
            const double distance = shot.Location.GetDistanceTo(location);
            if (distance >= nearestDistance) continue;
            nearest = target;
            nearestHit = hit;
            nearestHit.TraceStart.X = shot.Location.X;
            nearestHit.TraceStart.Y = shot.Location.Y;
            nearestHit.TraceStart.Z = shot.Location.Z;
            nearestHit.TraceEnd.X = end.X;
            nearestHit.TraceEnd.Y = end.Y;
            nearestHit.TraceEnd.Z = end.Z;
            nearestHit.Distance = static_cast<float>(distance);
            nearestDistance = distance;
        }
        if (!nearest) return false;
        const float damage = Configuration::cfg_OneHitKill ? 10000.0f : weapon->CurrentAmmoType.Damage;
        if (!std::isfinite(damage) || damage <= 0.0f) return false;
        UGameplayStatics::ApplyPointDamage(nearest, damage, direction, nearestHit, controller, weapon, weapon->DamageType);
        // Consume the native hitscan even when armour absorbs the damage.
        return true;
    }

    namespace {
        FVector GetAimBoneLocation(USkeletalMeshComponent* mesh, int selection) {
            if (!mesh || !mesh->SkeletalMesh) return {};
            if (selection < 0 || selection > 4) selection = 0;
            struct AimBones { FName names[5]{}; bool resolved[5]{}; bool valid[5]{}; };
            static thread_local std::unordered_map<USkeletalMesh*, AimBones> boneCache;
            if (boneCache.size() >= 256) boneCache.clear();
            auto& cached = boneCache[mesh->SkeletalMesh];
            if (cached.resolved[selection])
                return cached.valid[selection] ? mesh->GetSocketLocation(cached.names[selection]) : FVector{};
            cached.resolved[selection] = true;
            std::vector<std::string> candidates;
            switch (selection) {
            case 0: candidates = { "head" }; break;
            case 1: candidates = { "neck_1", "neck" }; break;
            // Prefer the upper chest joint immediately below the neck when the
            // character rig exposes it; fall back to the usual chest joint.
            case 2: candidates = { "spine_3", "spine3", "spine_2", "spine2" }; break;
            case 3: candidates = { "spine_1", "spine1" }; break;
            case 4: candidates = { "calf_le", "calf_ri", "calf_l", "calf_r" }; break;
            default: candidates = { "head" }; break;
            }
            const int32 boneCount = mesh->GetNumBones();
            for (const auto& candidate : candidates) {
                for (int32 i = 0; i < boneCount; ++i) {
                    const FName boneName = mesh->GetBoneName(i);
                    std::string normalized = boneName.ToString();
                    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (normalized != candidate) continue;
                    cached.names[selection] = boneName;
                    cached.valid[selection] = true;
                    const FVector location = mesh->GetSocketLocation(boneName);
                    if (!location.IsZero()) return location;
                }
            }
            return {};
        }
    }

    void DoTargeting(ReadyornotOverlay* Overlay, const SDK::AHUD* HUD)
    {
        if ((!Configuration::cfg_EnableAimbot && !Configuration::cfg_TeleportToTarget && !Configuration::cfg_EnableTPTarget) || !Overlay || !HUD || !HUD->Canvas)
            return;

        if (IsLevelTransitioning.load()) {
            return;
        }

        const auto PlayerController = GetARONPC();
        if (!PlayerController || !IsActorValid(PlayerController))
            return;

        const auto RONCharacter = GetARONCharacter();
        if (!RONCharacter || !IsActorValid(RONCharacter))
            return;

        FVector CharacterLocation = RONCharacter->K2_GetActorLocation();

        AActor* PawnPlayer = PlayerController->K2_GetPawn();
        if (!PawnPlayer || !IsActorValid(PawnPlayer))
            return;

        AReadyOrNotCharacter* LocalCharacter = static_cast<AReadyOrNotCharacter*>(PawnPlayer);
        if (!LocalCharacter || !IsActorValid(LocalCharacter))
            return;

        if (!PlayerController->PlayerCameraManager || !IsActorValid(PlayerController->PlayerCameraManager))
            return;
            
        FVector ViewPoint = PlayerController->PlayerCameraManager->GetCameraLocation();

        float ScreenWidth = HUD->Canvas->SizeX;
        float ScreenHeight = HUD->Canvas->SizeY;
        FVector2D FOVCenter = { ScreenWidth * 0.5f, ScreenHeight * 0.5f };

        double BestScore = DBL_MAX;
        double BestScoreOther = DBL_MAX;
        bool HasValidTarget = false;
        bool HasValidOtherTarget = false;

        AActor* CurrentBestTarget = nullptr;
        FVector BestBoneLocation;

        AActor* CurrentBestOtherTarget = nullptr;
        FVector BestOtherLocation;

        // Weight constants
        const double AngleWeight = 0.15;
        const double DistanceWeight = 0.05;
        const double MovementWeight = 0.05;

        const auto& allActors = ActorCache::GetInstance().GetCachedActors();

        for (const auto& actorInfo : allActors)
        {
            if (!actorInfo.Actor || !IsActorValid(actorInfo.Actor))
                continue;

            FVector ActorLocation = actorInfo.Actor->K2_GetActorLocation();
            FVector2D TargetScreenPosition;
            if (!PlayerController->ProjectWorldLocationToScreen(ActorLocation, &TargetScreenPosition, true))
                continue;

            float ScreenDistance = CustomMath::Sqrt(
                CustomMath::Square(TargetScreenPosition.X - FOVCenter.X) +
                CustomMath::Square(TargetScreenPosition.Y - FOVCenter.Y)
            );
            const bool teleportTargeting = Configuration::cfg_TeleportToTarget || Configuration::cfg_EnableTPTarget;
            float targetRadius = teleportTargeting ? Configuration::cfg_TPFOVRadius : Configuration::cfg_FOVRadius;
            if (teleportTargeting && Configuration::cfg_SmallerTPTargeting) targetRadius *= 0.5f;
            if (ScreenDistance > targetRadius)
                continue;

            bool shouldTarget = false;
            float maxDistance = Configuration::cfg_MaxPlayerESPDistance / 100.0f;
            
            switch (actorInfo.Category) {
                case EActorCategory::Player:
                    shouldTarget = false;
                    maxDistance = Configuration::cfg_MaxPlayerESPDistance / 100.0f;
                    break;
                case EActorCategory::Suspect:
                    shouldTarget = Configuration::cfg_DrawSuspectESP || teleportTargeting;
                    maxDistance = Configuration::cfg_MaxAIESPDistance / 100.0f;
                    break;
                case EActorCategory::SERT_Red:
                    shouldTarget = false;
                    maxDistance = Configuration::cfg_MaxAIESPDistance / 100.0f;
                    break;
                case EActorCategory::SERT_Blue:
                    shouldTarget = false;
                    maxDistance = Configuration::cfg_MaxAIESPDistance / 100.0f;
                    break;
                case EActorCategory::Civilian:
                    shouldTarget = false;
                    maxDistance = Configuration::cfg_MaxAIESPDistance / 100.0f;
                    break;
                case EActorCategory::Squad:
                    shouldTarget = false;
                    break;
                default:
                    shouldTarget = false;
                    break;
            }
            
            if (!shouldTarget)
                continue;

            AReadyOrNotCharacter* Character = static_cast<AReadyOrNotCharacter*>(actorInfo.Actor);
            if (!Character || !IsActorValid(Character))
                continue;

            if (Character == LocalCharacter || !Character->CharacterHealth || Character->GetHealthStatus() == EPlayerHealthStatus::HS_Dead)
                continue;

            float Distance = Character->GetDistanceTo(RONCharacter) / 100.0f;
            if (Distance > maxDistance)
                continue;

            if (Configuration::cfg_AimVisCheck && !PlayerController->LineOfSightTo(Character, ViewPoint, false))
                continue;

            FRotator ViewRotation = PlayerController->GetControlRotation();
            FVector ForwardVector = UKismetMathLibrary::GetForwardVector(ViewRotation);
            FVector DirectionToTarget = (ActorLocation - CharacterLocation).GetNormalized();
            double Angle = CustomMath::RadiansToDegrees(acosf(static_cast<float>(ForwardVector.Dot(DirectionToTarget))));

            FVector TargetVelocity = Character->GetVelocity();
            FVector RelativeVelocity = TargetVelocity - RONCharacter->GetVelocity();
            FVector RelativeDirection = RelativeVelocity.GetNormalized();

            // Bone logic
            FVector EvaluatedBoneLocation = ActorLocation;
            USkeletalMeshComponent* Mesh = Character->Mesh;

            if (Mesh && IsComponentValid(Mesh) && !Mesh->IsDefaultObject())
            {
                try {
                    FVector boneLocation = GetAimBoneLocation(Mesh, Configuration::cfg_AimbotTargetBone);
                
                    if (!boneLocation.IsZero())
                    {
                        EvaluatedBoneLocation = boneLocation;
                        // Keep the actual joint position at every distance.
                        // A distance-scaled height offset introduces a fixed
                        // angular bias and moves the aim above distant heads.
                    }
                }
                catch (...) {
                    // Silently handle errors
                }
            }

            // Weighted scoring
            double Score =
                (AngleWeight * Angle) +
                (DistanceWeight * Distance) +
                (MovementWeight * RelativeDirection.Magnitude());

            if (Angle <= Configuration::cfg_FOVRadius / 2.0 && Score < BestScore)
            {
                BestScore = Score;
                CurrentBestTarget = Character;
                BestBoneLocation = EvaluatedBoneLocation;
                HasValidTarget = true;
            }
        }

        if (HasValidTarget)
        {
            Overlay->BestTargetActor = CurrentBestTarget;
            Overlay->BestScore = BestScore;
            Overlay->BestTargetLocation = BestBoneLocation;
            Overlay->BestTargetRotation = UKismetMathLibrary::FindLookAtRotation(CharacterLocation, BestBoneLocation);
        }
        else if (HasValidOtherTarget)
        {
            Overlay->BestTargetOther = CurrentBestOtherTarget;
            Overlay->BestScoreOther = BestScoreOther;
            Overlay->BestTargetOtherLocation = BestOtherLocation;
            Overlay->BestTargetOtherRotation = UKismetMathLibrary::FindLookAtRotation(CharacterLocation, BestOtherLocation);
        }
        else
        {
            Overlay->BestTargetActor = nullptr;
            Overlay->BestScore = DBL_MAX;
            Overlay->BestTargetLocation = FVector{0.0f, 0.0f, 0.0f};
            Overlay->BestTargetRotation = FRotator{0.0f, 0.0f, 0.0f};

            Overlay->BestTargetOther = nullptr;
            Overlay->BestScoreOther = DBL_MAX;
            Overlay->BestTargetOtherLocation = FVector{0.0f, 0.0f, 0.0f};
            Overlay->BestTargetOtherRotation = FRotator{0.0f, 0.0f, 0.0f};
        }
    }

    void DoAimbot(ReadyornotOverlay* Overlay, const SDK::AHUD* HUD) {
        if (!Configuration::cfg_EnableAimbot || !Overlay || !HUD || !HUD->Canvas)
            return;

        if (!IsGameWindowActive())
            return;

        if (IsLevelTransitioning.load()) {
            return;
        }

        const auto PlayerController = GetARONPC();
        if (!PlayerController || !IsActorValid(PlayerController))
            return;

        const auto RONCharacter = GetARONCharacter();
        if (!RONCharacter || !IsActorValid(RONCharacter))
            return;

        AActor* TargetActor = Overlay->BestTargetActor;
        if (!TargetActor || !IsActorValid(TargetActor))
            return;

        auto* targetCharacter = static_cast<AReadyOrNotCharacter*>(TargetActor);
        if (!targetCharacter->Mesh || !IsComponentValid(targetCharacter->Mesh)
            || targetCharacter->Mesh->IsDefaultObject()) return;
        // Refresh the selected joint at the time of aiming so movement,
        // crouching, and changing distance use the same anatomical point.
        const FVector BestTargetLocation = GetAimBoneLocation(targetCharacter->Mesh, Configuration::cfg_AimbotTargetBone);
        if (BestTargetLocation.IsZero()) return;

        const auto CameraManager = PlayerController->PlayerCameraManager;
        if (!CameraManager || !IsActorValid(CameraManager))
            return;

        FVector CameraLocation = CameraManager->GetCameraLocation();
        FRotator TargetRotation = UKismetMathLibrary::FindLookAtRotation(CameraLocation, BestTargetLocation);
        FRotator CurrentControlRotation = PlayerController->GetControlRotation();

        // Regular aimbot logic
        float SmoothingFactor = UKismetMathLibrary::Clamp(Configuration::cfg_AimbotSmoothing, 1.0f, 100.0f);
        bool bAimbotKeyHeld = Hotkey::IsPressed(Configuration::cfg_AimbotKey);
        Overlay->AimbotInUse = bAimbotKeyHeld;

        AReadyOrNotCharacter* TargetPlayer = static_cast<AReadyOrNotCharacter*>(TargetActor);
        if (TargetPlayer && IsActorValid(TargetPlayer) && 
            TargetPlayer->Mesh && IsComponentValid(TargetPlayer->Mesh) && !TargetPlayer->Mesh->IsDefaultObject()) {
            if (!bAimbotKeyHeld)
                return;

            if (SmoothingFactor == 1.0f) {
                PlayerController->SetControlRotation(TargetRotation);
            }
            else {
                float InterpSpeed = UKismetMathLibrary::MapRangeClamped(SmoothingFactor, 1.0f, 100.0f, 25.0f, 1.0f);
                float DeltaTime = UGameplayStatics::GetWorldDeltaSeconds(GetWorld());
                FRotator SmoothedRotation = UKismetMathLibrary::RInterpTo(CurrentControlRotation, TargetRotation, DeltaTime, InterpSpeed);

                PlayerController->SetControlRotation(SmoothedRotation);
            }
        }
    }

    void DoInfiniteAmmo() {
        if (!Configuration::cfg_InfiniteAmmo)
            return;

        if (IsLevelTransitioning.load()) {
            return;
        }

        auto RONCharacter = GetARONCharacter();
        if (!RONCharacter || !IsActorValid(RONCharacter))
            return;

        ABaseWeapon* CurrentWeapon = GetCurrentWeapon();
        if (!CurrentWeapon || !IsActorValid(CurrentWeapon)) {
            return;
        }

        static ABaseWeapon* LastProcessedWeapon = nullptr;
        if (LastProcessedWeapon != CurrentWeapon) {
            
            LastProcessedWeapon = CurrentWeapon;
            InvalidateWeaponCache();
            CurrentWeapon = GetCurrentWeapon();

            if (!CurrentWeapon || !IsActorValid(CurrentWeapon)) {
                return;
            }
        }

        ABaseMagazineWeapon* MagazineWeapon = static_cast<ABaseMagazineWeapon*>(CurrentWeapon);
        if (MagazineWeapon && IsActorValid(MagazineWeapon)) {
            try {
                TArray<FName> AmmoTypes;
                if (MagazineWeapon->AmmunitionTypes.Num() > 0) {
                    AmmoTypes = MagazineWeapon->AmmunitionTypes;
                }

                int32 maxMags = MagazineWeapon->MagazineCountMax;
                if (maxMags > 0) {
                    MagazineWeapon->SetMagazineCount(maxMags, AmmoTypes);
                }

                MagazineWeapon->ReplenishAmmo();
                
            } catch (...) {
                // Silently handle errors
            }
        }
    }

    void ApplyWeaponOptions() {
        if (IsLevelTransitioning.load()) return;
        auto* weapon = GetCurrentWeapon();
        struct SavedWeaponValues {
            ABaseWeapon* weapon = nullptr;
            float damage = 0.0f;
            float fireRate = 0.0f;
            float penetration = 0.0f;
            uint8_t ammoPenetrationLevel = 0;
            float ammoPenetrationDistance = 0.0f;
            EFireMode fireMode = EFireMode::FM_Single;
            bool infiniteAmmo = false;
            bool saved = false;
        };
        static SavedWeaponValues original{};
        auto restore = [&]() {
            if (!original.saved || !original.weapon || !IsActorValid(original.weapon)) return;
            original.weapon->Damage = original.damage;
            original.weapon->FireRate = original.fireRate;
            if (original.weapon->IsA(ABaseMagazineWeapon::StaticClass())) {
                auto* oldMagazineWeapon = static_cast<ABaseMagazineWeapon*>(original.weapon);
                oldMagazineWeapon->PenetrationDistance = original.penetration;
                oldMagazineWeapon->CurrentAmmoType.PenetrationLevel = original.ammoPenetrationLevel;
                oldMagazineWeapon->CurrentAmmoType.PenetrationDistance = original.ammoPenetrationDistance;
                oldMagazineWeapon->CurrentFireMode = original.fireMode;
                oldMagazineWeapon->bInfiniteAmmo = original.infiniteAmmo;
            }
        };
        if (!weapon || !IsActorValid(weapon)) {
            restore();
            original = {};
            return;
        }
        if (original.weapon != weapon) {
            restore();
            original = {};
            original.weapon = weapon;
            original.damage = weapon->Damage;
            original.fireRate = weapon->FireRate;
            if (weapon->IsA(ABaseMagazineWeapon::StaticClass())) {
                auto* magazineWeapon = static_cast<ABaseMagazineWeapon*>(weapon);
                original.penetration = magazineWeapon->PenetrationDistance;
                original.ammoPenetrationLevel = magazineWeapon->CurrentAmmoType.PenetrationLevel;
                original.ammoPenetrationDistance = magazineWeapon->CurrentAmmoType.PenetrationDistance;
                original.fireMode = magazineWeapon->CurrentFireMode;
                original.infiniteAmmo = magazineWeapon->bInfiniteAmmo;
            }
            original.saved = true;
        }
        if (!original.saved) return;

        weapon->Damage = Configuration::cfg_OneHitKill ? 10000.0f : original.damage;
        weapon->FireRate = Configuration::cfg_CustomFireRate && Configuration::cfg_CustomFireRateRPM > 0.0f
            ? 60.0f / Configuration::cfg_CustomFireRateRPM : original.fireRate;
        if (!weapon->IsA(ABaseMagazineWeapon::StaticClass())) return;
        auto* magazineWeapon = static_cast<ABaseMagazineWeapon*>(weapon);
        // Wall hits now use the authority-only hitscan path; preserve normal ammo values.
        magazineWeapon->bInfiniteAmmo = Configuration::cfg_InfiniteAmmo ? 1 : original.infiniteAmmo;
        if (Configuration::cfg_AutoFire) {
            bool hasAuto = false;
            for (const auto mode : magazineWeapon->AvailableFireModes)
                if (mode == EFireMode::FM_Auto) { hasAuto = true; break; }
            if (!hasAuto) magazineWeapon->AvailableFireModes.Add(EFireMode::FM_Auto);
            magazineWeapon->CurrentFireMode = EFireMode::FM_Auto;
        } else {
            magazineWeapon->CurrentFireMode = original.fireMode;
        }
    }

    void RemoveWeaponEffects() {
        if (!Configuration::cfg_AimbotNoSpread)
            return;

        if (IsLevelTransitioning.load()) {
            return;
        }

        static bool OriginalValuesSaved = false;
        static float OriginalSpreadValue;
        static float OriginalSpreadMin;
        static float OriginalSpreadMax;
        static float OriginalSpreadModValue1;
        static float OriginalSpreadModValue2;
        static float OriginalWobble;
        static float OriginalInitialWobbleDelay;
        static float OriginalMovementSpeedMultiplier;
        static ABaseWeapon* LastProcessedWeapon = nullptr;

        auto RONCharacter = GetARONCharacter();
        if (!RONCharacter || !IsActorValid(RONCharacter))
            return;

        ABaseWeapon* CurrentWeapon = GetCurrentWeapon();
        if (!CurrentWeapon || !IsActorValid(CurrentWeapon)) {
            return;
        }

        if (LastProcessedWeapon != CurrentWeapon) {
            LastProcessedWeapon = CurrentWeapon;
            OriginalValuesSaved = false;
        }

        try {
            if (!OriginalValuesSaved) {
                OriginalSpreadValue = CurrentWeapon->SpreadPattern.Pitch;
                OriginalSpreadMin = CurrentWeapon->SpreadPattern.Yaw;
                OriginalSpreadMax = CurrentWeapon->SpreadPattern.Roll;
                OriginalSpreadModValue1 = CurrentWeapon->FirstShotSpread;
                OriginalSpreadModValue2 = CurrentWeapon->ADSSpreadMultiplier;
                OriginalWobble = CurrentWeapon->Wobble;
                OriginalInitialWobbleDelay = CurrentWeapon->InitialWobbleDelay;
                OriginalMovementSpeedMultiplier = CurrentWeapon->MovementSpeedMultiplier;
                OriginalValuesSaved = true;
            }

            // no spread modifications
            if (Configuration::cfg_AimbotNoSpread) {
                CurrentWeapon->SpreadPattern.Pitch = 0.0f;
                CurrentWeapon->SpreadPattern.Yaw = 0.0f; 
                CurrentWeapon->SpreadPattern.Roll = 0.0f;
                CurrentWeapon->FirstShotSpread = 0.0f;
                CurrentWeapon->ADSSpreadMultiplier = 0.0f;
                CurrentWeapon->VelocitySpreadMultiplier = 0.0f;
                
                // pending spread zero
                CurrentWeapon->PendingSpread.Pitch = 0.0f;
                CurrentWeapon->PendingSpread.Yaw = 0.0f;
                CurrentWeapon->PendingSpread.Roll = 0.0f;
            }

            // no recoil modifications
            if (CurrentWeapon->RecoilPattern.Num() > 0) {
                for (int32 i = 0; i < CurrentWeapon->RecoilPattern.Num(); i++) {
                    CurrentWeapon->RecoilPattern[i].Pitch = 0.0f;
                    CurrentWeapon->RecoilPattern[i].Yaw = 0.0f;
                    CurrentWeapon->RecoilPattern[i].Roll = 0.0f;
                }
            }

            // recoil multipliers
            CurrentWeapon->ADSRecoilMultiplier = 0.0f;
            CurrentWeapon->RecoilMultiplierPitch = 0.0f;
            CurrentWeapon->RecoilMultiplierYaw = 0.0f;
            CurrentWeapon->FirstShotRecoil = 0.0f;
            CurrentWeapon->VelocityRecoilMultiplier = 0.0f;

            // procedural recoil
            CurrentWeapon->RecoilFireStrength = 0.0f;
            CurrentWeapon->RecoilFireStrengthFirst = 0.0f;
            CurrentWeapon->RecoilAngleStrength = 0.0f;
            CurrentWeapon->RecoilRandomness = 0.0f;

            // Clear recoil buildup
            CurrentWeapon->RecoilRotationBuildup.Pitch = 0.0f;
            CurrentWeapon->RecoilRotationBuildup.Yaw = 0.0f;
            CurrentWeapon->RecoilRotationBuildup.Roll = 0.0f;
            CurrentWeapon->RecoilPositionBuildup.X = 0.0f;
            CurrentWeapon->RecoilPositionBuildup.Y = 0.0f;
            CurrentWeapon->RecoilPositionBuildup.Z = 0.0f;

            // procedural recoil runtime
            CurrentWeapon->ProcRecoil_Trans.X = 0.0f;
            CurrentWeapon->ProcRecoil_Trans.Y = 0.0f;
            CurrentWeapon->ProcRecoil_Trans.Z = 0.0f;
            CurrentWeapon->ProcRecoil_Rot.Pitch = 0.0f;
            CurrentWeapon->ProcRecoil_Rot.Yaw = 0.0f;
            CurrentWeapon->ProcRecoil_Rot.Roll = 0.0f;
            CurrentWeapon->ProcRecoil_Trans_Buildup.X = 0.0f;
            CurrentWeapon->ProcRecoil_Trans_Buildup.Y = 0.0f;
            CurrentWeapon->ProcRecoil_Trans_Buildup.Z = 0.0f;
            CurrentWeapon->ProcRecoil_Rot_Buildup.Pitch = 0.0f;
            CurrentWeapon->ProcRecoil_Rot_Buildup.Yaw = 0.0f;
            CurrentWeapon->ProcRecoil_Rot_Buildup.Roll = 0.0f;

            // weapon sway and wobble effects
            CurrentWeapon->Wobble = 0.0f;
            CurrentWeapon->InitialWobbleDelay = 0.0f;

            // movement speed penalties
            CurrentWeapon->MovementSpeedMultiplier = 1.0f;

            // ADS modifiers for better handling
            CurrentWeapon->RecoilFireADSModifier = 0.0f;
            CurrentWeapon->RecoilAngleADSModifier = 0.0f;
            CurrentWeapon->RecoilBuildupADSModifier = 0.0f;

            // Improve weapon handling by removing delays and penalties
            CurrentWeapon->RefireDelay = 0.0f;
            CurrentWeapon->FirstShotResetTime = 0.0f;

            // recoil return settings 
            CurrentWeapon->RecoilReturnRate = 100.0f;
            CurrentWeapon->RecoilReturnPercentage = 1.0f;
            CurrentWeapon->RecoilReturnInterpSpeed = 100.0f;
            CurrentWeapon->SpreadReturnRate = 100.0f;

            // recoil interpolation and damping
            CurrentWeapon->RecoilInterpSpeed = 100.0f;
            CurrentWeapon->RecoilDampStrength = 0.0f;
            CurrentWeapon->RecoilBuildupDampStrength = 0.0f;

            // recoil buildup system
            CurrentWeapon->RecoilHasBuildup = false;

            // fire time effects
            CurrentWeapon->RecoilFireTime = 0.0f;

            if (!Configuration::cfg_AimbotNoSpread && OriginalValuesSaved) {
                CurrentWeapon->SpreadPattern.Pitch = OriginalSpreadValue;
                CurrentWeapon->SpreadPattern.Yaw = OriginalSpreadMin;
                CurrentWeapon->SpreadPattern.Roll = OriginalSpreadMax;
                CurrentWeapon->FirstShotSpread = OriginalSpreadModValue1;
                CurrentWeapon->ADSSpreadMultiplier = OriginalSpreadModValue2;
                CurrentWeapon->Wobble = OriginalWobble;
                CurrentWeapon->InitialWobbleDelay = OriginalInitialWobbleDelay;
                CurrentWeapon->MovementSpeedMultiplier = OriginalMovementSpeedMultiplier;
                OriginalValuesSaved = false;
            }

        } catch (...) {
            // Silently handle errors
        }
    }

} // namespace AimFeatures
