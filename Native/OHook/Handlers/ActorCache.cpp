#include "ActorCache.h"
#include <Windows.h>
#include "ReadyornotOverlay.h"
#include "SDKExt.h"
#include "Configuration.h"

#include <SDK.hpp>
#include <mutex>
#include <atomic>

using namespace SDK;

ActorCache& ActorCache::GetInstance() {
    static ActorCache instance;
    return instance;
}

ActorCache::ActorCache()
    : LastCachedTime(0.0),
    LastRetryTime(std::chrono::steady_clock::now()) {
}

void ActorCache::Initialize(ReadyornotOverlay* overlay) {
    OverlayInstance = overlay;
    CachedActors.clear();
    BackBufferActors.clear();
    
    LastCachedTime = 0.0;
    LastRetryTime = std::chrono::steady_clock::now();
    bCacheUpdateInProgress.store(false);
}

void ActorCache::ClearCacheDuringLevelTransition() {
    std::lock_guard<std::mutex> lock(CacheMutex);

    CachedActors.clear();
    BackBufferActors.clear();
    ActorHashes.clear();
    CachedActorSet.clear();

    LastCachedTime = 0.0;
    LastRetryTime = std::chrono::steady_clock::now();

    bCacheUpdateInProgress.store(false);
    
    if (OverlayInstance) {
        OverlayInstance->BestTargetActor = nullptr;
        OverlayInstance->BestTargetOther = nullptr;
        OverlayInstance->TeleportTargetActor = nullptr;
        OverlayInstance->TeleportTargetName.clear();
    }
}

void ActorCache::Update() {
    if (bCacheUpdateInProgress.load()) return;

    auto* World = GetWorld();
    if (!World) return;

    extern std::atomic<bool> IsLevelTransitioning;
    if (IsLevelTransitioning.load()) {
        ClearCacheDuringLevelTransition();
        return;
    }

    auto* PlayerController = GetARONPCForESP();
    if (!PlayerController) return;

    AActor* LocalPlayer = reinterpret_cast<AActor*>(reinterpret_cast<AController*>(PlayerController)->K2_GetPawn());
    if (!LocalPlayer || !IsActorValid(reinterpret_cast<AActor*>(LocalPlayer))) return;

    double CurrentTime = UGameplayStatics::GetTimeSeconds(reinterpret_cast<UObject*>(World));
    if (fabs(CurrentTime - LastCachedTime) > CacheIntervalSeconds && !bCacheUpdateInProgress.load()) {
        LastCachedTime = CurrentTime;
        
        CacheActors();
    }
}

void ActorCache::ForceCacheUpdate() {
    if (!bCacheUpdateInProgress.load()) {
        CacheActors();
    }
}

