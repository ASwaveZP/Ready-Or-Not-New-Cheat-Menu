#include "MenuPipe.h"
#include "Configuration.h"
#include "InputOwnership.h"
#include "LocalizationManager.h"
#include "../Handlers/ActorCache.h"
#include "DetourManager.h"
#include "../Misc/Utils.h"

#include <Windows.h>
#include <json/json.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <queue>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {
    struct SettingCommand {
        std::string key;
        Json::Value value;
    };

    struct FloatSetting { float* value; float minimum; float maximum; };
    struct IntSetting { int* value; int minimum; int maximum; };
    struct TextSetting { char* value; size_t capacity; };
    struct ColorSetting { ImU32* value; };
    const std::unordered_set<std::string> ActionSettings = { "action_AddMagazine", "action_KillSuspects", "action_KillCivilians", "action_UnlockDoors", "action_CollectEvidence" };

    std::mutex g_queueMutex;
    std::mutex g_statusMutex;
    std::string g_mapName = "Loading world...";
    std::queue<SettingCommand> g_commands;
    std::atomic_bool g_started = false;
    std::atomic_int g_menuHotkey{ VK_INSERT };
    constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\RONFentry.Menu.v1";

    void SetMenuMouseOwnership(bool visible) {
        // Only queue the transition here: this runs on the render thread, while
        // the controller fields and ChangeInputMode() are game-thread state.
        // InputOwnership::ApplyPending() performs the actual switch.
        InputOwnership::RequestMenuState(visible);
    }

    const std::unordered_map<std::string, bool*>& Settings() {
        static const std::unordered_map<std::string, bool*> settings = {
            { "bShowWatermark", &Configuration::bShowWatermark },
            { "bShowSpectators", &Configuration::bShowSpectators },
            { "cfg_EnableAimbot", &Configuration::cfg_EnableAimbot },
            { "cfg_AllowOtherTargeting", &Configuration::cfg_AllowOtherTargeting },
            { "cfg_SilentAimbotEnabled", &Configuration::cfg_SilentAimbotEnabled },
            { "cfg_AimVisCheck", &Configuration::cfg_AimVisCheck },
            { "cfg_AimbotNoSpread", &Configuration::cfg_AimbotNoSpread },
            { "cfg_AimbotNoRecoil", &Configuration::cfg_AimbotNoRecoil },
            { "cfg_AimbotNoSway", &Configuration::cfg_AimbotNoSway },
            { "cfg_InfiniteAmmo", &Configuration::cfg_InfiniteAmmo },
            { "cfg_AutoFire", &Configuration::cfg_AutoFire },
            { "cfg_BulletPenetration", &Configuration::cfg_BulletPenetration },
            { "cfg_OneHitKill", &Configuration::cfg_OneHitKill },
            { "cfg_ForcedCrosshairShot", &Configuration::cfg_ForcedCrosshairShot },
            { "cfg_CustomFireRate", &Configuration::cfg_CustomFireRate },
            { "cfg_ShowObjectives", &Configuration::cfg_ShowObjectives },
            { "cfg_InstantReload", &Configuration::cfg_InstantReload },
            { "cfg_EnableNoClip", &Configuration::cfg_EnableNoClip },
            { "cfg_EnableSpeedhack", &Configuration::cfg_EnableSpeedhack },
            { "cfg_DoNoFlash", &Configuration::cfg_DoNoFlash },
            { "cfg_DoNoWeight", &Configuration::cfg_DoNoWeight },
            { "cfg_NoStagger", &Configuration::cfg_NoStagger },
            { "cfg_UnlockAllItems", &Configuration::cfg_UnlockAllItems },
            { "cfg_EnableGodmode", &Configuration::cfg_EnableGodmode },
            { "cfg_NoSuspectFirefight", &Configuration::cfg_NoSuspectFirefight },
            { "cfg_DisarmAllSuspects", &Configuration::cfg_DisarmAllSuspects },
            { "cfg_ForceSuspectSurrender", &Configuration::cfg_ForceSuspectSurrender },
            { "cfg_AutoComplySuspects", &Configuration::cfg_AutoComplySuspects },
            { "cfg_DisableAllTraps", &Configuration::cfg_DisableAllTraps },
            { "cfg_TeleportToTarget", &Configuration::cfg_TeleportToTarget },
            { "cfg_EnableTPTarget", &Configuration::cfg_EnableTPTarget },
            { "cfg_SmallerTPTargeting", &Configuration::cfg_SmallerTPTargeting },
            { "cfg_DrawFOVCircle", &Configuration::cfg_DrawFOVCircle },
            { "cfg_DrawESPBoxes", &Configuration::cfg_DrawESPBoxes },
            { "cfg_DrawESPNames", &Configuration::cfg_DrawESPNames },
            { "cfg_DrawESPHealth", &Configuration::cfg_DrawESPHealth },
            { "cfg_DrawESPBones", &Configuration::cfg_DrawESPBones },
            { "cfg_DrawESPLines", &Configuration::cfg_DrawESPLines },
            { "cfg_DrawESPLinesTop", &Configuration::cfg_DrawESPLinesTop },
            { "cfg_DrawPlayerESP", &Configuration::cfg_DrawPlayerESP },
            { "cfg_DrawPlayerBoxes", &Configuration::cfg_DrawPlayerBoxes },
            { "cfg_DrawPlayerNames", &Configuration::cfg_DrawPlayerNames },
            { "cfg_DrawPlayerHealth", &Configuration::cfg_DrawPlayerHealth },
            { "cfg_DrawPlayerBones", &Configuration::cfg_DrawPlayerBones },
            { "cfg_DrawPlayerLines", &Configuration::cfg_DrawPlayerLines },
            { "cfg_DrawPlayerLinesTop", &Configuration::cfg_DrawPlayerLinesTop },
            { "cfg_DrawPlayerCorpses", &Configuration::cfg_DrawPlayerCorpses },
            { "cfg_DrawAICorpses", &Configuration::cfg_DrawAICorpses },
            { "cfg_DrawSERTRedESP", &Configuration::cfg_DrawSERTRedESP },
            { "cfg_DrawSERTRedBoxes", &Configuration::cfg_DrawSERTRedBoxes },
            { "cfg_DrawSERTRedNames", &Configuration::cfg_DrawSERTRedNames },
            { "cfg_DrawSERTRedHealth", &Configuration::cfg_DrawSERTRedHealth },
            { "cfg_DrawSERTRedBones", &Configuration::cfg_DrawSERTRedBones },
            { "cfg_DrawSERTRedLines", &Configuration::cfg_DrawSERTRedLines },
            { "cfg_DrawSERTBlueESP", &Configuration::cfg_DrawSERTBlueESP },
            { "cfg_DrawSERTBlueBoxes", &Configuration::cfg_DrawSERTBlueBoxes },
            { "cfg_DrawSERTBlueNames", &Configuration::cfg_DrawSERTBlueNames },
            { "cfg_DrawSERTBlueHealth", &Configuration::cfg_DrawSERTBlueHealth },
            { "cfg_DrawSERTBlueBones", &Configuration::cfg_DrawSERTBlueBones },
            { "cfg_DrawSERTBlueLines", &Configuration::cfg_DrawSERTBlueLines },
            { "cfg_DrawSuspectESP", &Configuration::cfg_DrawSuspectESP },
            { "cfg_DrawSuspectBoxes", &Configuration::cfg_DrawSuspectBoxes },
            { "cfg_DrawSuspectNames", &Configuration::cfg_DrawSuspectNames },
            { "cfg_DrawSuspectHealth", &Configuration::cfg_DrawSuspectHealth },
            { "cfg_DrawSuspectBones", &Configuration::cfg_DrawSuspectBones },
            { "cfg_DrawSuspectLines", &Configuration::cfg_DrawSuspectLines },
            { "cfg_DrawSuspectCorpses", &Configuration::cfg_DrawSuspectCorpses },
            { "cfg_DrawCivilianESP", &Configuration::cfg_DrawCivilianESP },
            { "cfg_DrawCivilianBoxes", &Configuration::cfg_DrawCivilianBoxes },
            { "cfg_DrawCivilianNames", &Configuration::cfg_DrawCivilianNames },
            { "cfg_DrawCivilianHealth", &Configuration::cfg_DrawCivilianHealth },
            { "cfg_DrawCivilianBones", &Configuration::cfg_DrawCivilianBones },
            { "cfg_DrawCivilianLines", &Configuration::cfg_DrawCivilianLines },
            { "cfg_DrawCivilianCorpses", &Configuration::cfg_DrawCivilianCorpses },
            { "cfg_DrawSquadESP", &Configuration::cfg_DrawSquadESP },
            { "cfg_DrawSquadBoxes", &Configuration::cfg_DrawSquadBoxes },
            { "cfg_DrawSquadNames", &Configuration::cfg_DrawSquadNames },
            { "cfg_DrawSquadHealth", &Configuration::cfg_DrawSquadHealth },
            { "cfg_DrawSquadBones", &Configuration::cfg_DrawSquadBones },
            { "cfg_DrawSquadLines", &Configuration::cfg_DrawSquadLines },
            { "cfg_DrawWoundedESP", &Configuration::cfg_DrawWoundedESP },
            { "cfg_DrawWoundedBoxes", &Configuration::cfg_DrawWoundedBoxes },
            { "cfg_DrawWoundedNames", &Configuration::cfg_DrawWoundedNames },
            { "cfg_DrawWoundedHealth", &Configuration::cfg_DrawWoundedHealth },
            { "cfg_DrawWoundedBones", &Configuration::cfg_DrawWoundedBones },
            { "cfg_DrawWoundedLines", &Configuration::cfg_DrawWoundedLines },
            { "cfg_DrawEvidenceESP", &Configuration::cfg_DrawEvidenceESP },
            { "cfg_DrawCollectedEvidenceESP", &Configuration::cfg_DrawCollectedEvidenceESP },
            { "cfg_DrawItemsESP", &Configuration::cfg_DrawItemsESP },
            { "cfg_DrawTrapsESP", &Configuration::cfg_DrawTrapsESP },
            { "cfg_ShowDebugClassNames", &Configuration::cfg_ShowDebugClassNames },
            { "cfg_ShowDebugDistance", &Configuration::cfg_ShowDebugDistance },
            { "cfg_EnableCustomFOV", &Configuration::cfg_EnableCustomFOV },
            { "cfg_EnableUnlitMode", &Configuration::cfg_EnableUnlitMode }
        };
        return settings;
    }

    const std::unordered_map<std::string, FloatSetting>& FloatSettings() {
        static const std::unordered_map<std::string, FloatSetting> settings = {
            { "cfg_FOVRadius", { &Configuration::cfg_FOVRadius, 0.f, 1000.f } },
            { "cfg_CircleAlpha", { &Configuration::cfg_CircleAlpha, 0.f, 1.f } },
            { "cfg_LootAlpha", { &Configuration::cfg_LootAlpha, 0.f, 1.f } },
            { "cfg_ESPThickness", { &Configuration::cfg_ESPThickness, 0.5f, 5.f } },
            { "cfg_MaxPlayerESPDistance", { &Configuration::cfg_MaxPlayerESPDistance, 0.f, 100000.f } },
            { "cfg_AimbotFOV", { &Configuration::cfg_AimbotFOV, 1.f, 360.f } },
            { "cfg_AimbotSmoothing", { &Configuration::cfg_AimbotSmoothing, 0.f, 100.f } },
            { "cfg_speedPlayerValue", { &Configuration::cfg_speedPlayerValue, 1.f, 10.f } },
            { "cfg_TPFOVRadius", { &Configuration::cfg_TPFOVRadius, 0.f, 1000.f } },
            { "cfg_ActorTeleportOffset", { &Configuration::cfg_ActorTeleportOffset, -1000.f, 1000.f } },
            { "cfg_TPNoTargetDistance", { &Configuration::cfg_TPNoTargetDistance, 0.f, 1000.f } },
            { "cfg_MaxAIESPDistance", { &Configuration::cfg_MaxAIESPDistance, 0.f, 100000.f } },
            { "cfg_MaxObjectESPDistance", { &Configuration::cfg_MaxObjectESPDistance, 0.f, 100000.f } },
            { "cfg_DebugMaxDistance", { &Configuration::cfg_DebugMaxDistance, 0.f, 100000.f } },
            { "cfg_CustomFOV", { &Configuration::cfg_CustomFOV, 60.f, 130.f } },
            { "cfg_CustomFireRateRPM", { &Configuration::cfg_CustomFireRateRPM, 60.f, 1800.f } }
        };
        return settings;
    }

    const std::unordered_map<std::string, IntSetting>& IntSettings() {
        static const std::unordered_map<std::string, IntSetting> settings = {
            { "cfg_AimbotTargetBone", { &Configuration::cfg_AimbotTargetBone, 0, 4 } },
            { "cfg_AimbotBox", { &Configuration::cfg_AimbotBox, 0, 1 } },
            { "cfg_AimbotKey", { &Configuration::cfg_AimbotKey, 0, 2047 } },
            { "cfg_AimTarget", { &Configuration::cfg_AimTarget, 0, 5 } },
            { "cfg_CustomAmmoCount", { &Configuration::cfg_CustomAmmoCount, 0, 999 } },
            { "cfg_SpeedKey", { &Configuration::cfg_SpeedKey, 0, 2047 } },
            { "cfg_NoClipKey", { &Configuration::cfg_NoClipKey, 0, 2047 } },
            { "cfg_TeleportKey", { &Configuration::cfg_TeleportKey, 0, 2047 } },
            { "cfg_TeleportMethod", { &Configuration::cfg_TeleportMethod, 0, 3 } },
            { "cfg_TPTargetKey", { &Configuration::cfg_TPTargetKey, 0, 2047 } },
            { "cfg_IncapacitateAllKey", { &Configuration::cfg_IncapacitateAllKey, 0, 2047 } },
            { "cfg_InstantWinKey", { &Configuration::cfg_InstantWinKey, 0, 2047 } },
            { "cfg_InstantLoseKey", { &Configuration::cfg_InstantLoseKey, 0, 2047 } },
            { "cfg_ArrestAllKey", { &Configuration::cfg_ArrestAllKey, 0, 2047 } },
            { "cfg_ReportAllKey", { &Configuration::cfg_ReportAllKey, 0, 2047 } },
            { "cfg_CollectAllEvidenceKey", { &Configuration::cfg_CollectAllEvidenceKey, 0, 2047 } },
            { "cfg_SelectedLanguage", { &Configuration::cfg_SelectedLanguage, 0, 3 } }
        };
        return settings;
    }

    const std::unordered_map<std::string, TextSetting>& TextSettings() {
        static const std::unordered_map<std::string, TextSetting> settings = {
            { "cfg_PlayerNameInput", { Configuration::cfg_PlayerNameInput, sizeof(Configuration::cfg_PlayerNameInput) } }
        };
        return settings;
    }

    const std::unordered_map<std::string, ColorSetting>& ColorSettings() {
        static const std::unordered_map<std::string, ColorSetting> settings = {
            { "PlayerESPColorVisible", { &PlayerESPColorVisible } },
            { "PlayerESPColorNonVisible", { &PlayerESPColorNonVisible } },
            { "PlayerESPColorAimed", { &PlayerESPColorAimed } },
            { "SERTRedESPColor", { &SERTRedESPColor } },
            { "SERTBlueESPColor", { &SERTBlueESPColor } },
            { "SuspectESPColor", { &SuspectESPColor } },
            { "CivilianESPColor", { &CivilianESPColor } },
            { "SquadESPColor", { &SquadESPColor } },
            { "WoundedESPColor", { &WoundedESPColor } },
            { "EvidenceESPColor", { &EvidenceESPColor } },
            { "CollectedEvidenceESPColor", { &CollectedEvidenceESPColor } },
            { "ItemsESPColor", { &ItemsESPColor } },
            { "TrapsESPColor", { &TrapsESPColor } },
        };
        return settings;
    }

    std::string ColorToHex(ImU32 color) {
        char text[10]{};
        std::snprintf(text, sizeof(text), "#%02X%02X%02X%02X",
            color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF, (color >> 24) & 0xFF);
        return text;
    }

    bool ParseColor(const std::string& text, ImU32& color) {
        if (text.size() != 9 || text[0] != '#') return false;
        char* end = nullptr;
        const auto rgba = std::strtoul(text.c_str() + 1, &end, 16);
        if (!end || *end != '\0') return false;
        const auto red = (rgba >> 24) & 0xFF;
        const auto green = (rgba >> 16) & 0xFF;
        const auto blue = (rgba >> 8) & 0xFF;
        const auto alpha = rgba & 0xFF;
        color = IM_COL32(red, green, blue, alpha);
        return true;
    }

    std::string SerializeColorSettings() {
        Json::Value colors(Json::objectValue);
        for (const auto& [key, setting] : ColorSettings())
            colors[key] = ColorToHex(*setting.value);
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        return Json::writeString(builder, colors);
    }

    Json::Value ReadSettings() {
        Json::Value state(Json::objectValue);
        for (const auto& [key, value] : Settings()) state[key] = *value;
        for (const auto& [key, value] : FloatSettings()) state[key] = *value.value;
        for (const auto& [key, value] : IntSettings()) state[key] = *value.value;
        for (const auto& [key, value] : TextSettings()) state[key] = value.value;
        for (const auto& [key, value] : ColorSettings()) state[key] = ColorToHex(*value.value);
        return state;
    }

    void QueueCommand(std::string key, Json::Value value) {
        std::lock_guard lock(g_queueMutex);
        g_commands.push({ std::move(key), std::move(value) });
    }

    void Serve() {
        while (true) {
            HANDLE pipe = CreateNamedPipeW(kPipeName,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) {
                Sleep(1000);
                continue;
            }

            const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (connected) {
                std::string line;
                char buffer[512];
                DWORD bytesRead = 0;
                bool complete = false;
                while (!complete && ReadFile(pipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
                    for (DWORD i = 0; i < bytesRead; ++i) {
                        if (buffer[i] == '\n') { complete = true; break; }
                        if (line.size() < 8192) line.push_back(buffer[i]);
                    }
                }

                if (!line.empty()) {
                    Json::Value request;
                    Json::CharReaderBuilder builder;
                    std::string errors;
                    std::istringstream input(line);
                    if (Json::parseFromStream(builder, input, &request, &errors)) {
                        Json::Value response(Json::objectValue);
                        if (request["type"].asString() == "get") {
                            response["type"] = "state";
                            response["settings"] = ReadSettings();
                            response["settings"]["ui_MenuHotkey"] = MenuPipe::GetMenuHotkey();
                            {
                                std::lock_guard statusLock(g_statusMutex);
                                response["status"]["map"] = g_mapName;
                            }
                        } else if (request["type"].asString() == "set" && request["key"].isString()) {
                            const auto key = request["key"].asString();
                            const auto& value = request["value"];
                            const bool supported = (key == "ui_MenuVisible" && value.isBool())
                                || (key == "ui_SaveConfig" && value.isBool())
                                || (key == "ui_MenuHotkey" && value.isInt())
                                || (Settings().contains(key) && value.isBool())
                                || (ActionSettings.contains(key) && value.isBool())
                                || ((FloatSettings().contains(key) || IntSettings().contains(key)) && value.isNumeric())
                                || ((TextSettings().contains(key) || ColorSettings().contains(key)) && value.isString());
                            response["type"] = "ack";
                            response["key"] = key;
                            response["ok"] = supported;
                            if (supported && key == "ui_MenuHotkey") MenuPipe::SetMenuHotkey(value.asInt());
                            else if (supported) QueueCommand(key, value);
                        }
                        if (!response.isNull()) {
                            Json::StreamWriterBuilder writerBuilder;
                            writerBuilder["indentation"] = "";
                            const std::string responseText = Json::writeString(writerBuilder, response) + "\n";
                            DWORD written = 0;
                            WriteFile(pipe, responseText.data(), static_cast<DWORD>(responseText.size()), &written, nullptr);
                        }
                    }
                }
            }
            FlushFileBuffers(pipe);
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
    }
}

namespace MenuPipe {
    void SetMapName(const std::string& name) {
        std::lock_guard lock(g_statusMutex);
        g_mapName = name.empty() ? "Loading world..." : name;
    }

    void Start() {
        bool expected = false;
        if (!g_started.compare_exchange_strong(expected, true)) return;
        std::thread(Serve).detach();
    }

    void SetMenuHotkey(int virtualKey) { g_menuHotkey.store(std::clamp(virtualKey, 0, 2047)); }
    int GetMenuHotkey() { return g_menuHotkey.load(); }

    void ApplyPending() {
        std::queue<SettingCommand> pending;
        {
            std::lock_guard lock(g_queueMutex);
            pending.swap(g_commands);
        }
        bool changed = false;
        while (!pending.empty()) {
            const auto& command = pending.front();
            if (command.key == "ui_MenuVisible" && command.value.isBool())
                SetMenuMouseOwnership(command.value.asBool());
            else if (command.key == "ui_SaveConfig" && command.value.isBool() && command.value.asBool())
                Configuration::Save();
            else
                changed = ApplySetting(command.key, command.value) || changed;
            pending.pop();
        }
        if (changed) Configuration::Save();
    }

    std::string SerializeColors() {
        return SerializeColorSettings();
    }

    void LoadColors(const std::string& configJson) {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream input(configJson);
        if (!Json::parseFromStream(builder, input, &root, &errors) || !root["ESP Colors"].isObject()) return;
        const auto& colors = root["ESP Colors"];
        for (const auto& key : colors.getMemberNames()) {
            const auto found = ColorSettings().find(key);
            if (found == ColorSettings().end() || !colors[key].isString()) continue;
            ImU32 color = *found->second.value;
            if (ParseColor(colors[key].asString(), color)) *found->second.value = color;
        }
    }

    bool ApplySetting(const std::string& key, const Json::Value& value) {
        if (ActionSettings.contains(key) && value.isBool() && value.asBool()) {
            auto* controller = GetARONPCForESP();
            if (!controller || !IsActorValid(controller)) return false;
            if (key == "action_KillSuspects") controller->KillAllSuspects();
            else if (key == "action_KillCivilians") controller->KillAllCivilians();
            else if (key == "action_UnlockDoors") {
                auto* gameState = UReadyOrNotStatics::GetReadyOrNotGameState();
                if (!gameState || !IsActorValid(gameState)) return false;
                for (auto* door : gameState->AllDoors)
                    if (door && IsActorValid(door)) door->SetLocked(false);
            }
            else if (key == "action_CollectEvidence") {
                auto* world = GetWorld();
                auto* player = controller->K2_GetPawn();
                if (!world || !player || !IsActorValid(player)) return false;
                TArray<AActor*> evidenceActors;
                UGameplayStatics::GetAllActorsOfClass(reinterpret_cast<UObject*>(world), AEvidenceActor::StaticClass(), &evidenceActors);
                for (AActor* actor : evidenceActors) {
                    auto* evidence = reinterpret_cast<AEvidenceActor*>(actor);
                    if (!evidence || !IsActorValid(evidence) || !evidence->EvidenceComponent || !IsComponentValid(evidence->EvidenceComponent)) continue;
                    if (evidence->EvidenceComponent->CanBeCollected()) static_cast<AReadyOrNotCharacter*>(player)->PickupEvidence(evidence);
                }
            }
            else if (key == "action_AddMagazine") {
                auto* weapon = GetCurrentWeapon();
                if (!weapon || !IsActorValid(weapon) || !weapon->IsA(ABaseMagazineWeapon::StaticClass())) return false;
                FMagazine magazine{};
                magazine.Ammo = static_cast<uint16>(std::clamp(Configuration::cfg_CustomAmmoCount, 1, 999));
                magazine.AmmoType = 1;
                static_cast<ABaseMagazineWeapon*>(weapon)->Server_AddMagazine(magazine);
            }
            return true;
        }
        if (const auto found = Settings().find(key); found != Settings().end() && value.isBool()) {
            *found->second = value.asBool();
            return true;
        }
        if (const auto found = FloatSettings().find(key); found != FloatSettings().end() && value.isNumeric()) {
            *found->second.value = std::clamp(value.asFloat(), found->second.minimum, found->second.maximum);
            return true;
        }
        if (const auto found = IntSettings().find(key); found != IntSettings().end() && value.isNumeric()) {
            const int settingValue = std::clamp(value.asInt(), found->second.minimum, found->second.maximum);
            if (key == "cfg_SelectedLanguage") {
                LocalizationManager::SetLanguage(static_cast<Language>(settingValue));
            } else {
                *found->second.value = settingValue;
            }
            return true;
        }
        if (const auto found = TextSettings().find(key); found != TextSettings().end() && value.isString()) {
            strncpy_s(found->second.value, found->second.capacity, value.asCString(), _TRUNCATE);
            return true;
        }
        if (const auto found = ColorSettings().find(key); found != ColorSettings().end() && value.isString()) {
            ImU32 color = *found->second.value;
            if (!ParseColor(value.asString(), color)) return false;
            *found->second.value = color;
            return true;
        }
        return false;
    }
}

