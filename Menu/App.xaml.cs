namespace RonMenu;

public sealed class App : System.Windows.Application
{
    public App()
    {
        Resources.MergedDictionaries.Add(new System.Windows.ResourceDictionary
        {
            Source = new System.Uri("/RONMenu.UI;component/AppResources.xaml", System.UriKind.Relative)
        });

        // This UI runs inside the game process, so an exception that escapes a
        // WPF event handler does not just close a window - it takes the whole
        // game down with it. Swallow anything that reaches the dispatcher and
        // keep the menu alive; a broken setting row is far better than a crash.
        DispatcherUnhandledException += (_, e) =>
        {
            e.Handled = true;
            System.Diagnostics.Debug.WriteLine("RON menu: unhandled UI exception: " + e.Exception);
        };
    }
}
