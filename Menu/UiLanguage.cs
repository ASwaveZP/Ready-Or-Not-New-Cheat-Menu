using System.IO;
using System.Text.RegularExpressions;

namespace RonMenu;

internal static class UiLanguage
{
    private static readonly string PreferencePath = Path.Combine(
        System.Environment.GetFolderPath(System.Environment.SpecialFolder.LocalApplicationData),
        "RON New Cheat Menu", "ui-language.txt");

    public static bool Chinese { get; private set; } = true;

    public static void Load()
    {
        try { Chinese = File.ReadAllText(PreferencePath).Trim() != "en"; }
        catch { Chinese = true; }
    }

    public static void Set(bool chinese)
    {
        Chinese = chinese;
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(PreferencePath)!);
            File.WriteAllText(PreferencePath, chinese ? "zh" : "en");
        }
        catch { }
    }

    public static string Category(string category) => !Chinese ? category : category switch
    {
        "Visuals" => "视觉", "Player" => "玩家", "Weapons" => "武器", "World" => "世界", "Aim" => "瞄准", "Settings" => "设置", _ => category
    };

    public static string Text(string english, string chinese) => Chinese ? chinese : english;

    public static string SettingDescription(FeatureSetting setting)
    {
        if (SettingDescriptions.TryGetValue(setting.Key, out var description))
            return Text(description.English, description.Chinese);
        return setting.Description;
    }

    public static string SettingTitle(FeatureSetting setting)
    {
        if (!Chinese) return setting.Title;
        if (ChineseTitles.TryGetValue(setting.Key, out var translated)) return translated;
        var words = new (string, string)[]
        {
            ("Corner Box", "四角框"), ("Box", "方框"), ("Abdomen", "小腹"), ("Unlit Mode", "无光照模式"), ("FOV", "视野"),
            ("ESP", "透视"), ("SERT", "人机队友"), ("No Clip", "穿墙移动"), ("Aimbot", "自动瞄准"),
        ("Teleport", "传送"), ("Spectators", "旁观者"), ("Watermark", "地图状态卡片"),
            ("Draw", "显示"), ("Enable", "启用"), ("Enabled", "已启用"), ("Disable", "关闭"), ("Allow", "允许"), ("Other", "其他"), ("Targeting", "目标选择"), ("Player", "玩家"),
            ("Suspect", "嫌疑人"), ("Civilian", "平民"), ("Squad", "小队"), ("Wounded", "伤员"),
            ("Evidence", "证据"), ("Collected", "已收集"), ("Health", "生命值"), ("Names", "名称"), ("Name", "名称"),
            ("Bones", "骨骼"), ("Lines", "射线"), ("Rays", "射线"), ("Boxes", "方框"), ("Distance", "距离"),
            ("Custom", "自定义"), ("Unlimited", "无限"), ("Infinite", "无限"), ("Instant", "快速"),
            ("Reload", "装填"), ("Ammo", "弹药"), ("Speed", "速度"), ("No", "无"), ("Aim", "瞄准"), ("Silent", "静默"),
            ("Target", "目标"), ("Key", "按键"), ("Circle", "圆环"), ("Radius", "半径"),
            ("Alpha", "透明度"), ("Thickness", "粗细"), ("Max", "最大"), ("Object", "物品"),
            ("Doors", "门"), ("Traps", "陷阱"), ("Debug", "调试"), ("Show", "显示"),
            ("All", "全部"), ("Mode", "模式"), ("Color", "颜色"), ("Value", "数值"),
            ("Offset", "偏移"), ("Method", "方式"), ("Name", "名称"), ("Bone", "骨骼"),
            ("Smoothing", "平滑度"), ("Spread", "散布"), ("Recoil", "后坐力"), ("Sway", "晃动"),
            ("Stamina", "体力"), ("Weight", "负重"), ("Flash", "闪光"), ("Godmode", "无敌"),
            ("Theme", "主题"), ("Menu", "菜单"), ("Language", "语言"), ("Player", "玩家"),
            ("Red", "红色"), ("Blue", "蓝色"), ("Unbound", "未绑定"), ("Card", "卡片"),
            ("Check", "检测"), ("Firefight", "交火"), ("Comply", "服从"), ("Force", "强制"),
            ("Disarm", "解除武装"), ("Arrest", "逮捕"), ("Report", "报告"), ("Incapacitate", "使失能"),
            ("Win", "胜利"), ("Lose", "失败"), ("Modifier", "调节"), ("Movement", "移动"),
            ("Smoothing", "平滑度"), ("Stagger", "硬直"), ("Stamina", "体力"), ("Field", "视野"),
            ("Of", ""), ("To", "至"), ("Top", "屏幕顶部"), ("TP", "传送"), ("Loot", "战利品"),
            ("Class", "类别"), ("Native", "游戏内"), ("Notification", "通知"), ("Smaller", "缩小"),
            ("Unlock", "解锁"), ("Unlit", "无光照"), ("View", "视线"), ("Vis", "视线"),
            ("Visible", "可见"), ("Hidden", "不可见"), ("Aimed", "瞄准目标"), ("AI", "AI"),
            ("Securables", "可加固物"), ("Do", "")
        };
        var title = setting.Title;
        foreach (var (from, to) in words)
            title = Regex.Replace(title, @"(?<![A-Za-z])" + Regex.Escape(from) + @"(?![A-Za-z])", to, RegexOptions.IgnoreCase);
        return title;
    }

    private static readonly System.Collections.Generic.Dictionary<string, (string English, string Chinese)> SettingDescriptions = new()
    {
        ["bShowWatermark"] = ("Show the current map and game connection status in the menu.", "在菜单中显示当前地图与游戏连接状态。"),
        ["bShowSpectators"] = ("Show players who are watching the current session.", "显示正在观看当前对局的玩家。"),
        ["cfg_EnableAimbot"] = ("Move aim toward a valid target while the aim key is held.", "按住瞄准热键时，辅助瞄准有效目标。"),
        ["cfg_AllowOtherTargeting"] = ("Allow aim assistance to select targets outside the main target group.", "允许辅助瞄准选择主要目标类别以外的目标。"),
        ["cfg_SilentAimbotEnabled"] = ("Adjust shots toward the selected target without visibly moving the crosshair.", "让射击方向对准选中目标，同时不明显移动准星。"),
        ["cfg_AimVisCheck"] = ("Limit aim assistance to targets you can see.", "仅对视线可见的目标启用辅助瞄准。"),
        ["cfg_AimbotNoSpread"] = ("Reduce weapon spread while aiming.", "瞄准时减少武器散布。"),
        ["cfg_AimbotNoRecoil"] = ("Reduce weapon kick when firing.", "射击时减少武器后坐。"),
        ["cfg_AimbotNoSway"] = ("Reduce weapon sway while aiming.", "瞄准时减少武器晃动。"),
        ["cfg_InfiniteAmmo"] = ("Keep the loaded ammunition supply from running out.", "保持弹药持续补充，避免弹药耗尽。"),
        ["cfg_InstantReload"] = ("Reload the weapon immediately when a reload is started.", "开始换弹时立即完成装填。"),
        ["cfg_EnableNoClip"] = ("Move through solid obstacles while this mode is enabled.", "开启后可穿过实体障碍移动。"),
        ["cfg_EnableSpeedhack"] = ("Use the speed setting below while holding its hotkey.", "按住加速热键时使用下方设置的移动速度。"),
        ["cfg_DoNoFlash"] = ("Prevent flash effects from obscuring your view.", "避免闪光效果遮挡视野。"),
        ["cfg_DoNoWeight"] = ("Move without the usual movement slowdown from carried weight.", "移动时不受携带重量造成的减速影响。"),
        ["cfg_NoStagger"] = ("Reduce interruptions caused by incoming hits.", "减少受到攻击时的动作中断。"),
        ["cfg_UnlockAllItems"] = ("Make locked equipment and items available.", "解锁可用的装备与物品。"),
        ["cfg_EnableGodmode"] = ("Prevent your character from taking damage.", "避免角色受到伤害。"),
        ["cfg_NoSuspectFirefight"] = ("Stop suspects from starting or continuing a firefight.", "阻止嫌疑人发起或继续交火。"),
        ["cfg_DisarmAllSuspects"] = ("Remove weapons from suspects in the current mission.", "解除当前任务中嫌疑人的武装。"),
        ["cfg_ForceSuspectSurrender"] = ("Make suspects surrender when the feature is active.", "启用后让嫌疑人投降。"),
        ["cfg_AutoComplySuspects"] = ("Have suspects comply with commands automatically.", "让嫌疑人自动服从指令。"),
        ["cfg_DisableAllTraps"] = ("Disable traps found during the current mission.", "关闭当前任务中发现的陷阱。"),
        ["cfg_TeleportToTarget"] = ("Teleport to the selected target when its hotkey is pressed.", "按下热键时传送到选中的目标。"),
        ["cfg_EnableTPTarget"] = ("Enable target selection for teleporting.", "启用传送目标选择。"),
        ["cfg_SmallerTPTargeting"] = ("Use a smaller area when selecting a teleport target.", "缩小传送目标的选择范围。"),
        ["cfg_DrawESPBoxes"] = ("Draw outline boxes around enabled character targets.", "为已启用的角色目标绘制方框。"),
        ["cfg_DrawESPNames"] = ("Show names above enabled character targets.", "在已启用的角色目标上方显示名称。"),
        ["cfg_DrawESPHealth"] = ("Show health information for enabled character targets.", "显示已启用角色目标的生命值。"),
        ["cfg_DrawESPBones"] = ("Draw a skeleton over enabled character targets.", "在已启用的角色目标身上绘制骨骼线。"),
        ["cfg_DrawESPLines"] = ("Show rays from the top of the screen toward enabled character targets.", "从屏幕顶部向已启用的角色目标显示射线。"),
        ["cfg_DrawESPLinesTop"] = ("Start character rays from the top of the screen.", "角色射线从屏幕顶部开始。"),
        ["cfg_DrawPlayerESP"] = ("Show other players in the current session.", "显示当前对局中的其他玩家。"),
        ["cfg_DrawPlayerBoxes"] = ("Draw outline boxes around other players.", "为其他玩家绘制方框。"),
        ["cfg_DrawPlayerNames"] = ("Show names above other players.", "在其他玩家上方显示名称。"),
        ["cfg_DrawPlayerHealth"] = ("Show health information for other players.", "显示其他玩家的生命值。"),
        ["cfg_DrawPlayerBones"] = ("Draw skeletons over other players.", "在其他玩家身上绘制骨骼线。"),
        ["cfg_DrawPlayerLines"] = ("Show rays from the top of the screen toward other players.", "从屏幕顶部向其他玩家显示射线。"),
        ["cfg_DrawPlayerLinesTop"] = ("Start player rays from the top of the screen.", "玩家射线从屏幕顶部开始。"),
        ["cfg_DrawSERTRedESP"] = ("Show red team tactical teammates.", "显示红方人机队友。"),
        ["cfg_DrawSERTRedBoxes"] = ("Draw outline boxes around red team tactical teammates.", "为红方人机队友绘制方框。"),
        ["cfg_DrawSERTRedNames"] = ("Show names above red team tactical teammates.", "在红方人机队友上方显示名称。"),
        ["cfg_DrawSERTRedHealth"] = ("Show health information for red team tactical teammates.", "显示红方人机队友的生命值。"),
        ["cfg_DrawSERTRedBones"] = ("Draw skeletons over red team tactical teammates.", "在红方人机队友身上绘制骨骼线。"),
        ["cfg_DrawSERTRedLines"] = ("Show rays from the top of the screen toward red team tactical teammates.", "从屏幕顶部向红方人机队友显示射线。"),
        ["cfg_DrawSERTBlueESP"] = ("Show blue team tactical teammates.", "显示蓝方人机队友。"),
        ["cfg_DrawSERTBlueBoxes"] = ("Draw outline boxes around blue team tactical teammates.", "为蓝方人机队友绘制方框。"),
        ["cfg_DrawSERTBlueNames"] = ("Show names above blue team tactical teammates.", "在蓝方人机队友上方显示名称。"),
        ["cfg_DrawSERTBlueHealth"] = ("Show health information for blue team tactical teammates.", "显示蓝方人机队友的生命值。"),
        ["cfg_DrawSERTBlueBones"] = ("Draw skeletons over blue team tactical teammates.", "在蓝方人机队友身上绘制骨骼线。"),
        ["cfg_DrawSERTBlueLines"] = ("Show rays from the top of the screen toward blue team tactical teammates.", "从屏幕顶部向蓝方人机队友显示射线。"),
        ["cfg_DrawSuspectESP"] = ("Show suspects in the current mission.", "显示当前任务中的嫌疑人。"),
        ["cfg_DrawSuspectBoxes"] = ("Draw outline boxes around suspects.", "为嫌疑人绘制方框。"),
        ["cfg_DrawSuspectNames"] = ("Show names above suspects.", "在嫌疑人上方显示名称。"),
        ["cfg_DrawSuspectHealth"] = ("Show health information for suspects.", "显示嫌疑人的生命值。"),
        ["cfg_DrawSuspectBones"] = ("Draw skeletons over suspects.", "在嫌疑人身上绘制骨骼线。"),
        ["cfg_DrawSuspectLines"] = ("Show rays from the top of the screen toward suspects.", "从屏幕顶部向嫌疑人显示射线。"),
        ["cfg_DrawCivilianESP"] = ("Show civilians in the current mission.", "显示当前任务中的平民。"),
        ["cfg_DrawCivilianBoxes"] = ("Draw outline boxes around civilians.", "为平民绘制方框。"),
        ["cfg_DrawCivilianNames"] = ("Show names above civilians.", "在平民上方显示名称。"),
        ["cfg_DrawCivilianHealth"] = ("Show health information for civilians.", "显示平民的生命值。"),
        ["cfg_DrawCivilianBones"] = ("Draw skeletons over civilians.", "在平民身上绘制骨骼线。"),
        ["cfg_DrawCivilianLines"] = ("Show rays from the top of the screen toward civilians.", "从屏幕顶部向平民显示射线。"),
        ["cfg_DrawSquadESP"] = ("Show your AI squad members.", "显示你的小队人机队友。"),
        ["cfg_DrawSquadBoxes"] = ("Draw outline boxes around your AI squad members.", "为你的小队人机队友绘制方框。"),
        ["cfg_DrawSquadNames"] = ("Show names above your AI squad members.", "在你的小队人机队友上方显示名称。"),
        ["cfg_DrawSquadHealth"] = ("Show health information for your AI squad members.", "显示你的小队人机队友的生命值。"),
        ["cfg_DrawSquadBones"] = ("Draw skeletons over your AI squad members.", "在你的小队人机队友身上绘制骨骼线。"),
        ["cfg_DrawSquadLines"] = ("Show rays from the top of the screen toward your AI squad members.", "从屏幕顶部向你的小队人机队友显示射线。"),
        ["cfg_DrawWoundedESP"] = ("Show wounded characters in the current mission.", "显示当前任务中的伤员。"),
        ["cfg_DrawWoundedBoxes"] = ("Draw outline boxes around wounded characters.", "为伤员绘制方框。"),
        ["cfg_DrawWoundedNames"] = ("Show names above wounded characters.", "在伤员上方显示名称。"),
        ["cfg_DrawWoundedHealth"] = ("Show health information for wounded characters.", "显示伤员的生命值。"),
        ["cfg_DrawWoundedBones"] = ("Draw skeletons over wounded characters.", "在伤员身上绘制骨骼线。"),
        ["cfg_DrawWoundedLines"] = ("Show rays from the top of the screen toward wounded characters.", "从屏幕顶部向伤员显示射线。"),
        ["cfg_DrawEvidenceESP"] = ("Show evidence that can be found in the mission.", "显示任务中可找到的证物。"),
        ["cfg_DrawCollectedEvidenceESP"] = ("Show evidence that has already been collected.", "显示已经收集的证物。"),
        ["cfg_DrawItemsESP"] = ("Show dropped firearms that can be collected as evidence, excluding equipped weapons and collected evidence bags.", "仅显示掉落后可收取的证物枪械，不显示角色手持武器和已收取证物袋。"),
        ["cfg_DrawHeldItemsESP"] = ("Show the currently held item of AI characters and players; exclude stored inventory and dropped items.", "仅显示 AI 和玩家当前手持的物品，不显示背包、收起或掉落的物品。"),
        ["cfg_DrawDoorsESP"] = ("Show doors around the mission.", "显示任务场景中的门。"),
        ["cfg_DrawTrapsESP"] = ("Show traps and other hazards around the mission.", "显示任务场景中的陷阱与危险物。"),
        ["cfg_DrawSecurablesESP"] = ("Show objects that can be secured.", "显示可执行固定操作的物品。"),
        ["cfg_ShowDebugClassNames"] = ("Show the name of recognized objects above them.", "在识别到的物体上方显示名称。"),
        ["cfg_ShowDebugDistance"] = ("Show the distance to recognized objects.", "显示到识别目标的距离。"),
        ["cfg_EnableCustomFOV"] = ("Use the custom field of view set below.", "使用下方设置的自定义视野角度。"),
        ["cfg_EnableUnlitMode"] = ("Make dark areas easier to see.", "提高暗处的可见度。"),
        ["cfg_CustomFireRate"] = ("Use the configured firing speed for your weapon.", "使用设置的武器射速。"),
        ["cfg_AutoFire"] = ("Force automatic fire mode; hold the fire button to keep firing.", "强制使用全自动模式，按住开火键持续射击。"),
        ["cfg_TriggerBot"] = ("Automatically fire at an active suspect under the crosshair. Stop when the target is lost or the menu is open.", "准星对准未投降且仍有行动能力的嫌疑人时自动开火，移开准星或打开菜单后停止。"),
        ["cfg_BulletPenetration"] = ("Experimental: host/single-player hitscan shots directly damage suspects on the firing line, ignoring walls. Requires authority.", "实验功能：仅单人或主机生效，射线武器沿射击方向直接判定嫌疑人命中，忽略墙体遮挡。"),
        ["cfg_OneHitKill"] = ("Make shots take down targets with one hit.", "让射击一次即可击倒目标。"),
        ["cfg_ForcedCrosshairShot"] = ("Fire toward the center of your screen.", "让射击方向对准屏幕中心。"),
        ["cfg_ShowObjectives"] = ("Show mission objectives, including hidden objective points.", "显示任务目标，包括隐藏任务点。"),
        ["cfg_DrawFOVCircle"] = ("Show the aim range circle on screen.", "在屏幕上显示瞄准范围圆圈。"),
        ["cfg_DrawPlayerCorpses"] = ("Keep player markers visible after death and show a red dead label.", "玩家死亡后保留透视标记，并显示红色“死亡”标记。"),
        ["cfg_DrawAICorpses"] = ("Keep AI teammate markers visible after death and show a red dead label.", "人机队友死亡后保留透视标记，并显示红色“死亡”标记。"),
        ["cfg_DrawSuspectCorpses"] = ("Keep suspect markers visible after death and show a red dead label.", "嫌疑人死亡后保留透视标记，并显示红色“死亡”标记。"),
        ["cfg_DrawCivilianCorpses"] = ("Keep civilian markers visible after death and show a red dead label.", "平民死亡后保留透视标记，并显示红色“死亡”标记。"),
        ["cfg_AimbotFOV"] = ("Set how far from the crosshair aim assistance can select a target.", "设置辅助瞄准可选择目标的范围。"),
        ["cfg_AimbotSmoothing"] = ("Adjust how smoothly aim moves toward a target.", "调整准星跟随目标时的平滑程度。"),
        ["cfg_AimbotKey"] = ("Hold this key to activate aim assistance.", "按住此键启用辅助瞄准。"),
        ["cfg_AimbotTargetBone"] = ("Choose which body area aim assistance follows.", "选择辅助瞄准跟随的身体部位。"),
        ["cfg_AimbotBox"] = ("Choose the outline style used for aim targets.", "选择瞄准目标使用的方框样式。"),
        ["cfg_AimTarget"] = ("Choose which types of characters can be selected.", "选择可被瞄准的角色类型。"),
        ["cfg_FOVRadius"] = ("Set the size of the on-screen aim range circle.", "设置屏幕上瞄准范围圆圈的大小。"),
        ["cfg_CircleAlpha"] = ("Set how transparent the aim range circle appears.", "调整瞄准范围圆圈的透明度。"),
        ["cfg_CustomAmmoCount"] = ("Set the amount of ammunition provided by the ammo option.", "设置弹药功能补充的弹药数量。"),
        ["cfg_CustomFireRateRPM"] = ("Set the firing speed in rounds per minute.", "设置每分钟射击的发数。"),
        ["cfg_speedPlayerValue"] = ("Choose your movement speed while acceleration is enabled.", "设置开启加速时的移动速度。"),
        ["cfg_SpeedKey"] = ("Hold this key to move at the configured acceleration speed.", "按住此键以设置的加速速度移动。"),
        ["cfg_NoClipKey"] = ("Hold this key to pass through solid obstacles.", "按住此键穿过实体障碍。"),
        ["cfg_TeleportKey"] = ("Press this key to teleport to your selected target.", "按下此键传送到选中的目标。"),
        ["cfg_TPTargetKey"] = ("Hold this key to select a target for teleporting.", "按住此键选择传送目标。"),
        ["cfg_TPFOVRadius"] = ("Set the distance around the crosshair used to select a teleport target.", "设置准星周围可选为传送目标的范围。"),
        ["cfg_ActorTeleportOffset"] = ("Adjust how high or low you arrive when teleporting.", "调整传送到达位置的高度。"),
        ["cfg_TPNoTargetDistance"] = ("Set how far to move when teleporting without a selected target.", "未选中目标时设置传送距离。"),
        ["cfg_TeleportMethod"] = ("Choose how the teleport movement is performed.", "选择传送时使用的方式。"),
        ["cfg_InstantWinKey"] = ("Press this key to complete the current mission.", "按下此键完成当前任务。"),
        ["cfg_InstantLoseKey"] = ("Press this key to end the current mission as a failure.", "按下此键以失败状态结束当前任务。"),
        ["cfg_ArrestAllKey"] = ("Press this key to arrest all suspects.", "按下此键逮捕所有嫌疑人。"),
        ["cfg_ReportAllKey"] = ("Press this key to report all available evidence.", "按下此键报告所有可用证物。"),
        ["cfg_CollectAllEvidenceKey"] = ("Press this key to collect all available evidence.", "按下此键收集所有可用证物。"),
        ["cfg_IncapacitateAllKey"] = ("Press this key to incapacitate all characters.", "按下此键使所有角色失去行动能力。"),
        ["cfg_LootAlpha"] = ("Adjust the transparency of item and loot markers.", "调整物品与战利品标记的透明度。"),
        ["cfg_ESPThickness"] = ("Adjust the thickness of ESP outlines and guide lines.", "调整透视方框与连线的粗细。"),
        ["cfg_MaxPlayerESPDistance"] = ("Set the farthest distance at which player markers appear.", "设置玩家标记的最远显示距离。"),
        ["cfg_MaxAIESPDistance"] = ("Set the farthest distance at which character markers appear.", "设置角色标记的最远显示距离。"),
        ["cfg_MaxObjectESPDistance"] = ("Set the farthest distance at which object markers appear.", "设置物体标记的最远显示距离。"),
        ["cfg_DebugMaxDistance"] = ("Set the farthest distance at which extra object details appear.", "设置额外物体信息的最远显示距离。"),
        ["cfg_CustomFOV"] = ("Choose the custom field of view used when enabled.", "设置启用自定义视野后的视野角度。"),
        ["cfg_SelectedLanguage"] = ("Choose the language used for in-game notifications.", "选择游戏内通知使用的语言。"),
        ["cfg_PlayerNameInput"] = ("Set the name shown for your player character.", "设置显示在玩家角色上的名称。"),
        ["PlayerESPColorVisible"] = ("Choose the marker color for visible players.", "设置可见玩家的标记颜色。"),
        ["PlayerESPColorNonVisible"] = ("Choose the marker color for players behind cover.", "设置遮挡后玩家的标记颜色。"),
        ["PlayerESPColorAimed"] = ("Choose the marker color for your current aim target.", "设置当前瞄准目标的标记颜色。"),
        ["SERTRedESPColor"] = ("Choose the marker color for red team teammates.", "设置红方人机队友的标记颜色。"),
        ["SERTBlueESPColor"] = ("Choose the marker color for blue team teammates.", "设置蓝方人机队友的标记颜色。"),
        ["SuspectESPColor"] = ("Choose the marker color for suspects.", "设置嫌疑人的标记颜色。"),
        ["CivilianESPColor"] = ("Choose the marker color for civilians.", "设置平民的标记颜色。"),
        ["SquadESPColor"] = ("Choose the marker color for your squad.", "设置小队成员的标记颜色。"),
        ["WoundedESPColor"] = ("Choose the marker color for wounded characters.", "设置伤员的标记颜色。"),
        ["EvidenceESPColor"] = ("Choose the marker color for evidence.", "设置证物的标记颜色。"),
        ["CollectedEvidenceESPColor"] = ("Choose the marker color for collected evidence.", "设置已收集证物的标记颜色。"),
        ["ItemsESPColor"] = ("Choose the marker color for evidence weapons and held items.", "设置证物武器和手持物品的标记颜色。"),
        ["DoorsESPColor"] = ("Choose the marker color for doors.", "设置门的标记颜色。"),
        ["TrapsESPColor"] = ("Choose the marker color for traps.", "设置陷阱的标记颜色。"),
        ["SecurablesESPColor"] = ("Choose the marker color for objects that can be secured.", "设置可执行固定操作物品的标记颜色。"),
        ["action_AddMagazine"] = ("Add a magazine to your currently equipped weapon.", "为当前装备的武器添加一个弹匣。"),
        ["action_KillSuspects"] = ("Take down all suspects in the current mission.", "击倒当前任务中的所有嫌疑人。"),
        ["action_KillCivilians"] = ("Take down all civilians in the current mission.", "击倒当前任务中的所有平民。"),
        ["action_UnlockDoors"] = ("Unlock doors throughout the current mission.", "解锁当前任务中的所有门。"),
        ["action_CollectEvidence"] = ("Collect all evidence that can be picked up in the current mission.", "收取当前任务中所有可收集的证物。")
    };

    private static readonly System.Collections.Generic.Dictionary<string, string> ChineseTitles = new()
    {
        ["ui_MenuHotkey"] = "菜单显示热键", ["ui_SaveConfig"] = "保存配置",
        ["bShowWatermark"] = "地图状态卡片", ["bShowSpectators"] = "显示旁观者",
        ["cfg_AllowOtherTargeting"] = "允许选择其他目标", ["cfg_SilentAimbotEnabled"] = "静默瞄准",
        ["cfg_AimVisCheck"] = "瞄准视线检测", ["cfg_AimbotNoSpread"] = "移除武器散布",
        ["cfg_AimbotNoRecoil"] = "移除武器后坐力", ["cfg_AimbotNoSway"] = "移除武器晃动",
        ["cfg_EnableNoClip"] = "穿墙", ["cfg_EnableSpeedhack"] = "加速",
        ["cfg_DoNoFlash"] = "关闭闪光效果",
        ["cfg_DoNoWeight"] = "移除负重限制", ["cfg_NoStagger"] = "移除受击硬直",
        ["cfg_EnableGodmode"] = "启用无敌模式", ["cfg_NoSuspectFirefight"] = "禁止嫌疑人交火",
        ["cfg_DisarmAllSuspects"] = "解除所有嫌疑人武装", ["cfg_ForceSuspectSurrender"] = "强制所有嫌疑人投降",
        ["cfg_AutoComplySuspects"] = "嫌疑人自动服从指令", ["cfg_EnableTPTarget"] = "启用目标传送",
        ["cfg_SmallerTPTargeting"] = "缩小传送目标判定范围", ["cfg_AimTarget"] = "瞄准目标类型",
        ["cfg_AimbotBox"] = "目标框样式", ["cfg_AimbotTargetBone"] = "瞄准骨骼位置",
        ["cfg_SelectedLanguage"] = "游戏内通知语言", ["cfg_CustomFOV"] = "自定义视野角度",
        ["cfg_EnableUnlitMode"] = "启用无光照模式", ["cfg_PlayerNameInput"] = "玩家名称",
        ["cfg_ActorTeleportOffset"] = "传送高度偏移", ["cfg_TPNoTargetDistance"] = "无目标时的传送距离",
        ["cfg_CollectAllEvidenceKey"] = "收集全部证据按键", ["cfg_IncapacitateAllKey"] = "使所有角色失能按键",
        ["cfg_DrawESPLinesTop"] = "透视连线起点（屏幕顶部）", ["cfg_DrawPlayerLinesTop"] = "玩家连线起点（屏幕顶部）",
        ["cfg_DrawPlayerESP"] = "显示玩家透视", ["cfg_DrawSERTRedESP"] = "显示红方人机队友透视",
        ["cfg_DrawPlayerCorpses"] = "显示玩家尸体", ["cfg_DrawAICorpses"] = "显示人机队友尸体",
        ["cfg_DrawSuspectCorpses"] = "显示嫌疑人尸体", ["cfg_DrawCivilianCorpses"] = "显示平民尸体",
        ["cfg_DrawSERTBlueESP"] = "显示蓝方人机队友透视", ["cfg_DrawSuspectESP"] = "显示嫌疑人透视",
        ["cfg_DrawCivilianESP"] = "显示平民透视", ["cfg_DrawSquadESP"] = "显示小队透视",
        ["cfg_DrawWoundedESP"] = "显示伤员透视", ["cfg_DrawEvidenceESP"] = "显示证据透视",
        ["cfg_DrawCollectedEvidenceESP"] = "显示已收集证据", ["cfg_DrawItemsESP"] = "显示武器",
        ["cfg_DrawHeldItemsESP"] = "手持物品",
        ["ItemsESPColor"] = "武器与手持物品颜色",
        ["cfg_DrawDoorsESP"] = "显示门透视", ["cfg_DrawTrapsESP"] = "显示陷阱透视",
        ["cfg_DrawSecurablesESP"] = "显示可加固物透视", ["cfg_DrawFOVCircle"] = "显示瞄准视野圈",
        ["cfg_EnableAimbot"] = "启用自动瞄准", ["cfg_InfiniteAmmo"] = "无限弹药",
        ["cfg_AutoFire"] = "强制连发", ["cfg_TriggerBot"] = "自动射击", ["cfg_BulletPenetration"] = "子弹穿墙", ["cfg_OneHitKill"] = "一击必杀",
        ["cfg_speedPlayerValue"] = "加速速度", ["cfg_SpeedKey"] = "加速热键", ["cfg_NoClipKey"] = "穿墙热键",
        ["cfg_ForcedCrosshairShot"] = "强制准星射击", ["cfg_CustomFireRateRPM"] = "自定义射速（发/分钟）",
        ["cfg_CustomFireRate"] = "启用自定义射速",
        ["action_AddMagazine"] = "添加弹匣", ["action_KillSuspects"] = "击杀所有嫌疑人", ["action_KillCivilians"] = "击杀所有平民",
        ["action_UnlockDoors"] = "解锁所有门", ["action_CollectEvidence"] = "收取所有证物", ["cfg_ShowObjectives"] = "任务目标透视",
        ["cfg_InstantReload"] = "快速装填", ["cfg_TeleportToTarget"] = "传送至目标",
        ["cfg_EnableCustomFOV"] = "启用自定义视野角度", ["cfg_ShowDebugClassNames"] = "显示调试类别名称",
        ["cfg_ShowDebugDistance"] = "显示调试距离"
    };
}
