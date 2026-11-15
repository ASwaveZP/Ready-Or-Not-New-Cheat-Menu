#include "ESP.h"
#include "Configuration.h"
#include "ActorCache.h"
#include "Utils.h"
#include "SDKExt.h"
#include "ImGuiExt.h"
#include "ReadyornotOverlay.h"
#include "PlayerFeatures.h"
#include <OverlayBase.h>
#include <imgui_internal.h>
#include <Fonts.h>
#include <Windows.h>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <chrono>
#include <cstdio>
#include <cctype>
#include <limits>

using namespace SDK;

extern ImU32 PlayerESPColorVisible;
extern ImU32 PlayerESPColorNonVisible;
extern ImU32 PlayerESPColorAimed;
extern ImU32 SERTRedESPColor;
extern ImU32 SERTBlueESPColor;
extern ImU32 SuspectESPColor;
extern ImU32 CivilianESPColor;
extern ImU32 SquadESPColor;

namespace {
    struct SkeletonBoneLink { SDK::FName Parent; SDK::FName Child; };

    bool IsBodySkeletonBone(const std::string& boneName) {
        std::string name = boneName;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        // Ignore auxiliary rig bones that create short branches or doubled lines
        // around the main joints (twist, roll, IK, and attachment bones).
        static const char* auxiliaryFragments[] = {
            "twist", "roll", "ik_", "_ik", "fk_", "_fk", "socket", "attach",
            "weapon", "virtual", "helper", "corrective", "offset"
        };
        if (std::any_of(std::begin(auxiliaryFragments), std::end(auxiliaryFragments), [&](const char* fragment) {
            return name.find(fragment) != std::string::npos;
        })) return false;

        static const char* fragments[] = {
            "root", "pelvis", "hip", "spine", "neck", "head", "clavicle", "shoulder",
            "upperarm", "lowerarm", "forearm", "elbow", "hand", "thigh", "upleg",
            "calf", "lowerleg", "leg", "knee", "ankle", "foot", "toe"
        };
        return std::any_of(std::begin(fragments), std::end(fragments), [&](const char* fragment) {
            return name.find(fragment) != std::string::npos;
        });
    }

    const std::vector<SkeletonBoneLink>& GetBodySkeleton(USkeletalMeshComponent* mesh) {
        static std::unordered_map<USkeletalMesh*, std::vector<SkeletonBoneLink>> cache;
        static const std::vector<SkeletonBoneLink> empty;
        if (!mesh || !mesh->SkeletalMesh) return empty;
        auto* asset = mesh->SkeletalMesh;
        auto found = cache.find(asset);
        if (found != cache.end()) return found->second;

        std::vector<SkeletonBoneLink> links;
        const int32 boneCount = mesh->GetNumBones();
        if (boneCount > 0 && boneCount < 1024) {
            links.reserve(static_cast<size_t>(boneCount));
            for (int32 i = 0; i < boneCount; ++i) {
                const SDK::FName child = mesh->GetBoneName(i);
                const SDK::FName parent = mesh->GetParentBone(child);
                const std::string childText = child.ToString();
                const std::string parentText = parent.ToString();
                if (childText.empty() || parentText.empty() || parentText == "None") continue;
                if (!IsBodySkeletonBone(childText) || !IsBodySkeletonBone(parentText)) continue;
                links.push_back({ parent, child });
            }
        }
        if (cache.size() >= 256) cache.clear();
        return cache.emplace(asset, std::move(links)).first->second;
    }

    void DrawAnimatedSkeleton(ImDrawList* drawList, USkeletalMeshComponent* mesh, APlayerController* controller, ImU32 color) {
        if (!drawList || !mesh || !controller) return;
        const float thickness = std::clamp(Configuration::cfg_ESPThickness, 1.0f, 2.8f);
        for (const auto& link : GetBodySkeleton(mesh)) {
            const FVector parentWorld = mesh->GetSocketLocation(link.Parent);
            const FVector childWorld = mesh->GetSocketLocation(link.Child);
            if (parentWorld.IsZero() || childWorld.IsZero()) continue;
            FVector2D parentScreen{}, childScreen{};
            if (!controller->ProjectWorldLocationToScreen(parentWorld, &parentScreen, true)
                || !controller->ProjectWorldLocationToScreen(childWorld, &childScreen, true)) continue;

            const ImVec2 parent(static_cast<float>(parentScreen.X), static_cast<float>(parentScreen.Y));
            const ImVec2 child(static_cast<float>(childScreen.X), static_cast<float>(childScreen.Y));
            drawList->AddLine(parent, child, color, thickness);
        }
    }
}

