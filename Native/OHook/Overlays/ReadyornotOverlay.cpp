#include "ReadyornotOverlay.h"
#include "DetourManager.h"

#include "console.hpp"
#include "ImGuiExt.h"
#include "SDKExt.h"
#include "Utils.h"
#include "Configuration.h"
#include "MenuPipe.h"
#include "LocalizationManager.h"
#include "ActorCache.h"
#include "Overlays/ESP.h"

#include "Features/WorldFeatures.h"

#include <Windows.h>
#include <algorithm>
#include <thread>
#include <fstream>
#include <TlHelp32.h>
#include <imgui_internal.h>
#include <Fonts.h>
#include <unordered_set>

using namespace SDK;

std::vector<std::string> debugger;
DetourManager gDetourManager;

// ======== DRAWING STUFF ======== //

void ReadyornotOverlay::DrawGuiESP() {    
    ReadyornotESP::DrawGuiESP();
}

void ReadyornotOverlay::DrawGuiFOVCircle() {
    ReadyornotESP::DrawGuiFOVCircle();
}

void ReadyornotOverlay::DrawHUD() {
    static bool bConfigLoaded = false;
    if (!bConfigLoaded) {
        Configuration::Load(static_cast<ReadyornotOverlay*>(OverlayBase::Instance));
        LocalizationManager::Initialize();
        bConfigLoaded = true;
    }

    ReadyornotOverlay* Overlay = static_cast<ReadyornotOverlay*>(OverlayBase::Instance);
    WorldFeatures::Func_UpdateCurrentLevel(Overlay);
    MenuPipe::SetMapName(Overlay ? Overlay->CurrentMap : "Loading world...");

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    /*
    * Theme loading
    */
    if (getTheme) {
        switch (Configuration::selectedTheme) {
        case 0:
            ImGui::StyleColorsDark();
            break;
        case 1:
            ImGui::Spectrum::StyleColorsSpectrum();
            break;
        case 2:
            ImGui::Magma::StyleColorsMagma();
            break;
        case 3:
            Configuration::ApplyCustomTheme();
            break;
        default:
            ImGui::StyleColorsDark();
            break;
        }
        getTheme = false;
    }

    /*
     * Notification rendering
     */
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImVec4 windowBg = ImGui::ColorConvertU32ToFloat4(ImGui::GetColorU32(ImGuiCol_WindowBg));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(windowBg.x, windowBg.y, windowBg.z, 1.00f));
    ImGui::RenderNotifications();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(1);

    /*
     * Notification(s) for first use-case scenario
     */
    static bool displayedFirstUseNotif, displayedCorruptConfigNotif = false;
    if (Configuration::bFirstUse && !displayedFirstUseNotif) {
        ImGui::InsertNotification({ ImGuiToastType::Info, 8000, LOC("notification.first_use").c_str() });
        displayedFirstUseNotif = true;
    }

    APlayerController* PC = GetAPC();
    AReadyOrNotPlayerController* ARONPC = GetARONPC();

    static APlayerController* s_HookedPC = nullptr;
    static AReadyOrNotPlayerState* s_HookedPS = nullptr;
    static AReadyOrNotCharacter* s_HookedARONChar = nullptr;
    static UCharacterHealthComponent* s_HookedHealthComp = nullptr;
    static UCharacterMovementComponent* s_HookedARONMovement = nullptr;
    static ABaseWeapon* s_HookedWeapon = nullptr;
    static AReadyOrNotCharacter* s_HookedRONChar = nullptr;
    static ACharacter* s_HookedCharacter = nullptr;
    static AHUD* s_HookedHUD = nullptr;

    // Hook PC
    if (PC && PC != s_HookedPC) {
        gDetourManager.SetupDetour(PC);
        s_HookedPC = PC;
    }

    // Hook PlayerState
    if (PC && PC->PlayerState && PC->PlayerState != s_HookedPS) {
        gDetourManager.SetupDetour(PC->PlayerState);
        s_HookedPS = static_cast<AReadyOrNotPlayerState*>(PC->PlayerState);
    }

    // Hook Ready or Not character + components
    AReadyOrNotCharacter* ARONChar = GetARONCharacter();
    if (ARONChar && ARONChar != s_HookedARONChar) {
        gDetourManager.SetupDetour(ARONChar);
        s_HookedARONChar = ARONChar;
    }

    // Hook generic Ready or Not character
    AReadyOrNotCharacter* RONChar = GetAHCharacter();
    if (RONChar && RONChar != s_HookedRONChar) {
       gDetourManager.SetupDetour(RONChar);
       s_HookedRONChar = RONChar;
    }

    // Hook generic pawn
    ACharacter* Char = static_cast<ACharacter*>(PC ? PC->K2_GetPawn() : nullptr);
    if (Char && Char != s_HookedCharacter) {
       gDetourManager.SetupDetour(Char);
       s_HookedCharacter = Char;
    }

    // Hook HUD once
    if (PC && PC->MyHUD && PC->MyHUD != s_HookedHUD) {
        gDetourManager.SetupDetour(PC->MyHUD);
        s_HookedHUD = PC->MyHUD;
    }
}

