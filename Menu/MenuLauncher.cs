using System.Threading;
using System.Windows;
using System.Windows.Interop;

namespace RonMenu;

public static class MenuLauncher
{
    private static readonly object StartupLock = new();
    private static readonly System.Collections.Generic.List<string> PendingToggles = new();
    private static volatile App? _app;
    private static volatile MainWindow? _window;

    public static int Start(string argument)
    {
        var uiThread = new Thread(() =>
        {
            try
            {
                var app = new App();
                UiLanguage.Load();
                MenuPreferences.Load();
                app.ShutdownMode = ShutdownMode.OnExplicitShutdown;

                string[] pending;
                lock (StartupLock)
                {
                    _app = app;
                    pending = PendingToggles.ToArray();
                    PendingToggles.Clear();
                }
                foreach (var pendingArgument in pending) ScheduleToggle(app, pendingArgument);
                app.Run();
            }
            catch (System.Exception error)
            {
                MessageBox.Show(error.ToString(), "RON Menu", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }) { IsBackground = true, Name = "RON WPF Menu" };
        uiThread.SetApartmentState(ApartmentState.STA);
        uiThread.Start();
        return 0;
    }

    public static int Toggle(string argument)
    {
        App? app;
        lock (StartupLock)
        {
            app = _app;
            if (app == null)
            {
                PendingToggles.Add(argument ?? string.Empty);
                return 0;
            }
        }
        ScheduleToggle(app, argument ?? string.Empty);
        return 0;
    }

    private static void ScheduleToggle(App app, string argument)
    {
        app.Dispatcher.BeginInvoke(new System.Action(() =>
        {
            var window = _window;
            if (window is { IsVisible: true })
            {
                // Hide instead of Close. Destroying a topmost window hands the
                // foreground to whatever sits next in the z-order, which is how
                // unrelated background apps ended up on top of the game.
                window.HideMenu();
                return;
            }

            // One window for the whole session. Re-creating it on every toggle
            // made the game lose and regain activation each time, and a
            // fullscreen game that loses activation drops out of fullscreen.
            if (window is null)
            {
                window = new MainWindow();
                if (long.TryParse(argument, out var rawHandle) && rawHandle != 0)
                    new WindowInteropHelper(window).Owner = new IntPtr(rawHandle);
                _window = window;
                window.Closed += (_, _) =>
                {
                    if (ReferenceEquals(_window, window)) _window = null;
                };
            }
            window.ShowMenu();
        }));
    }
}
