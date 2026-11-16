namespace RonMenu;

public static class AppVersion
{
    public static string Display => "V" + typeof(AppVersion).Assembly.GetName().Version!.ToString(3);
}
