using System.IO;
using System.Web.Script.Serialization;

namespace RonMenu;

internal static class MenuPreferences
{
    private static readonly string PathName = Path.Combine(
        System.Environment.GetFolderPath(System.Environment.SpecialFolder.LocalApplicationData),
        "RON New Cheat Menu", "menu-config.json");
    private static readonly JavaScriptSerializer Json = new();

    public static int Hotkey { get; private set; } = 0x2D;
    public static int Theme { get; private set; }

    public static void Load()
    {
        try
        {
            var values = Json.Deserialize<System.Collections.Generic.Dictionary<string, int>>(File.ReadAllText(PathName));
            if (values == null) return;
            if (values.TryGetValue("menuHotkey", out var hotkey) && hotkey >= 0 && hotkey <= 2047) Hotkey = hotkey;
            if (values.TryGetValue("theme", out var theme) && theme >= 0 && theme < 4) Theme = theme;
            if (values.TryGetValue("chinese", out var chinese)) UiLanguage.Set(chinese != 0);
        }
        catch { }
    }

    public static void Save(int? hotkey = null, int? theme = null)
    {
        if (hotkey.HasValue) Hotkey = hotkey.Value;
        if (theme.HasValue) Theme = theme.Value;
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(PathName)!);
            File.WriteAllText(PathName, Json.Serialize(new { menuHotkey = Hotkey, theme = Theme, chinese = UiLanguage.Chinese ? 1 : 0 }));
        }
        catch { }
    }
}
