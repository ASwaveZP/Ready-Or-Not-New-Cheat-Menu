#include "AimFeatures.h"
#include "ActorCache.h"
#include "Utils.h"
#include "Hotkey.h"
#include "InputOwnership.h"
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

    void DoTriggerBot() {
        static APlayerCharacter* firingPlayer = nullptr;
        static ABaseMagazineWeapon* firingWeapon = nullptr;
        static auto nextPress = std::chrono::steady_clock::time_point{};
        if (!Configuration::cfg_TriggerBot && !firingPlayer) return;

        auto release = [&]() {
            auto* previous = firingPlayer;
            firingPlayer = nullptr;
            firingWeapon = nullptr;
            if (previous && IsActorValid(previous)) previous->EndPrimaryUse();
        };
        // During travel the old world's actors must not receive input calls.
        if (IsLevelTransitioning.load()) {
            firingPlayer = nullptr;
            firingWeapon = nullptr;
            nextPress = {};
            return;
        }
        if (!Configuration::cfg_TriggerBot || InputOwnership::IsMenuOpen() || !IsGameWindowActive()) {
            release();
            return;
        }
        // Hand control back to a physical trigger press without releasing it.
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
            firingPlayer = nullptr;
            firingWeapon = nullptr;
            return;
        }
        auto* character = GetARONCharacter();
        auto* controller = GetARONPC();
        if (!character || !IsActorValid(character) || !character->IsA(APlayerCharacter::StaticClass())
            || character->IsDeadOrUnconscious() || !controller || !IsActorValid(controller)) {
            release();
            return;
        }
        auto* player = static_cast<APlayerCharacter*>(character);
        auto* equipped = GetCurrentWeapon();
        if (!equipped || !IsActorValid(equipped) || !equipped->IsA(ABaseMagazineWeapon::StaticClass())) {
            release();
            return;
        }
        auto* weapon = static_cast<ABaseMagazineWeapon*>(equipped);
        if (firingPlayer && (firingPlayer != player || firingWeapon != weapon)) release();
        if (weapon->Owner != player || weapon->bReloading || weapon->IsCurrentlyReloading()
            || weapon->IsPlayingDraw() || weapon->IsPlayingHolster() || weapon->IsDepleted()) {
            release();
            return;
        }
        int32 width = 0, height = 0;
        controller->GetViewportSize(&width, &height);
        FVector origin{}, direction{};
        if (width <= 0 || height <= 0 || !controller->DeprojectScreenPositionToWorld(
            width * 0.5f, height * 0.5f, &origin, &direction)) {
            release();
            return;
        }
        TArray<AActor*> ignored;
        ignored.Add(player);
        ignored.Add(weapon);
        FHitResult hit{};
        const bool blocked = UKismetSystemLibrary::LineTraceSingle(player, origin, origin + direction * 1000000.0,
            ETraceTypeQuery::TraceTypeQuery1, true, ignored, EDrawDebugTrace::None, &hit, true, {}, {}, 0.0f);
        auto* target = blocked && hit.bBlockingHit ? hit.HitObjectHandle.Actor.Get() : nullptr;
        if (!target || !IsActorValid(target) || !target->IsA(ASuspectCharacter::StaticClass())) {
            release();
            return;
        }
        auto* suspect = static_cast<ASuspectCharacter*>(target);
        if (suspect->IsDeadOrUnconscious() || suspect->IsIncapacitated() || suspect->IsArrestedOrSurrendered()) {
            release();
            return;
        }
        // Full auto uses the game's held-trigger behavior. Semi/burst modes
        // release on the next frame and re-press no faster than the fire rate.
        if (firingPlayer) {
            if (weapon->CurrentFireMode != EFireMode::FM_Auto) release();
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now < nextPress) return;
        const double interval = std::isfinite(weapon->FireRate) ? (std::max)(0.01, static_cast<double>(weapon->FireRate)) : 0.1;
        nextPress = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(interval));
        firingPlayer = player;
        firingWeapon = weapon;
        player->PrimaryUse();
    }

    namespace {
        FName GetAimBoneName(USkeletalMeshComponent* mesh, int selection) {
            if (!mesh || !mesh->SkeletalMesh) return {};
            if (selection < 0 || selection > 4) selection = 0;
            struct AimBones { FName names[5]{}; bool resolved[5]{}; bool valid[5]{}; };
            static thread_local std::unordered_map<USkeletalMesh*, AimBones> boneCache;
            if (boneCache.size() >= 256) boneCache.clear();
            auto& cached = boneCache[mesh->SkeletalMesh];
            if (cached.resolved[selection])
                return cached.valid[selection] ? cached.names[selection] : FName{};
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
                    return boneName;
                }
            }
            return {};
        }

        FVector GetAimBoneLocation(USkeletalMeshComponent* mesh, int selection) {
            const FName boneName = GetAimBoneName(mesh, selection);
            if (boneName == FName{}) return {};
            return mesh->GetSocketLocation(boneName);
        }

        bool IntersectRaySphere(const FVector& origin, const FVector& dir, const FVector& center, double radius, double& outT) {
            const FVector oc = center - origin;
            const double tCa = oc.Dot(dir);
            const double d2 = oc.Dot(oc) - tCa * tCa;
            const double r2 = radius * radius;
            if (d2 > r2) return false;
            const double tHc = std::sqrt((std::max)(0.0, r2 - d2));
            double t = tCa - tHc;
            if (t < 0.0) t = tCa + tHc;
            if (t < 0.0 || !std::isfinite(t)) return false;
            outT = t;
            return true;
        }

        bool TryAuthorityRayHit(ABaseMagazineWeapon* weapon, const FVector& origin, const FVector& rawDirection) {
            if (!Configuration::cfg_BulletPenetration || IsLevelTransitioning.load()
                || !weapon || !IsActorValid(weapon)) return false;
            auto* player = GetARONCharacter();
            auto* controller = GetARONPC();
            if (!player || !IsActorValid(player) || !controller || !IsActorValid(controller)
                || weapon->Owner != player || !player->HasAuthority() || !weapon->HasAuthority()) return false;

            const double length = std::sqrt(rawDirection.X * rawDirection.X + rawDirection.Y * rawDirection.Y + rawDirection.Z * rawDirection.Z);
            if (!std::isfinite(length) || length < 0.000001) return false;
            const FVector direction = rawDirection * (1.0 / length);

            // Query character meshes individually and fall back to bone/capsule
            // intersection when occluded/distant skeletal meshes skip physics traces.
            const auto suspects = ActorCache::GetInstance().GetCachedActors();
            AReadyOrNotCharacter* nearest = nullptr;
            FHitResult nearestHit{};
            double nearestDistance = DBL_MAX;

            static constexpr struct { int slot; double radius; } kBoneSpheres[] = {
                { 0, 14.0 }, // head
                { 1, 14.0 }, // neck
                { 2, 24.0 }, // upper chest
                { 3, 22.0 }, // lower spine
            };

            for (const auto& info : suspects) {
                if (info.Category != EActorCategory::Suspect) continue;
                auto* actor = info.Actor;
                if (!actor || !IsActorValid(actor)) continue;
                auto* target = static_cast<AReadyOrNotCharacter*>(actor);
                if (target->IsDeadOrUnconscious() || !target->Mesh) continue;

                FVector center{}, extent{};
                float sphereRadius = 0.0f;
                UKismetSystemLibrary::GetComponentBounds(target->Mesh, &center, &extent, &sphereRadius);
                if (sphereRadius <= 0.0f) center = target->K2_GetActorLocation();

                const FVector delta = center - origin;
                const double along = delta.X * direction.X + delta.Y * direction.Y + delta.Z * direction.Z;
                const double radius = (std::max)(static_cast<double>(sphereRadius), 95.0) + 15.0;
                if (!std::isfinite(along) || along + radius <= 0.0) continue;
                const FVector closest = origin + direction * along;
                if (center.GetDistanceTo(closest) > radius) continue;

                const FVector start = origin + direction * (std::max)(0.0, along - radius);
                const FVector end = origin + direction * (along + radius);
                FVector location{}, normal{};
                FName bone{};
                FHitResult hit{};
                double distance = DBL_MAX;

                // Use bTraceComplex=false so UE5 traces the SkeletalMesh PhysicsAsset.
                if (target->Mesh->K2_LineTraceComponent(start, end, false, false, false, &location, &normal, &bone, &hit)) {
                    distance = origin.GetDistanceTo(location);
                } else {
                    // Fallback for occluded or distant suspects whose mesh physics are culled.
                    double bestT = DBL_MAX;
                    FName bestBone{};
                    for (const auto& entry : kBoneSpheres) {
                        const FName boneName = GetAimBoneName(target->Mesh, entry.slot);
                        if (boneName == FName{}) continue;
                        const FVector bonePos = target->Mesh->GetSocketLocation(boneName);
                        if (bonePos.IsZero()) continue;
                        double t = 0.0;
                        if (IntersectRaySphere(origin, direction, bonePos, entry.radius, t) && t < bestT) {
                            bestT = t;
                            bestBone = boneName;
                        }
                    }
                    if (bestT >= DBL_MAX) {
                        double t = 0.0;
                        if (!IntersectRaySphere(origin, direction, center, 36.0, t)) continue;
                        bestT = t;
                    }
                    distance = bestT;
                    const FVector hitPos = origin + direction * bestT;
                    hit.bBlockingHit = true;
                    hit.Location.X = hitPos.X;
                    hit.Location.Y = hitPos.Y;
                    hit.Location.Z = hitPos.Z;
                    hit.ImpactPoint.X = hitPos.X;
                    hit.ImpactPoint.Y = hitPos.Y;
                    hit.ImpactPoint.Z = hitPos.Z;
                    hit.Normal.X = -direction.X;
                    hit.Normal.Y = -direction.Y;
                    hit.Normal.Z = -direction.Z;
                    hit.ImpactNormal.X = -direction.X;
                    hit.ImpactNormal.Y = -direction.Y;
                    hit.ImpactNormal.Z = -direction.Z;
                    hit.BoneName = bestBone;
                }

                if (distance >= nearestDistance) continue;
                nearest = target;
                nearestHit = hit;
                nearestHit.TraceStart.X = origin.X;
                nearestHit.TraceStart.Y = origin.Y;
                nearestHit.TraceStart.Z = origin.Z;
                nearestHit.TraceEnd.X = end.X;
                nearestHit.TraceEnd.Y = end.Y;
                nearestHit.TraceEnd.Z = end.Z;
                nearestHit.Distance = static_cast<float>(distance);
                nearestDistance = distance;
            }
            if (!nearest) return false;

            float damage = Configuration::cfg_OneHitKill ? 10000.0f : weapon->CurrentAmmoType.Damage;
            if (!std::isfinite(damage) || damage <= 0.0f) {
                damage = weapon->Damage > 0.0f ? weapon->Damage : weapon->DefaultDamage;
            }
            if (!std::isfinite(damage) || damage <= 0.0f) damage = 100.0f;

            TSubclassOf<UDamageType> damageType = weapon->DamageType;
            if (!damageType) damageType = weapon->CurrentAmmoType.DamageTypeOverride;
            if (!damageType) damageType = weapon->DefaultDamageType;
            if (!damageType) damageType = UDamageType::StaticClass();

            UGameplayStatics::ApplyPointDamage(nearest, damage, direction, nearestHit, controller, weapon, damageType);
            return true;
        }
    }

    bool TryAuthorityWallHit(ABaseMagazineWeapon* weapon, const FHitscanShot& shot, bool localOnly) {
        if (!weapon || !IsActorValid(weapon) || !weapon->bHitScan) return false;
        // Let local prediction FX run without applying duplicate damage or blocking normal shots.
        if (localOnly) return false;
        return TryAuthorityRayHit(weapon, shot.Location, shot.Direction);
    }

    void TryAuthorityProjectileWallHit(ABaseMagazineWeapon* weapon, const FVector& origin, const FRotator& rotation) {
        if (!weapon || !IsActorValid(weapon) || weapon->bHitScan) return;
        const FVector direction = UKismetMathLibrary::GetForwardVector(rotation);
        TryAuthorityRayHit(weapon, origin, direction);
    }

    void DoTargeting(ReadyornotOverlay* Overlay, const SDK::AHUD* HUD)
    {
        if (!Configuration::cfg_EnableAimbot || !Overlay || !HUD || !HUD->Canvas) {
            if (Overlay) Overlay->BestTargetActor = nullptr;
            return;
        }

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
            if (ScreenDistance > Configuration::cfg_FOVRadius)
                continue;

            bool shouldTarget = false;
            float maxDistance = Configuration::cfg_MaxPlayerESPDistance / 100.0f;
            
            switch (actorInfo.Category) {
                case EActorCategory::Player:
                    shouldTarget = false;
                    maxDistance = Configuration::cfg_MaxPlayerESPDistance / 100.0f;
                    break;
                case EActorCategory::Suspect:
                    shouldTarget = Configuration::cfg_DrawSuspectESP || Configuration::cfg_EnableAimbot;
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