namespace ReadyornotESP {

    void DrawGuiFOVCircle() {
        if (!Configuration::cfg_DrawFOVCircle)
            return;

        if (IsLevelTransitioning.load()) {
            return;
        }

        ReadyornotOverlay* Overlay = static_cast<ReadyornotOverlay*>(OverlayBase::Instance);
        if (!Overlay)
            return;

        APlayerController* PlayerController = GetARONPC();
        if (!PlayerController || !PlayerController->K2_GetPawn())
            return;

        FVector2D FOVCenter = { ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f };
        if (FOVCenter.X <= 0 || FOVCenter.Y <= 0)
            return;

        auto drawList = ImGui::GetBackgroundDrawList();
        if (!drawList)
            return;

        const float alpha = std::clamp(Configuration::cfg_CircleAlpha, 0.0f, 1.0f);
        const float radius = Configuration::cfg_FOVRadius;
        const int segments = 240;
        const float timeHue = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.16f, 1.0f);
        auto rainbowColor = [&](float t, float a) {
            const float hue = std::fmod(timeHue + t, 1.0f);
            float red = 0.0f, green = 0.0f, blue = 0.0f;
            ImGui::ColorConvertHSVtoRGB(hue, 0.9f, 1.0f, red, green, blue);
            return ImGui::ColorConvertFloat4ToU32(ImVec4(red, green, blue, a));
        };
        const ImVec2 center(FOVCenter.X, FOVCenter.Y);
        // A soft colored halo under the crisp animated rainbow stroke.
        for (int i = 0; i < segments; ++i) {
            const float t0 = static_cast<float>(i) / segments;
            const float t1 = static_cast<float>(i + 1) / segments;
            const float a0 = t0 * 6.28318530718f;
            const float a1 = t1 * 6.28318530718f;
            const ImVec2 p0(center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius);
            const ImVec2 p1(center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius);
            drawList->AddLine(p0, p1, rainbowColor((t0 + t1) * 0.5f, alpha * 0.22f), 5.0f);
            drawList->AddLine(p0, p1, rainbowColor((t0 + t1) * 0.5f, alpha), 2.0f);
        }
        const ImU32 CircleColor = rainbowColor(0.0f, alpha);

        const float FOVRadius = Configuration::cfg_FOVRadius;