void ActorCache::CacheActors() {
    if (bCacheUpdateInProgress.load())
        return;

    bCacheUpdateInProgress.store(true);

    auto* World = GetWorld();
    if (!World) {
        bCacheUpdateInProgress.store(false);
        return;
    }

    extern std::atomic<bool> IsLevelTransitioning;
    if (IsLevelTransitioning.load()) {
        bCacheUpdateInProgress.store(false);
        return;
    }

    auto* PlayerController = GetARONPCForESP();
    if (!PlayerController) {
        bCacheUpdateInProgress.store(false);
        return;
    }

    AActor* LocalPlayer = reinterpret_cast<AActor*>(reinterpret_cast<AController*>(PlayerController)->K2_GetPawn());
    if (!LocalPlayer || !IsActorValid(reinterpret_cast<AActor*>(LocalPlayer))) {
        bCacheUpdateInProgress.store(false);
        return;
    }

    std::vector<CachedActorInfo> newActors;
    newActors.reserve(2048);

    // ==========================
    // Process Ready or Not Characters
    // ==========================
    if (Configuration::cfg_DrawPlayerESP || Configuration::cfg_DrawSERTRedESP || Configuration::cfg_DrawSERTBlueESP || Configuration::cfg_DrawSuspectESP || Configuration::cfg_DrawCivilianESP || Configuration::cfg_DrawSquadESP || Configuration::cfg_DrawWoundedESP
        || Configuration::cfg_DrawPlayerCorpses || Configuration::cfg_DrawAICorpses || Configuration::cfg_DrawSuspectCorpses || Configuration::cfg_DrawCivilianCorpses
        || Configuration::cfg_NoSuspectFirefight || Configuration::cfg_DisarmAllSuspects || Configuration::cfg_ForceSuspectSurrender || Configuration::cfg_AutoComplySuspects || Configuration::cfg_EnableAimbot
        || Configuration::cfg_TeleportToTarget || Configuration::cfg_EnableTPTarget || Configuration::cfg_BulletPenetration
        || Configuration::cfg_DrawHeldItemsESP) {
        
        TArray<AActor*> FoundCharacters;
        UGameplayStatics::GetAllActorsOfClass(reinterpret_cast<UObject*>(World), AReadyOrNotCharacter::StaticClass(), &FoundCharacters);

        for (AActor* A : FoundCharacters) {
            if (IsLevelTransitioning.load()) break;
            
            auto* Character = reinterpret_cast<AReadyOrNotCharacter*>(A);
            if (!Character || !IsActorValid(reinterpret_cast<AActor*>(Character)))
                continue;

            // Read the active hand slot, never enumerate a character's inventory.
            if (Configuration::cfg_DrawHeldItemsESP && !Character->IsDeadOrUnconscious()) {
                auto* item = Character->GetEquippedItem();
                if (item && IsActorValid(item) && item->Owner == Character
                    && item->IsEquipped() && !item->IsPlayingHolster()) {
                    auto held = ProcessItem(item, PlayerController, LocalPlayer);
                    held.Category = EActorCategory::HeldItems;
                    held.ItemHolder = Character;
                    newActors.push_back(held);
                }
            }
            if (Character == LocalPlayer) continue;
            
            CachedActorInfo info = ProcessCharacter(Character, PlayerController, LocalPlayer);
            if (info.Actor) {
                newActors.push_back(info);
            }
        }
        
        // The base-character query already includes suspects and civilians.
    }

    // ==========================
    // Process Evidence Actors
    // ==========================
    if (Configuration::cfg_DrawEvidenceESP || Configuration::cfg_DrawCollectedEvidenceESP || Configuration::cfg_EnableTPTarget || Configuration::cfg_TeleportToTarget) {
        // Search for regular evidence actors (AEvidenceActor)
        TArray<AActor*> FoundEvidence;
        UGameplayStatics::GetAllActorsOfClass(reinterpret_cast<UObject*>(World), AEvidenceActor::StaticClass(), &FoundEvidence);

        for (AActor* A : FoundEvidence) {
            if (IsLevelTransitioning.load()) break;
            
            auto* Evidence = reinterpret_cast<AEvidenceActor*>(A);
            if (!Evidence || !IsActorValid(reinterpret_cast<AActor*>(Evidence)))
                continue;
            
            CachedActorInfo info = ProcessEvidence(Evidence, PlayerController, LocalPlayer);
            if (info.Actor) {
                newActors.push_back(info);
            }
        }
        
        // Search for collected evidence actors (ACollectedEvidenceActor)
        TArray<AActor*> FoundCollectedEvidence;
        UGameplayStatics::GetAllActorsOfClass(reinterpret_cast<UObject*>(World), ACollectedEvidenceActor::StaticClass(), &FoundCollectedEvidence);

        for (AActor* A : FoundCollectedEvidence) {
            if (IsLevelTransitioning.load()) break;
            
            auto* CollectedEvidence = reinterpret_cast<ACollectedEvidenceActor*>(A);
            if (!CollectedEvidence || !IsActorValid(reinterpret_cast<AActor*>(CollectedEvidence)))
                continue;
            
            CachedActorInfo info = ProcessCollectedEvidence(CollectedEvidence, PlayerController, LocalPlayer);
            if (info.Actor) {
                newActors.push_back(info);
            }
        }
    }

    // Dropped evidence firearms only. Equipped and inventory items are handled
    // by the character's active hand slot above, not this weapon query.
    if (Configuration::cfg_DrawItemsESP || Configuration::cfg_DrawEvidenceESP) {
        TArray<AActor*> weapons;
        UGameplayStatics::GetAllActorsOfClass(reinterpret_cast<UObject*>(World), ABaseWeapon::StaticClass(), &weapons);
        for (auto* actor : weapons) {
            if (IsLevelTransitioning.load()) break;
            if (!actor || !IsActorValid(actor)) continue;
            auto* weapon = static_cast<ABaseWeapon*>(actor);
            auto* evidence = weapon->EvidenceComponent;
            if (weapon->IsEquipped() || !evidence || !IsComponentValid(evidence) || evidence->IsEvidenceCollected()
                || (!evidence->CanBeCollected() && !evidence->IsBeingCollected())) continue;
            auto info = ProcessItem(weapon, PlayerController, LocalPlayer);
            // One marker when both weapons and evidence are enabled.
            if (!Configuration::cfg_DrawItemsESP) {
                info.Category = EActorCategory::Evidence;
                info.Color = EvidenceESPColor;
            }
            newActors.push_back(info);
        }
    }

    // ==========================
    // Process Door Traps
    // ==========================
    if (Configuration::cfg_DrawTrapsESP || Configuration::cfg_DisableAllTraps) {
        TArray<AActor*> FoundTraps;
        UGameplayStatics::GetAllActorsOfClass(reinterpret_cast<UObject*>(World), ATrapActorAttachedToDoor::StaticClass(), &FoundTraps);

        for (AActor* A : FoundTraps) {
            if (IsLevelTransitioning.load()) break;
            
            auto* Trap = reinterpret_cast<ATrapActorAttachedToDoor*>(A);
            if (!Trap || !IsActorValid(reinterpret_cast<AActor*>(Trap)))
                continue;
            
            CachedActorInfo info = ProcessGenericActor(Trap, EActorCategory::DoorTraps, "Door Trap", PlayerController, LocalPlayer);
            if (info.Actor) {
                newActors.push_back(info);
            }
        }
        
        TArray<AActor*> FoundBaseTrapActors;
        UGameplayStatics::GetAllActorsOfClass(reinterpret_cast<UObject*>(World), ATrapActor::StaticClass(), &FoundBaseTrapActors);

        for (AActor* A : FoundBaseTrapActors) {
            if (IsLevelTransitioning.load()) break;
            
            auto* Trap = reinterpret_cast<ATrapActor*>(A);
            if (!Trap || !IsActorValid(reinterpret_cast<AActor*>(Trap)))
                continue;
                
            if (Trap->IsA(ATrapActorAttachedToDoor::StaticClass()))
                continue;
            
            CachedActorInfo info = ProcessGenericActor(Trap, EActorCategory::DoorTraps, "Trap", PlayerController, LocalPlayer);
            if (info.Actor) {
                newActors.push_back(info);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(CacheMutex);
        BackBufferActors.swap(newActors);
        CachedActors.swap(BackBufferActors);
    }

    bCacheUpdateInProgress.store(false);
}

// ==========================
// Per-type processors
// ==========================

CachedActorInfo ActorCache::ProcessCharacter(AReadyOrNotCharacter* Character, APlayerController* PlayerController, AActor* LocalPlayer) {
    CachedActorInfo info;

    if (!Character || !IsActorValid(reinterpret_cast<AActor*>(Character)))
        return info;

    if (!Character->CharacterHealth || !IsComponentValid(Character->CharacterHealth))
        return info;

    const bool isDead = Character->GetHealthStatus() == EPlayerHealthStatus::HS_Dead;

    info.Actor = Character;
    info.WorldPosition = Character->K2_GetActorLocation();

    ETeamType team = Character->GetTeam();
    AController* Controller = Character->GetController();
    
    const bool isIncapacitated = Character->GetHealthStatus() == EPlayerHealthStatus::HS_Incapacitated;
    bool isPlayerCharacter = Character->IsA(ABasePlayer_C::StaticClass()) ||
                            (Controller && Controller->IsA(AReadyOrNotPlayerController::StaticClass()));

    if (isIncapacitated) {
        info.Category = EActorCategory::Wounded;
        info.DisplayName = FString(L"Wounded");
        info.CharacterName = "Wounded";
    } else if (isPlayerCharacter) {
        info.Category = EActorCategory::Player;
        
        APlayerState* PlayerState = Character->PlayerState;
        if (PlayerState && IsActorValid(reinterpret_cast<AActor*>(PlayerState))) {
            FString playerName = PlayerState->GetPlayerName();
            if (!playerName.ToString().empty()) {
                info.DisplayName = playerName;
                info.CharacterName = playerName.ToString();
            } else {
                info.DisplayName = FString(L"Player");
                info.CharacterName = "Player";
            }
        } else {
            info.DisplayName = FString(L"Player");
            info.CharacterName = "Player";
        }
    } else if (Character->IsA(ASuspectCharacter::StaticClass())) {
        info.Category = EActorCategory::Suspect;
        info.DisplayName = FString(L"Suspect");
        info.CharacterName = "Suspect";
    } else if (Character->IsA(ACivilianCharacter::StaticClass())) {
        info.Category = EActorCategory::Civilian;
        info.DisplayName = FString(L"Civilian");
        info.CharacterName = "Civilian";
    } else {
        switch (team) {
            case ETeamType::TT_SERT_RED:
                info.Category = EActorCategory::SERT_Red;
                info.DisplayName = FString(L"SERT Red");
                info.CharacterName = "SERT Red";
                break;
            case ETeamType::TT_SERT_BLUE:
                info.Category = EActorCategory::SERT_Blue;
                info.DisplayName = FString(L"SERT Blue");
                info.CharacterName = "SERT Blue";
                break;
            case ETeamType::TT_SUSPECT:
                info.Category = EActorCategory::Suspect;
                info.DisplayName = FString(L"Suspect");
                info.CharacterName = "Suspect";
                break;
            case ETeamType::TT_CIVILIAN:
                info.Category = EActorCategory::Civilian;
                info.DisplayName = FString(L"Civilian");
                info.CharacterName = "Civilian";
                break;
            case ETeamType::TT_SQUAD:
                info.Category = EActorCategory::Squad;
                info.DisplayName = FString(L"Squad");
                info.CharacterName = "Squad";
                break;
            case ETeamType::TT_NONE:
            default:
                if (Character->IsA(ASWATCharacter::StaticClass())) {
                    info.Category = EActorCategory::Squad;
                    info.DisplayName = FString(L"Squad");
                    info.CharacterName = "Squad";
                } else {
                    info.Category = EActorCategory::Unknown;
                    info.DisplayName = FString(L"Unknown");
                    info.CharacterName = "Unknown";
                }
                break;
        }
    }

    if (isDead && !IsCorpseESPEnabled(info.Category))
        return CachedActorInfo{};
    info.IsDead = isDead;

    info.Health = Character->GetCurrentHealth();
    info.MaxHealth = Character->GetMaxHealth();

    return info;
}

CachedActorInfo ActorCache::ProcessCharacterByType(AReadyOrNotCharacter* Character, APlayerController* PlayerController, AActor* LocalPlayer) {
    CachedActorInfo info;

    if (!Character || !IsActorValid(reinterpret_cast<AActor*>(Character)))
        return info;

    if (!Character->CharacterHealth || !IsComponentValid(Character->CharacterHealth))
        return info;
    
    EPlayerHealthStatus healthStatus = Character->GetHealthStatus();
    if (healthStatus == EPlayerHealthStatus::HS_Dead)
        return info;

    info.Actor = Character;
    info.WorldPosition = Character->K2_GetActorLocation();
    
    if (healthStatus == EPlayerHealthStatus::HS_Incapacitated) {
        info.Category = EActorCategory::Wounded;
        info.DisplayName = FString(L"Wounded");
        info.CharacterName = "Wounded";
    }
    else if (Character->IsA(ABasePlayer_C::StaticClass()) || 
        (Character->GetController() && Character->GetController()->IsA(AReadyOrNotPlayerController::StaticClass()))) {

        info.Category = EActorCategory::Player;
        
        // Get player name from player state
        APlayerState* PlayerState = Character->PlayerState;
        if (PlayerState && IsActorValid(reinterpret_cast<AActor*>(PlayerState))) {
            FString playerName = PlayerState->GetPlayerName();
            if (!playerName.ToString().empty()) {
                info.DisplayName = playerName;
                info.CharacterName = playerName.ToString();
            } else {
                info.DisplayName = FString(L"Player");
                info.CharacterName = "Player";
            }
        } else {
            info.DisplayName = FString(L"Player");
            info.CharacterName = "Player";
        }
    } else if (Character->IsA(ASuspectCharacter::StaticClass())) {
        // This is a suspect
        info.Category = EActorCategory::Suspect;
        info.DisplayName = FString(L"Suspect");
        info.CharacterName = "Suspect";
    } else if (Character->IsA(ACivilianCharacter::StaticClass())) {
        // This is a civilian
        info.Category = EActorCategory::Civilian;
        info.DisplayName = FString(L"Civilian");
        info.CharacterName = "Civilian";
    } else {
        // For SWAT/SERT characters, use team information since they might not have specific classes
        ETeamType team = Character->GetTeam();
        switch (team) {
            case ETeamType::TT_SERT_RED:
                info.Category = EActorCategory::SERT_Red;
                info.DisplayName = FString(L"SERT Red");
                info.CharacterName = "SERT Red";
                break;
            case ETeamType::TT_SERT_BLUE:
                info.Category = EActorCategory::SERT_Blue;
                info.DisplayName = FString(L"SERT Blue");
                info.CharacterName = "SERT Blue";
                break;
            case ETeamType::TT_SQUAD:
                info.Category = EActorCategory::Squad;
                info.DisplayName = FString(L"Squad");
                info.CharacterName = "Squad";
                break;
            default:
                // Unknown character type - skip it
                return info; // Return empty info
        }
    }

    // Set health information
    info.Health = Character->GetCurrentHealth();
    info.MaxHealth = Character->GetMaxHealth();

    return info;
}

CachedActorInfo ActorCache::ProcessEvidence(AEvidenceActor* Evidence, APlayerController* PlayerController, AActor* LocalPlayer) {
    CachedActorInfo info;

    if (!Evidence || !IsActorValid(reinterpret_cast<AActor*>(Evidence)))
        return info;

    // Check if evidence component exists and is valid
    if (!Evidence->EvidenceComponent || !IsComponentValid(Evidence->EvidenceComponent))
        return info;

    info.Actor = Evidence;
    info.WorldPosition = Evidence->K2_GetActorLocation();
    
    // Get evidence state to determine if it's collected or uncollected
    SDK::EEvidenceActorState evidenceState = Evidence->EvidenceComponent->GetEvidenceState();
    
    switch (evidenceState) {
        case SDK::EEvidenceActorState::Unclaimed:
            info.Category = EActorCategory::Evidence;
            info.DisplayName = FString(L"Evidence");
            info.CharacterName = "Evidence";
            info.Color = EvidenceESPColor;
            break;
            
        case SDK::EEvidenceActorState::Collected:
            info.Category = EActorCategory::CollectedEvidence;
            info.DisplayName = FString(L"Collected Evidence");
            info.CharacterName = "Collected Evidence";
            info.Color = CollectedEvidenceESPColor;
            break;
            
        case SDK::EEvidenceActorState::Dropped:
            info.Category = EActorCategory::Evidence;
            info.DisplayName = FString(L"Dropped Evidence");
            info.CharacterName = "Dropped Evidence";
            info.Color = EvidenceESPColor;
            break;
            
        case SDK::EEvidenceActorState::Extraction:
            info.Category = EActorCategory::CollectedEvidence;
            info.DisplayName = FString(L"Evidence (Extracting)");
            info.CharacterName = "Evidence (Extracting)";
            info.Color = CollectedEvidenceESPColor;
            break;
            
        default:
            info.Category = EActorCategory::Evidence;
            info.DisplayName = FString(L"Evidence");
            info.CharacterName = "Evidence";
            info.Color = EvidenceESPColor;
            break;
    }

    // Get evidence name if available
    FText evidenceName = Evidence->GetEvidenceName();
    FString evidenceNameString = evidenceName.GetStringRef();
    std::string evidenceNameStr = evidenceNameString.ToString();
    if (!evidenceNameStr.empty()) {
        info.DisplayName = evidenceNameString;
        info.CharacterName = evidenceNameString.ToString();
    }

    return info;
}

CachedActorInfo ActorCache::ProcessCollectedEvidence(ACollectedEvidenceActor* CollectedEvidence, APlayerController* PlayerController, AActor* LocalPlayer) {
    CachedActorInfo info;

    if (!CollectedEvidence || !IsActorValid(reinterpret_cast<AActor*>(CollectedEvidence)))
        return info;

    // Check if evidence component exists and is valid
    if (!CollectedEvidence->EvidenceComponent || !IsComponentValid(CollectedEvidence->EvidenceComponent))
        return info;

    info.Actor = CollectedEvidence;
    info.WorldPosition = CollectedEvidence->K2_GetActorLocation();
    
    // Collected evidence actors are always in collected state
    info.Category = EActorCategory::CollectedEvidence;
    info.DisplayName = FString(L"Evidence Bag");
    info.CharacterName = "Evidence Bag";
    info.Color = CollectedEvidenceESPColor;

    return info;
}

CachedActorInfo ActorCache::ProcessItem(ABaseItem* Item, APlayerController* PlayerController, AActor* LocalPlayer) {
    CachedActorInfo info;

    if (!Item || !IsActorValid(reinterpret_cast<AActor*>(Item)))
        return info;

    info.Actor = Item;
    info.WorldPosition = Item->K2_GetActorLocation();
    info.Category = EActorCategory::Items;
    
    // Get item name if available
    std::string itemClassName = Item->Class ? Item->Class->GetName() : "Item";
    std::wstring wItemName(itemClassName.begin(), itemClassName.end());
    info.DisplayName = FString(wItemName.c_str());
    info.CharacterName = itemClassName;
    info.Color = ItemsESPColor;

    return info;
}

CachedActorInfo ActorCache::ProcessDoor(Adoor* Door, APlayerController* PlayerController, AActor* LocalPlayer) {
    CachedActorInfo info;

    if (!Door || !IsActorValid(reinterpret_cast<AActor*>(Door)))
        return info;

    info.Actor = Door;
    info.WorldPosition = Door->K2_GetActorLocation();
    info.Category = EActorCategory::Doors;
    info.DisplayName = FString(L"Door");
    info.CharacterName = "Door";
    info.Color = DoorsESPColor;

    return info;
}

CachedActorInfo ActorCache::ProcessGenericActor(AActor* Actor, EActorCategory Category, const std::string& DisplayName, APlayerController* PlayerController, AActor* LocalPlayer) {
    CachedActorInfo info;

    if (!Actor || !IsActorValid(Actor))
        return info;

    info.Actor = Actor;
    info.WorldPosition = Actor->K2_GetActorLocation();
    info.Category = Category;
    std::wstring wDisplayName(DisplayName.begin(), DisplayName.end());
    info.DisplayName = FString(wDisplayName.c_str());
    info.CharacterName = DisplayName;
    
    // Set color based on category
    switch (Category) {
        case EActorCategory::DoorTraps:
            info.Color = TrapsESPColor;
            break;
        case EActorCategory::Securables:
            info.Color = SecurablesESPColor;
            break;
        case EActorCategory::ReportableActors:
            info.Color = IM_COL32(255, 255, 0, 255); // Yellow for reportables
            break;
        default:
            info.Color = IM_COL32(255, 255, 255, 255); // White default
            break;
    }

    return info;
}

