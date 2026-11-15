#pragma once

#include <string>

namespace Json { class Value; }

namespace MenuPipe {
    void Start();
    void ApplyPending();
    void SetMapName(const std::string& name);
    bool ApplySetting(const std::string& key, const Json::Value& value);
    std::string SerializeColors();
    void LoadColors(const std::string& configJson);
    void SetMenuHotkey(int virtualKey);
    int GetMenuHotkey();
}