        if (Overlay->BestTargetActor) {
            FVector2D TargetScreenPosition;
            if (PlayerController->ProjectWorldLocationToScreen(Overlay->BestTargetLocation, &TargetScreenPosition, true)) {
                float DistanceToCenter = CustomMath::Sqrt(
                    CustomMath::Square(TargetScreenPosition.X - FOVCenter.X) +
                    CustomMath::Square(TargetScreenPosition.Y - FOVCenter.Y)
                );

                if (DistanceToCenter <= FOVRadius) {
                    drawList->AddLine(
                        ImVec2(FOVCenter.X, FOVCenter.Y),
                        ImVec2(TargetScreenPosition.X, TargetScreenPosition.Y),
                        CircleColor,
                        Configuration::cfg_ESPThickness
                    );
                }
            }
        }
    }

    void DrawGuiESP() {
        if (!Configuration::cfg_DrawPlayerESP && 
            !Configuration::cfg_DrawSERTRedESP && 
            !Configuration::cfg_DrawSERTBlueESP && 
            !Configuration::cfg_DrawSuspectESP && 
            !Configuration::cfg_DrawCivilianESP && 
            !Configuration::cfg_DrawSquadESP &&
            !Configuration::cfg_DrawWoundedESP &&
            !Configuration::cfg_DrawEvidenceESP &&
            !Configuration::cfg_DrawCollectedEvidenceESP &&
            !Configuration::cfg_DrawItemsESP &&
            !Configuration::cfg_DrawDoorsESP &&
            !Configuration::cfg_DrawTrapsESP &&
            !Configuration::cfg_ShowObjectives &&
            !Configuration::cfg_DrawPlayerCorpses && !Configuration::cfg_DrawAICorpses &&
            !Configuration::cfg_DrawSuspectCorpses && !Configuration::cfg_DrawCivilianCorpses)
        {
            return;
        }

        if (IsLevelTransitioning.load()) {
            return;
        }

        auto PlayerController = GetARONPCForESP();
        if (!PlayerController || !IsActorValid(PlayerController)) return;

        auto PawnPlayer = PlayerController->K2_GetPawn();
        if (!PawnPlayer || !IsActorValid(PawnPlayer)) return;

        const auto drawList = ImGui::GetBackgroundDrawList();
        if (!drawList) return;

        // Mission reportables are kept by the game state, including objectives that
        // are hidden from normal world-object scans. Keep these in the game overlay.
        if (Configuration::cfg_ShowObjectives) {
            auto* gameState = UReadyOrNotStatics::GetReadyOrNotGameState();
            if (gameState && IsActorValid(gameState)) {
                const FVector localLocation = PawnPlayer->K2_GetActorLocation();
                for (AReportableActor* objective : gameState->AllReportableActors) {
                    if (!objective || !IsActorValid(objective)) continue;
                    const FVector location = objective->K2_GetActorLocation();
                    const float distance = GetDistance3D(location, localLocation) / 100.0f;
                    if (distance > Configuration::cfg_MaxObjectESPDistance / 100.0f) continue;
                    FVector2D screen{};
                    if (!PlayerController->ProjectWorldLocationToScreen(location, &screen, true)) continue;
                    const bool visible = PlayerController->LineOfSightTo(objective, localLocation, false);
                    const ImU32 color = !objective->bReportableEnabled ? IM_COL32(255, 176, 64, 255)
                        : visible ? IM_COL32(53, 216, 196, 255) : IM_COL32(255, 205, 76, 255);
                    drawList->AddCircleFilled(ImVec2(screen.X, screen.Y), 4.0f, color, 16);
                    const std::string label = objective->ReportableName.ToString().empty()
                        ? std::string("Mission Objective") : objective->ReportableName.ToString();
                    char distanceLabel[24]{};
                    std::snprintf(distanceLabel, sizeof(distanceLabel), " [%.0fm]", distance);
                    drawList->AddText(ImVec2(screen.X + 7.0f, screen.Y - 8.0f), color, (label + distanceLabel).c_str());
                }
            }
        }

        ImVec2 canvasSize = ImGui::GetIO().DisplaySize;

        const auto& allActors = ActorCache::GetInstance().GetCachedActors();

        // ================================
        //        DRAW CHARACTER ESP
        // ================================
        ReadyornotOverlay* Overlay = static_cast<ReadyornotOverlay*>(OverlayBase::Instance);
    
        for (const auto& actorInfo : allActors)
        {
            if (!actorInfo.Actor || !IsActorValid(actorInfo.Actor))
                continue;

            bool espEnabled = false;
            switch (actorInfo.Category) {
                case EActorCategory::Player:
                    espEnabled = Configuration::cfg_DrawPlayerESP;
                    break;
                case EActorCategory::SERT_Red:
                    espEnabled = Configuration::cfg_DrawSERTRedESP;
                    break;
                case EActorCategory::SERT_Blue:
                    espEnabled = Configuration::cfg_DrawSERTBlueESP;
                    break;
                case EActorCategory::Suspect:
                    espEnabled = Configuration::cfg_DrawSuspectESP;
                    break;
                case EActorCategory::Civilian:
                    espEnabled = Configuration::cfg_DrawCivilianESP;
                    break;
                case EActorCategory::Squad:
                    espEnabled = Configuration::cfg_DrawSquadESP;
                    break;
                case EActorCategory::Wounded:
                    espEnabled = Configuration::cfg_DrawWoundedESP;
                    break;
                case EActorCategory::Evidence:
                    espEnabled = Configuration::cfg_DrawEvidenceESP;
                    break;
                case EActorCategory::CollectedEvidence:
                    espEnabled = Configuration::cfg_DrawCollectedEvidenceESP;
                    break;
                case EActorCategory::Items:
                    espEnabled = Configuration::cfg_DrawItemsESP;
                    break;
                case EActorCategory::Doors:
                case EActorCategory::Securables:
                    continue;
                case EActorCategory::DoorTraps:
                    espEnabled = Configuration::cfg_DrawTrapsESP;
                    break;
                case EActorCategory::Unknown:
                    espEnabled = Configuration::cfg_DrawSuspectESP;
                    break;
                default:
                    continue; // Skip unknown
            }

            if (!espEnabled && !(actorInfo.IsDead && IsCorpseESPEnabled(actorInfo.Category)))
                continue;

            // Handle different actor types
            bool isCharacter = (actorInfo.Category == EActorCategory::Player ||
                               actorInfo.Category == EActorCategory::SERT_Red ||
                               actorInfo.Category == EActorCategory::SERT_Blue ||
                               actorInfo.Category == EActorCategory::Suspect ||
                               actorInfo.Category == EActorCategory::Civilian ||
                               actorInfo.Category == EActorCategory::Squad ||
                               actorInfo.Category == EActorCategory::Wounded ||
                               actorInfo.Category == EActorCategory::Unknown);

            if (isCharacter) {
                auto* Character = static_cast<SDK::AReadyOrNotCharacter*>(actorInfo.Actor);
                if (!Character || !IsActorValid(Character))
                    continue;

            if (!Character->CharacterHealth || !IsComponentValid(Character->CharacterHealth) || Character == PawnPlayer)
                continue;

            const bool isDead = actorInfo.IsDead || Character->GetHealthStatus() == EPlayerHealthStatus::HS_Dead;
            if (isDead && !IsCorpseESPEnabled(actorInfo.Category)) continue;

            if (actorInfo.Category == EActorCategory::Player && Character == PawnPlayer)
                continue;

            FVector CharacterLocation = Character->K2_GetActorLocation();
            float distance = GetDistance3D(CharacterLocation, PawnPlayer->K2_GetActorLocation()) / 100.0f;
            float maxDistance = (actorInfo.Category == EActorCategory::Player) 
                ? Configuration::cfg_MaxPlayerESPDistance / 100.0f
                : Configuration::cfg_MaxAIESPDistance / 100.0f;
        
            if (distance > maxDistance)
                continue;

            USkeletalMeshComponent* MeshComponent = Character->Mesh;
            if (!MeshComponent || !IsComponentValid(MeshComponent)) continue;

            FVector boundsOrigin{};
            FVector boundsExtent{};
            float sphereRadius = 0.0f;
            UKismetSystemLibrary::GetComponentBounds(MeshComponent, &boundsOrigin, &boundsExtent, &sphereRadius);
            if (boundsExtent.IsZero() || boundsExtent.Z < 1.0) continue;

            float minScreenX = (std::numeric_limits<float>::max)();
            float minScreenY = (std::numeric_limits<float>::max)();
            float maxScreenX = std::numeric_limits<float>::lowest();
            float maxScreenY = std::numeric_limits<float>::lowest();
            bool hasProjectedBounds = false;
            for (int corner = 0; corner < 8; ++corner) {
                const FVector worldCorner(
                    boundsOrigin.X + ((corner & 1) ? boundsExtent.X : -boundsExtent.X),
                    boundsOrigin.Y + ((corner & 2) ? boundsExtent.Y : -boundsExtent.Y),
                    boundsOrigin.Z + ((corner & 4) ? boundsExtent.Z : -boundsExtent.Z));
                FVector2D screenCorner{};
                if (!PlayerController->ProjectWorldLocationToScreen(worldCorner, &screenCorner, true)) continue;
                hasProjectedBounds = true;
                minScreenX = (std::min)(minScreenX, static_cast<float>(screenCorner.X));
                minScreenY = (std::min)(minScreenY, static_cast<float>(screenCorner.Y));
                maxScreenX = (std::max)(maxScreenX, static_cast<float>(screenCorner.X));
                maxScreenY = (std::max)(maxScreenY, static_cast<float>(screenCorner.Y));
            }
            if (!hasProjectedBounds || maxScreenX < 0.0f || maxScreenY < 0.0f
                || minScreenX > canvasSize.x || minScreenY > canvasSize.y) continue;

            minScreenX = std::clamp(minScreenX, 0.0f, static_cast<float>(canvasSize.x));
            minScreenY = std::clamp(minScreenY, 0.0f, static_cast<float>(canvasSize.y));
            maxScreenX = std::clamp(maxScreenX, 0.0f, static_cast<float>(canvasSize.x));
            maxScreenY = std::clamp(maxScreenY, 0.0f, static_cast<float>(canvasSize.y));
            const float boxWidth = maxScreenX - minScreenX;
            const float boxHeight = maxScreenY - minScreenY;
            if (boxWidth < 2.0f || boxHeight < 2.0f) continue;

            FVector2D screenPosTop{ (minScreenX + maxScreenX) * 0.5f, minScreenY };
            FVector2D screenPosBottom{ (minScreenX + maxScreenX) * 0.5f, maxScreenY };
            ImVec2 boxTopLeft(minScreenX, minScreenY);
            ImVec2 boxBottomRight(maxScreenX, maxScreenY);

            ImU32 boxColor;
            bool bIsVisible = Configuration::cfg_AimVisCheck
                ? PlayerController->LineOfSightTo(Character, PawnPlayer->K2_GetActorLocation(), false)
                : true;

            switch (actorInfo.Category) {
                case EActorCategory::Player:
                    boxColor = bIsVisible ? PlayerESPColorVisible : PlayerESPColorNonVisible;
                    break;
                case EActorCategory::SERT_Red:
                    boxColor = SERTRedESPColor;
                    break;
                case EActorCategory::SERT_Blue:
                    boxColor = SERTBlueESPColor;
                    break;
                case EActorCategory::Suspect:
                    boxColor = SuspectESPColor;
                    break;
                case EActorCategory::Civilian:
                    boxColor = CivilianESPColor;
                    break;
                case EActorCategory::Squad:
                    boxColor = SquadESPColor;
                    break;
                case EActorCategory::Wounded:
                    boxColor = WoundedESPColor;
                    break;
                case EActorCategory::Unknown:
                    boxColor = SuspectESPColor;
                    break;
                default:
                    boxColor = PlayerESPColorVisible;
                    break;
            }

            if (Configuration::cfg_AimVisCheck && !bIsVisible && actorInfo.Category != EActorCategory::Player)
                boxColor = IM_COL32(255, 171, 82, 190);

            if (!isDead && Overlay->BestTargetActor == actorInfo.Actor)
            {
                boxColor = PlayerESPColorAimed;
            }

            bool shouldDrawBoxes = false;
            
            switch (actorInfo.Category) {
                case EActorCategory::Player:
                    shouldDrawBoxes = Configuration::cfg_DrawPlayerBoxes;
                    break;
                case EActorCategory::SERT_Red:
                    shouldDrawBoxes = Configuration::cfg_DrawSERTRedBoxes;
                    break;
                case EActorCategory::SERT_Blue:
                    shouldDrawBoxes = Configuration::cfg_DrawSERTBlueBoxes;
                    break;
                case EActorCategory::Suspect:
                    shouldDrawBoxes = Configuration::cfg_DrawSuspectBoxes;
                    break;
                case EActorCategory::Civilian:
                    shouldDrawBoxes = Configuration::cfg_DrawCivilianBoxes;
                    break;
                case EActorCategory::Squad:
                    shouldDrawBoxes = Configuration::cfg_DrawSquadBoxes;
                    break;
                case EActorCategory::Wounded:
                    shouldDrawBoxes = Configuration::cfg_DrawWoundedBoxes;
                    break;
                default:
                    break;
            }

            // Draw box
            if (shouldDrawBoxes) {
                float boxThickness = Configuration::cfg_ESPThickness;

                switch (Configuration::cfg_AimbotBox) {
                case 0: // 2D Box
                    drawList->AddRect(
                        boxTopLeft,
                        boxBottomRight,
                        boxColor,
                        0.0f,
                        0,
                        boxThickness
                    );
                    break;

                case 1: // 2D Cornered Box
                    default: {
                        float cornerSize = boxWidth / 5.0f;

                        // Top Left
                        drawList->AddLine(
                            boxTopLeft,
                            ImVec2(boxTopLeft.x + cornerSize, boxTopLeft.y),
                            boxColor, boxThickness
                        );
                        drawList->AddLine(
                            boxTopLeft,
                            ImVec2(boxTopLeft.x, boxTopLeft.y + cornerSize),
                            boxColor, boxThickness
                        );

                        // Top Right
                        ImVec2 topRight = ImVec2(boxBottomRight.x, boxTopLeft.y);
                        drawList->AddLine(
                            topRight,
                            ImVec2(topRight.x - cornerSize, topRight.y),
                            boxColor, boxThickness
                        );
                        drawList->AddLine(
                            topRight,
                            ImVec2(topRight.x, topRight.y + cornerSize),
                            boxColor, boxThickness
                        );

                        // Bottom Left
                        ImVec2 bottomLeft = ImVec2(boxTopLeft.x, boxBottomRight.y);
                        drawList->AddLine(
                            bottomLeft,
                            ImVec2(bottomLeft.x + cornerSize, bottomLeft.y),
                            boxColor, boxThickness
                        );
                        drawList->AddLine(
                            bottomLeft,
                            ImVec2(bottomLeft.x, bottomLeft.y - cornerSize),
                            boxColor, boxThickness
                        );

                        // Bottom Right
                        ImVec2 bottomRight = boxBottomRight;
                        drawList->AddLine(
                            bottomRight,
                            ImVec2(bottomRight.x - cornerSize, bottomRight.y),
                            boxColor, boxThickness
                        );
                        drawList->AddLine(
                            bottomRight,
                            ImVec2(bottomRight.x, bottomRight.y - cornerSize),
                            boxColor, boxThickness
                        );
                        break;
                    }
                }
            }

            bool shouldDrawHealth = false;
            switch (actorInfo.Category) {
                case EActorCategory::Player:
                    shouldDrawHealth = Configuration::cfg_DrawPlayerHealth;
                    break;
                case EActorCategory::SERT_Red:
                    shouldDrawHealth = Configuration::cfg_DrawSERTRedHealth;
                    break;
                case EActorCategory::SERT_Blue:
                    shouldDrawHealth = Configuration::cfg_DrawSERTBlueHealth;
                    break;
                case EActorCategory::Suspect:
                    shouldDrawHealth = Configuration::cfg_DrawSuspectHealth;
                    break;
                case EActorCategory::Civilian:
                    shouldDrawHealth = Configuration::cfg_DrawCivilianHealth;
                    break;
                case EActorCategory::Squad:
                    shouldDrawHealth = Configuration::cfg_DrawSquadHealth;
                    break;
                case EActorCategory::Wounded:
                    shouldDrawHealth = Configuration::cfg_DrawWoundedHealth;
                    break;
                default:
                    break;
            }

            // Health bar
            if (shouldDrawHealth) {
                if (Character->CharacterHealth && IsComponentValid(Character->CharacterHealth))
                {
                    float currentHealth = Character->GetCurrentHealth();
                    float maxHealth = Character->GetMaxHealth();

                    if (maxHealth > 0.0f && currentHealth >= 0.0f)
                    {
                        float healthBarHeight = boxHeight * (currentHealth / maxHealth);
                        float barWidth = 3.0f;

                        ImVec2 barTopLeft = { boxTopLeft.x - barWidth - 2, boxTopLeft.y };
                        ImVec2 barBottomRight = { barTopLeft.x + barWidth, boxBottomRight.y };
                        ImVec2 barFillTop = { barTopLeft.x, boxBottomRight.y - healthBarHeight };
                        ImVec2 barFillBottom = { barBottomRight.x, boxBottomRight.y };

                        // Background
                        drawList->AddRectFilled(barTopLeft, barBottomRight, IM_COL32(0, 0, 0, 255));
                        // Fill
                        drawList->AddRectFilled(
                            barFillTop,
                            barFillBottom,
                            (currentHealth > maxHealth * 0.5f)
                            ? IM_COL32(0x00, 0xFF, 0x00, 0xFF)  // green
                            : IM_COL32(0xFF, 0xA5, 0x00, 0xFF)  // orange
                        );
                    }
                }
            }

            bool shouldDrawNames = false;
            switch (actorInfo.Category) {
                case EActorCategory::Player:
                    shouldDrawNames = Configuration::cfg_DrawPlayerNames;
                    break;
                case EActorCategory::SERT_Red:
                    shouldDrawNames = Configuration::cfg_DrawSERTRedNames;
                    break;
                case EActorCategory::SERT_Blue:
                    shouldDrawNames = Configuration::cfg_DrawSERTBlueNames;
                    break;
                case EActorCategory::Suspect:
                    shouldDrawNames = Configuration::cfg_DrawSuspectNames;
                    break;
                case EActorCategory::Civilian:
                    shouldDrawNames = Configuration::cfg_DrawCivilianNames;
                    break;
                case EActorCategory::Squad:
                    shouldDrawNames = Configuration::cfg_DrawSquadNames;
                    break;
                case EActorCategory::Wounded:
                    shouldDrawNames = Configuration::cfg_DrawWoundedNames;
                    break;
                default:
                    break;
            }

            // Name + distance
            if (shouldDrawNames || isDead) {
                ImVec2 centerBottom = {
                    (boxTopLeft.x + boxBottomRight.x) / 2.0f,
                    boxBottomRight.y + 5.0f
                };

                std::string characterNameText = actorInfo.CharacterName;

                // Add distance
                char distanceBuffer[32];
                sprintf_s(distanceBuffer, " [%.0fm]", distance);
                std::string displayText = isDead
                    ? std::string("死亡  ") + characterNameText + distanceBuffer
                    : characterNameText + std::string(distanceBuffer);

                if (drawList && OverlayBase::Instance->ESPFont) {
                    ImVec2 nameTextSize = ImGui::CalcTextSize(displayText.c_str());
                    ImVec2 namePos = {
                        centerBottom.x - nameTextSize.x / 2.0f,
                        centerBottom.y
                    };
                    namePos.y = std::clamp(namePos.y, 4.0f, static_cast<float>(canvasSize.y) - 15.0f);

                    drawTextOutlined(drawList, namePos, isDead ? IM_COL32(255, 48, 58, 255) : boxColor, displayText.c_str(), OverlayBase::Instance->ESPFont);
                }
            }

            bool shouldDrawLines = false;
            switch (actorInfo.Category) {
                case EActorCategory::Player:
                    shouldDrawLines = Configuration::cfg_DrawPlayerLines;
                    break;
                case EActorCategory::SERT_Red:
                    shouldDrawLines = Configuration::cfg_DrawSERTRedLines;
                    break;
                case EActorCategory::SERT_Blue:
                    shouldDrawLines = Configuration::cfg_DrawSERTBlueLines;
                    break;
                case EActorCategory::Suspect:
                    shouldDrawLines = Configuration::cfg_DrawSuspectLines;
                    break;
                case EActorCategory::Civilian:
                    shouldDrawLines = Configuration::cfg_DrawCivilianLines;
                    break;
                case EActorCategory::Squad:
                    shouldDrawLines = Configuration::cfg_DrawSquadLines;
                    break;
                case EActorCategory::Wounded:
                    shouldDrawLines = Configuration::cfg_DrawWoundedLines;
                    break;
                default:
                    break;
            }

            // Player lines
            if (shouldDrawLines && drawList) {
                const ImVec2 rayOrigin(static_cast<float>(canvasSize.x) * 0.5f, 0.0f);
                const ImVec2 rayTarget(static_cast<float>(screenPosTop.X), static_cast<float>(screenPosTop.Y));
                drawList->AddLine(rayOrigin, rayTarget, boxColor, Configuration::cfg_ESPThickness);
            }

            bool shouldDrawBones = false;
            switch (actorInfo.Category) {
                case EActorCategory::Player:
                    shouldDrawBones = Configuration::cfg_DrawPlayerBones;
                    break;
                case EActorCategory::SERT_Red:
                    shouldDrawBones = Configuration::cfg_DrawSERTRedBones;
                    break;
                case EActorCategory::SERT_Blue:
                    shouldDrawBones = Configuration::cfg_DrawSERTBlueBones;
                    break;
                case EActorCategory::Suspect:
                    shouldDrawBones = Configuration::cfg_DrawSuspectBones;
                    break;
                case EActorCategory::Civilian:
                    shouldDrawBones = Configuration::cfg_DrawCivilianBones;
                    break;
                case EActorCategory::Squad:
                    shouldDrawBones = Configuration::cfg_DrawSquadBones;
                    break;
                case EActorCategory::Wounded:
                    shouldDrawBones = Configuration::cfg_DrawWoundedBones;
                    break;
                default:
                    break;
            }

            // The global option draws skeletons for every enabled character ESP category.
            shouldDrawBones = shouldDrawBones || Configuration::cfg_DrawESPBones;

            // Skeleton
            if (shouldDrawBones) {
                DrawAnimatedSkeleton(drawList, MeshComponent, PlayerController, isDead ? IM_COL32(0, 0, 0, 255) : boxColor);
            }
            } else {
                // Handle object rendering
                DrawObjectESP(actorInfo, PlayerController, PawnPlayer, drawList, canvasSize);
            }
        }

        // --- DEBUG CLASS NAMES ---
        if (Configuration::cfg_ShowDebugClassNames) {

            if (IsLevelTransitioning.load()) {
                return;
            }
            
            UWorld* GWorld = UWorld::GetWorld();
            if (GWorld && GWorld->Levels.IsValid()) {
                FVector PlayerLocation = PawnPlayer->K2_GetActorLocation();
                
                for (int i = 0; i < GWorld->Levels.Num(); ++i) {
                    if (!GWorld->Levels.IsValidIndex(i))
                        continue;

                    const auto Level = GWorld->Levels[i];
                    if (!Level)
                        continue;

                    for (int j = 0; j < Level->Actors.Num(); ++j) {
                        const auto Actor = Level->Actors[j];
                        if (!Actor || !IsActorValid(Actor))
                            continue;
                            
                        if (Actor == PawnPlayer)
                            continue;
                            
                        float Distance = PawnPlayer->GetDistanceTo(Actor) / 100.0f; // Converted to meters
                        
                        if (Distance > Configuration::cfg_DebugMaxDistance / 100.0f)
                            continue;

                        FVector ActorLocation = Actor->K2_GetActorLocation();
                        FVector2D ScreenPos;
                        if (!PlayerController->ProjectWorldLocationToScreen(ActorLocation, &ScreenPos, true))
                            continue;

                        std::string className = Actor->Class ? Actor->Class->GetName() : "Unknown";
                        std::string displayText = className;

                        if (Configuration::cfg_ShowDebugDistance) {
                            char distanceBuffer[32];
                            sprintf_s(distanceBuffer, "[%.0fm]", Distance);
                            displayText += " " + std::string(distanceBuffer);
                        }
                        
                        ImU32 debugColor = IM_COL32(255, 255, 0, 180); // Yellowish
                        
                        // Draw classname
                        if (drawList && OverlayBase::Instance->ESPFont) {
                            ImVec2 NameSize = ImGui::CalcTextSize(displayText.c_str(), nullptr, true);
                            ImVec2 NamePos = {
                                static_cast<float>(ScreenPos.X) - NameSize.x / 2.0f,
                                static_cast<float>(ScreenPos.Y) + 10.0f
                            };

                            if (NamePos.x >= 0 && NamePos.y >= 0 && 
                                NamePos.x < static_cast<float>(canvasSize.x) && NamePos.y < static_cast<float>(canvasSize.y)) {
                                drawTextOutlined(drawList, NamePos, debugColor, displayText.c_str(), OverlayBase::Instance->ESPFont);
                            }
                        }
                    }
                }
            }
        }
    }

    void DrawObjectESP(const CachedActorInfo& actorInfo, SDK::APlayerController* PlayerController, SDK::AActor* PawnPlayer, 
                       ImDrawList* drawList, ImVec2 canvasSize) {
        
        FVector ObjectLocation = actorInfo.Actor->K2_GetActorLocation();
        float distance = GetDistance3D(ObjectLocation, PawnPlayer->K2_GetActorLocation()) / 100.0f;
        if (distance > Configuration::cfg_MaxObjectESPDistance / 100.0f)
            return;

        FVector2D screenPos;
        if (!PlayerController->ProjectWorldLocationToScreen(ObjectLocation, &screenPos, true))
            return;

        if (screenPos.X < 0 || screenPos.Y < 0 || screenPos.X > canvasSize.x || screenPos.Y > canvasSize.y)
            return;

        ImU32 objectColor = actorInfo.Color;

        // Draw text
        if (drawList && OverlayBase::Instance->ESPFont) {
            std::string displayText = actorInfo.CharacterName;
            if (Configuration::cfg_ShowDebugDistance) {
                char distanceBuffer[32];
                sprintf_s(distanceBuffer, " [%.0fm]", distance);
                displayText += std::string(distanceBuffer);
            }

            ImVec2 textSize = ImGui::CalcTextSize(displayText.c_str());
            ImVec2 textPos = {
                static_cast<float>(screenPos.X) - textSize.x / 2.0f,
                static_cast<float>(screenPos.Y) - textSize.y / 2.0f
            };

            textPos.x = std::clamp(textPos.x, 4.0f, static_cast<float>(canvasSize.x) - textSize.x - 4.0f);
            textPos.y = std::clamp(textPos.y, 4.0f, static_cast<float>(canvasSize.y) - textSize.y - 4.0f);

            drawTextOutlined(drawList, textPos, objectColor, displayText.c_str(), OverlayBase::Instance->ESPFont);
        }
    }

    bool IsValidScreenPosition(const ImVec2& position, const ImVec2& canvasSize) {
        return position.x >= 0.0f && position.y >= 0.0f && position.x <= canvasSize.x && position.y <= canvasSize.y;
    }

} // namespace ReadyornotESP
