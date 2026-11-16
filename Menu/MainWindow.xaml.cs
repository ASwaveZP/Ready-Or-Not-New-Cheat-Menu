using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Threading;
using System.Globalization;
using System.Windows.Interop;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Windows.Shapes;
using System.IO;
using System.Web.Script.Serialization;

namespace RonMenu;

public partial class MainWindow : Window
{
    private const int GwlExStyle = -20;
    private const long WsExNoActivate = 0x08000000L;
    private const long WsExToolWindow = 0x00000080L;
    private const int WmMouseActivate = 0x0021;
    private const int WmNcHitTest = 0x0084;
    private const int MaNoActivate = 3;
    private const int SwShowNoActivate = 4;
    private static readonly IntPtr HwndNotTopmost = new(-2);
    private const uint SwpNoActivate = 0x0010;
    private const uint SwpNoMove = 0x0002;
    private const uint SwpNoSize = 0x0001;
    private const uint SwpNoZOrder = 0x0004;
    private static readonly IntPtr HwndTopmost = new(-1);
    private const int ResizeBorderPixels = 8;
    private const int WhKeyboardLl = 13;
    private const int WmKeyDown = 0x0100;
    private const int WmSysKeyDown = 0x0104;
    private const int VkEscape = 0x1B;
    private const int VkBack = 0x08;
    private const int VkDelete = 0x2E;
    private const int VkControl = 0x11, VkShift = 0x10, VkMenu = 0x12;
    private const int HotkeyCtrl = 0x100, HotkeyShift = 0x200, HotkeyAlt = 0x400, HotkeyKeyMask = 0xFF;
    private const int WmKeyUp = 0x0101, WmSysKeyUp = 0x0105;
    private static MainWindow? _keyboardCaptureWindow;
    private static readonly LowLevelKeyboardProc KeyboardHookCallback = KeyboardHook;
    private IntPtr _keyboardHook;

    private delegate IntPtr LowLevelKeyboardProc(int code, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    private struct KeyboardData { public uint VirtualKey, ScanCode, Flags, Time; public IntPtr ExtraInfo; }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeRect { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW", SetLastError = true)]
    private static extern IntPtr GetWindowLongPtr(IntPtr hwnd, int index);

    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW", SetLastError = true)]
    private static extern IntPtr SetWindowLongPtr(IntPtr hwnd, int index, IntPtr value);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool GetWindowRect(IntPtr hwnd, out NativeRect rect);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool SetWindowPos(IntPtr hwnd, IntPtr insertAfter, int x, int y, int cx, int cy, uint flags);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hwnd, int cmdShow);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll", SetLastError = true)]
    private static extern IntPtr SetWindowsHookEx(int idHook, LowLevelKeyboardProc callback, IntPtr module, uint threadId);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool UnhookWindowsHookEx(IntPtr hook);

    [DllImport("user32.dll")]
    private static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr wParam, IntPtr lParam);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr GetModuleHandle(string? moduleName);

    private readonly MenuPipeClient _pipe = new();
    private readonly DispatcherTimer _connectionTimer = new() { Interval = TimeSpan.FromSeconds(2) };
    private Dictionary<string, object> _nativeState = new();
    private bool _wasConnected;
    private string _page = "Visuals";
    private int _selectedTheme = MenuPreferences.Theme;
    private FeatureSetting? _captureMenuHotkey;
    private Button? _captureMenuHotkeyButton;
    private FeatureSetting? _captureSetting;
    private Button? _captureButton;
    private int _captureModifiers;
    private ComboBox? _profileCombo;
    private TextBox? _profileNameBox;
    private TextBlock? _profileStatus;
    private bool _profileInputActive;
    private static readonly JavaScriptSerializer ProfileJson = new();
    private static readonly string ProfileDirectory = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RON New Cheat Menu", "Profiles");
    private readonly Dictionary<string, Border> _pageCache = new();
    private readonly List<Border> _mapStatusCards = new();
    private readonly List<TextBlock> _mapStatusTexts = new();
    private readonly List<TextBlock> _mapStatusConnections = new();
    private readonly List<Shape> _aimRegionShapes = new();
    private TextBlock? _aimRegionLabel;
    private Ellipse? _aimRegionMarker;
    private int _selectedAimBone;
    private bool _centerQueued;
    private bool? _publishedMenuVisible;
    private CursorOverlay? _cursorOverlay;
    private Color _accent = Color.FromRgb(0x35, 0xD8, 0xC4);

    public MainWindow()
    {
        InitializeComponent();
        ApplyLocalization();
        SetConnection(false);
        ShowPage("Visuals");
        SourceInitialized += (_, _) => ConfigureNonActivatingWindow();
        SizeChanged += (_, _) =>
        {
            if (FxCanvas.ActualWidth > 0) ScanLine.Width = FxCanvas.ActualWidth;
            QueueCenterOnOwner();
        };
        IsVisibleChanged += (_, _) =>
        {
            PublishMenuVisibility();
            SetOverlayPriority(IsVisible);
            if (IsVisible)
            {
                _connectionTimer.Start();
                QueueCenterOnOwner();
                // The poll timer is suspended while the menu is hidden, so take a
                // fresh snapshot as soon as it comes back.
                _ = UpdateConnectionAsync();
            }
            if (!IsVisible)
            {
                _connectionTimer.Stop();
                EndKeyboardCapture();
                StopBackgroundAnimation();
            }
        };
        StateChanged += (_, _) => PublishMenuVisibility();
        Closed += (_, _) =>
        {
            // Explicitly publish the hide transition so the native side
            // returns mouse ownership to the game even if IsVisibleChanged
            // does not fire during window teardown.
            _publishedMenuVisible = false;
            _ = _pipe.SetAsync("ui_MenuVisible", false);
            _cursorOverlay?.HideOverlay();
            _connectionTimer.Stop();
            EndKeyboardCapture();
            StopBackgroundAnimation();
        };
        PreviewKeyDown += Window_PreviewKeyDown;
        PreviewMouseDown += Window_PreviewMouseDown;
        _connectionTimer.Tick += async (_, _) =>
        {
            // Nothing on screen to refresh while the menu is hidden; polling the
            // pipe anyway meant a thread-pool wake-up plus a pipe connect every
            // two seconds for as long as the game ran.
            if (IsVisible) await UpdateConnectionAsync();
        };
        if (IsVisible) _connectionTimer.Start();
        Loaded += async (_, _) =>
        {
            ApplyTheme(_selectedTheme);
            await _pipe.SetAsync("ui_MenuHotkey", MenuPreferences.Hotkey);
            await UpdateConnectionAsync();
            ShowPage(_page);
        };
    }

    public void PlayOpeningAnimation()
    {
        RootFrame.Opacity = 0;
        var scale = new ScaleTransform(0.96, 0.96);
        RootFrame.RenderTransformOrigin = new Point(0.5, 0.5);
        RootFrame.RenderTransform = scale;
        var fade = new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(240)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } };
        var growX = new DoubleAnimation(0.96, 1, TimeSpan.FromMilliseconds(300)) { EasingFunction = new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.18 } };
        var growY = new DoubleAnimation(0.96, 1, TimeSpan.FromMilliseconds(300)) { EasingFunction = new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.18 } };
        Timeline.SetDesiredFrameRate(fade, 30);
        Timeline.SetDesiredFrameRate(growX, 30);
        Timeline.SetDesiredFrameRate(growY, 30);
        RootFrame.BeginAnimation(OpacityProperty, fade);
        scale.BeginAnimation(ScaleTransform.ScaleXProperty, growX);
        scale.BeginAnimation(ScaleTransform.ScaleYProperty, growY);
        StartBackgroundAnimation();
    }

    // The background animation is the only continuously running work in the
    // menu, so it is kept deliberately cheap:
    //  * every animated element moves through a TranslateTransform instead of
    //    Canvas.Left/Top, which avoids a layout pass per frame;
    //  * nothing uses an Effect (a software blur on a layered window costs more
    //    than the rest of the menu put together and made the game hitch);
    //  * every timeline is capped at 30 fps, which is indistinguishable from 60
    //    for slow drifting glows but halves the compositor load;
    //  * it only runs while the menu is visible and is torn down on hide.
    private void StartBackgroundAnimation()
    {
        var width = Math.Max(ActualWidth, 840);
        var height = Math.Max(ActualHeight, 540);
        ScanLine.Width = Math.Max(FxCanvas.ActualWidth, width);

        Drift(GlowOrbShift, TranslateTransform.XProperty, 0, width - 280, 9);
        Drift(GlowOrb2Shift, TranslateTransform.XProperty, 0, -(width - 220), 12);
        Drift(ScanShift, TranslateTransform.YProperty, -2, height, 6, autoReverse: false);
        Drift(PulseFrame, OpacityProperty, 0.10, 0.32, 3.6);
    }

    private static void Drift(DependencyObject target, DependencyProperty property, double from, double to, double seconds, bool autoReverse = true)
    {
        var animation = new DoubleAnimation(from, to, TimeSpan.FromSeconds(seconds))
        {
            AutoReverse = autoReverse,
            RepeatBehavior = RepeatBehavior.Forever,
            EasingFunction = new SineEase { EasingMode = EasingMode.EaseInOut }
        };
        Timeline.SetDesiredFrameRate(animation, 30);
        if (target is IAnimatable animatable) animatable.BeginAnimation(property, animation);
    }

    private void StopBackgroundAnimation()
    {
        GlowOrbShift.BeginAnimation(TranslateTransform.XProperty, null);
        GlowOrb2Shift.BeginAnimation(TranslateTransform.XProperty, null);
        ScanShift.BeginAnimation(TranslateTransform.YProperty, null);
        PulseFrame.BeginAnimation(OpacityProperty, null);
        PulseFrame.Opacity = 0.16;
    }

    private void ConfigureNonActivatingWindow()
    {
        var handle = new WindowInteropHelper(this).Handle;
        var style = GetWindowLongPtr(handle, GwlExStyle).ToInt64();
        // WS_EX_TOOLWINDOW keeps the overlay out of Alt+Tab and makes the window
        // manager far less likely to treat it as a foreground candidate, so the
        // game is never deactivated while the menu is up.
        SetWindowLongPtr(handle, GwlExStyle, new IntPtr(style | WsExNoActivate | WsExToolWindow));
        SetWindowPos(handle, HwndNotTopmost, 0, 0, 0, 0, SwpNoMove | SwpNoSize | SwpNoActivate);
        HwndSource.FromHwnd(handle)?.AddHook(WindowMessageHook);
    }

    public void SetOverlayPriority(bool visible)
    {
        var handle = new WindowInteropHelper(this).Handle;
        if (handle == IntPtr.Zero) return;
        // Window.Topmost is deliberately not touched: WPF's setter runs its own
        // SetWindowPos pass and that can move activation off the game. Drive the
        // z-order directly instead, and never use HWND_BOTTOM - sinking a window
        // to the bottom reshuffles the whole z-order and promotes whatever else
        // is on screen.
        SetWindowPos(handle, visible ? HwndTopmost : HwndNotTopmost, 0, 0, 0, 0,
            SwpNoMove | SwpNoSize | SwpNoActivate);
    }

    public void ShowMenu()
    {
        if (!IsVisible) Show();
        var handle = new WindowInteropHelper(this).Handle;
        // Show() on a WS_EX_NOACTIVATE window should not activate, but re-assert
        // it explicitly so the game keeps the foreground and stays fullscreen.
        if (handle != IntPtr.Zero) ShowWindow(handle, SwShowNoActivate);
        SetOverlayPriority(true);
        PlayOpeningAnimation();

        // Shown after the panel so it lands above it in the topmost band.
        _cursorOverlay ??= new CursorOverlay();
        _cursorOverlay.SetAccent(_accent);
        _cursorOverlay.ShowOverlay();
        // The panel queues its own re-centring after layout; make sure that pass
        // cannot leave the panel above the glyph.
        Dispatcher.BeginInvoke(new Action(() => _cursorOverlay?.BringAbove()), DispatcherPriority.Background);

        RestoreGameForeground();
        // Show() can hand activation to another window a moment later, once the
        // window manager has finished placing it. Re-check after layout.
        Dispatcher.BeginInvoke(new Action(RestoreGameForeground), DispatcherPriority.Background);
    }

    public void HideMenu()
    {
        // The pointer is never moved here. Warping it would desync it from the
        // position the game last saw, and the game cursor would then be drawn at
        // the old spot while clicks land on the new one. The pointer simply
        // stays wherever the menu cursor left it.
        _cursorOverlay?.HideOverlay();
        Hide();
        SetOverlayPriority(false);
        RestoreGameForeground();
    }

    // The menu is WS_EX_NOACTIVATE, so it never needs to be the foreground
    // window. Whenever it appears or disappears the game must be put back in
    // front: a fullscreen game that loses activation drops out of fullscreen
    // (desktop shows, game shrinks to a small window), and a disappearing
    // topmost window lets Windows promote whatever is next in the z-order.
    private void RestoreGameForeground()
    {
        var owner = new WindowInteropHelper(this).Owner;
        if (owner == IntPtr.Zero || GetForegroundWindow() == owner) return;
        SetForegroundWindow(owner);
    }

    private void PublishMenuVisibility()
    {
        // Keep visibility transitions ordered; stale asynchronous "visible"
        // messages must never arrive after the hide transition.
        var visible = IsVisible && WindowState != WindowState.Minimized;
        if (_publishedMenuVisible == visible) return;
        _publishedMenuVisible = visible;
        _ = _pipe.SetAsync("ui_MenuVisible", visible);
    }

    private void QueueCenterOnOwner()
    {
        if (_centerQueued || !IsVisible) return;
        _centerQueued = true;
        Dispatcher.BeginInvoke(new Action(() =>
        {
            _centerQueued = false;
            CenterOnOwner();
        }), DispatcherPriority.Background);
    }

    private void CenterOnOwner()
    {
        var handle = new WindowInteropHelper(this).Handle;
        var owner = new WindowInteropHelper(this).Owner;
        if (handle == IntPtr.Zero || owner == IntPtr.Zero ||
            !GetWindowRect(owner, out var ownerRect) || !GetWindowRect(handle, out var menuRect)) return;
        var width = menuRect.Right - menuRect.Left;
        var height = menuRect.Bottom - menuRect.Top;
        var x = ownerRect.Left + ((ownerRect.Right - ownerRect.Left) - width) / 2;
        var y = ownerRect.Top + ((ownerRect.Bottom - ownerRect.Top) - height) / 2;
        // Move only, never re-order. This runs after ShowMenu() has already shown
        // the cursor overlay, and pushing the panel to HWND_TOPMOST here would
        // lift it above the overlay - which is what buried the menu cursor under
        // the panel and left it visible only over the game area.
        SetWindowPos(handle, IntPtr.Zero, x, y, 0, 0,
            SwpNoSize | SwpNoZOrder | SwpNoActivate);
    }

    private IntPtr WindowMessageHook(IntPtr hwnd, int message, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (message == WmMouseActivate)
        {
            if (!_profileInputActive)
            {
                handled = true;
                return new IntPtr(MaNoActivate);
            }
            return IntPtr.Zero;
        }

        if (message != WmNcHitTest || !GetWindowRect(hwnd, out var rect)) return IntPtr.Zero;
        var packed = lParam.ToInt64();
        var x = unchecked((short)(packed & 0xffff));
        var y = unchecked((short)((packed >> 16) & 0xffff));
        var left = x < rect.Left + ResizeBorderPixels;
        var right = x >= rect.Right - ResizeBorderPixels;
        var top = y < rect.Top + ResizeBorderPixels;
        var bottom = y >= rect.Bottom - ResizeBorderPixels;
        var hit = top ? (left ? 13 : right ? 14 : 12)
            : bottom ? (left ? 16 : right ? 17 : 15)
            : left ? 10 : right ? 11 : 0;
        if (hit == 0) return IntPtr.Zero;
        handled = true;
        return new IntPtr(hit);
    }

    private static IntPtr KeyboardHook(int code, IntPtr wParam, IntPtr lParam)
    {
        var captureWindow = _keyboardCaptureWindow;
        if (code >= 0 && captureWindow != null && (captureWindow._captureSetting != null || captureWindow._captureMenuHotkey != null))
        {
            var data = Marshal.PtrToStructure<KeyboardData>(lParam);
            var message = wParam.ToInt32();
            var key = (int)data.VirtualKey;
            var modifier = key is VkControl or 0xA2 or 0xA3 ? HotkeyCtrl
                : key is VkShift or 0xA0 or 0xA1 ? HotkeyShift
                : key is VkMenu or 0xA4 or 0xA5 ? HotkeyAlt : 0;
            if (modifier != 0)
            {
                if (message == WmKeyDown || message == WmSysKeyDown) captureWindow._captureModifiers |= modifier;
                else if (message == WmKeyUp || message == WmSysKeyUp) captureWindow._captureModifiers &= ~modifier;
                var modifierState = captureWindow._captureModifiers;
                captureWindow.Dispatcher.BeginInvoke(new Action(() => captureWindow.UpdateCapturePrompt(modifierState)));
                return new IntPtr(1);
            }
            if (message == WmKeyDown || message == WmSysKeyDown)
            {
                var binding = key | captureWindow._captureModifiers;
                captureWindow.Dispatcher.BeginInvoke(new Action(() =>
                {
                    if (key == VkEscape)
                    {
                        if (captureWindow._captureSetting != null) captureWindow.FinishKeyCapture(VkEscape);
                        else captureWindow.CancelMenuHotkeyCapture();
                    }
                    else if ((key == VkBack || key == VkDelete) && captureWindow._captureModifiers == 0)
                    {
                        if (captureWindow._captureSetting != null) captureWindow.FinishKeyCapture(0);
                        else _ = captureWindow.CompleteMenuHotkeyCaptureAsync(0);
                    }
                    else if (captureWindow._captureSetting != null) captureWindow.FinishKeyCapture(binding);
                    else _ = captureWindow.CompleteMenuHotkeyCaptureAsync(binding);
                }));
                return new IntPtr(1);
            }
        }
        return CallNextHookEx(captureWindow?._keyboardHook ?? IntPtr.Zero, code, wParam, lParam);
    }

    private void BeginKeyboardCapture()
    {
        _captureModifiers = 0;
        _keyboardCaptureWindow = this;
        _keyboardHook = SetWindowsHookEx(WhKeyboardLl, KeyboardHookCallback, GetModuleHandle(null), 0);
    }

    private void EndKeyboardCapture()
    {
        if (_keyboardHook != IntPtr.Zero) UnhookWindowsHookEx(_keyboardHook);
        _keyboardHook = IntPtr.Zero;
        if (_keyboardCaptureWindow == this) _keyboardCaptureWindow = null;
    }

    private void Nav_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button button) return;
        foreach (var nav in new[] { VisualsNav, PlayerNav, WeaponNav, WorldNav, AimNav, SettingsNav }) nav.Tag = null;
        button.Tag = "Active";
        ShowPage(button == VisualsNav ? "Visuals" : button == PlayerNav ? "Player" : button == WeaponNav ? "Weapons" : button == WorldNav ? "World" : button == AimNav ? "Aim" : "Settings");
    }

    private void ShowPage(string page)
    {
        if (!SettingCatalog.AllSettings.Any(setting => setting.Category == page)) page = "Visuals";
        _page = page;
        var subtitle = page switch
        {
            "Visuals" => UiLanguage.Text("Character and object overlays", "角色与物体显示选项"),
            "Player" => UiLanguage.Text("Movement, survivability, and player state", "移动、生存与玩家状态"),
            "Weapons" => UiLanguage.Text("Weapon and ammunition options", "武器与弹药选项"),
            "World" => UiLanguage.Text("World objects, evidence, and environment", "场景物体、证据与环境选项"),
            "Aim" => UiLanguage.Text("Aim assistance and target selection", "瞄准辅助与目标选择"),
            _ => UiLanguage.Text("Menu, overlays, and general options", "菜单、显示与通用选项")
        };
        PageTitle.Text = UiLanguage.Category(page);
        PageSubtitle.Text = subtitle;
        var titleShift = new TranslateTransform(0, -5);
        PageTitle.RenderTransform = titleShift;
        PageTitle.Opacity = 0;
        var titleFade = new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(210)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } };
        var titleSlide = new DoubleAnimation(-5, 0, TimeSpan.FromMilliseconds(250)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } };
        Timeline.SetDesiredFrameRate(titleFade, 30);
        Timeline.SetDesiredFrameRate(titleSlide, 30);
        PageTitle.BeginAnimation(OpacityProperty, titleFade);
        titleShift.BeginAnimation(TranslateTransform.YProperty, titleSlide);
        if (!_pageCache.TryGetValue(page, out var card))
        {
            card = new Border { CornerRadius = new CornerRadius(12), Padding = new Thickness(22), Margin = new Thickness(0, 0, 10, 14) };
            card.SetResourceReference(BackgroundProperty, "PanelBrush");
            if (page == "Aim")
            {
                var columns = new Grid();
                columns.ColumnDefinitions.Add(new ColumnDefinition());
                columns.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(190) });
                var settings = new StackPanel { Margin = new Thickness(0, 0, 12, 0) };
                settings.Children.Add(CreateStatusCard());
                foreach (var setting in SettingCatalog.AllSettings.Where(setting => setting.Category == page).OrderBy(UiLanguage.SettingTitle))
                    settings.Children.Add(CreateSetting(setting));
                Grid.SetColumn(settings, 0);
                columns.Children.Add(settings);
                var modelPanel = CreateAimModelPanel();
                Grid.SetColumn(modelPanel, 1);
                columns.Children.Add(modelPanel);
                card.Child = columns;
            }
            else
            {
                var stack = new StackPanel();
                stack.Children.Add(CreateStatusCard());
                if (page == "Settings")
                {
                    stack.Children.Add(CreateLanguageSetting());
                    stack.Children.Add(CreateThemeSetting());
                    stack.Children.Add(CreateMenuHotkeySetting());
                    stack.Children.Add(CreateSaveConfigSetting());
                }
                if (page == "Visuals")
                    AddVisualGroups(stack);
                else
                    foreach (var setting in SettingCatalog.AllSettings.Where(setting => setting.Category == page)
                                 .OrderBy(setting => page == "Player" ? setting.Key switch
                                 {
                                     "cfg_EnableSpeedhack" => 0,
                                     "cfg_speedPlayerValue" => 1,
                                     _ => 2
                                 } : 0)
                                 .ThenBy(UiLanguage.SettingTitle))
                        stack.Children.Add(CreateSetting(setting));
                card.Child = stack;
            }
            _pageCache.Add(page, card);
        }
        PageHost.Children.Clear();
        PageHost.Children.Add(card);        PageHost.Opacity = 0;
        PageHost.RenderTransform = new TranslateTransform(0, 9);
        var fade = new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(220)) { EasingFunction = new QuadraticEase { EasingMode = EasingMode.EaseOut } };
        var slide = new DoubleAnimation(9, 0, TimeSpan.FromMilliseconds(260)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } };
        Timeline.SetDesiredFrameRate(fade, 30);
        Timeline.SetDesiredFrameRate(slide, 30);
        PageHost.BeginAnimation(OpacityProperty, fade);
        PageHost.RenderTransform.BeginAnimation(TranslateTransform.YProperty, slide);
    }

    private void AddVisualGroups(StackPanel stack)
    {
        var settings = SettingCatalog.AllSettings.Where(setting => setting.Category == "Visuals").ToArray();
        var used = new HashSet<string>();

        void AddGroup(string title, params string[] keys)
        {
            var groupSettings = keys.Select(key => settings.FirstOrDefault(setting => setting.Key == key))
                .Where(setting => setting is not null).Cast<FeatureSetting>().ToArray();
            foreach (var setting in groupSettings) used.Add(setting.Key);
            stack.Children.Add(CreateVisualGroup(title, groupSettings));
        }

        AddGroup(UiLanguage.Text("Suspects and Wounded", "嫌疑人和伤员"),
            "cfg_DrawSuspectESP", "cfg_DrawSuspectBoxes", "cfg_DrawSuspectNames", "cfg_DrawSuspectHealth", "cfg_DrawSuspectBones", "cfg_DrawSuspectLines",
            "cfg_DrawSuspectCorpses",
            "cfg_DrawWoundedESP", "cfg_DrawWoundedBoxes", "cfg_DrawWoundedNames", "cfg_DrawWoundedHealth", "cfg_DrawWoundedBones", "cfg_DrawWoundedLines");
        AddGroup(UiLanguage.Text("AI Teammates", "人机队友"),
            "cfg_DrawSERTRedESP", "cfg_DrawSERTRedBoxes", "cfg_DrawSERTRedNames", "cfg_DrawSERTRedHealth", "cfg_DrawSERTRedBones", "cfg_DrawSERTRedLines",
            "cfg_DrawSERTBlueESP", "cfg_DrawSERTBlueBoxes", "cfg_DrawSERTBlueNames", "cfg_DrawSERTBlueHealth", "cfg_DrawSERTBlueBones", "cfg_DrawSERTBlueLines",
            "cfg_DrawSquadESP", "cfg_DrawSquadBoxes", "cfg_DrawSquadNames", "cfg_DrawSquadHealth", "cfg_DrawSquadBones", "cfg_DrawSquadLines",
            "cfg_DrawAICorpses");
        AddGroup(UiLanguage.Text("Player Teammates", "玩家队友"),
            "cfg_DrawPlayerESP", "cfg_DrawPlayerBoxes", "cfg_DrawPlayerNames", "cfg_DrawPlayerHealth", "cfg_DrawPlayerBones", "cfg_DrawPlayerLines",
            "cfg_DrawPlayerCorpses");

        AddGroup(UiLanguage.Text("Civilians", "平民"),
            "cfg_DrawCivilianESP", "cfg_DrawCivilianBoxes", "cfg_DrawCivilianNames", "cfg_DrawCivilianHealth", "cfg_DrawCivilianBones", "cfg_DrawCivilianLines",
            "cfg_DrawCivilianCorpses");
        var otherSettings = settings.Where(setting => !used.Contains(setting.Key)).OrderBy(UiLanguage.SettingTitle).ToArray();
        AddGroup(UiLanguage.Text("Other · Evidence and Objectives", "其他（证物、任务及隐藏任务点）"), otherSettings.Select(setting => setting.Key).ToArray());
    }

    private UIElement CreateVisualGroup(string title, IReadOnlyList<FeatureSetting> settings, UIElement? emptyContent = null)
    {
        var expanded = false;
        var body = new StackPanel { Visibility = Visibility.Collapsed, Margin = new Thickness(18, 0, 8, 8) };
        if (emptyContent is not null)
        {
            if (emptyContent is FrameworkElement emptyElement) emptyElement.Margin = new Thickness(8, 7, 8, 0);
            body.Children.Add(emptyContent);
        }
        foreach (var setting in settings)
        {
            var row = CreateSetting(setting);
            if (row is FrameworkElement rowElement) rowElement.Margin = new Thickness(0, 0, 0, 1);
            body.Children.Add(row);
        }

        var header = new Grid { Cursor = System.Windows.Input.Cursors.Hand };
        header.ColumnDefinitions.Add(new ColumnDefinition());
        header.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        header.Children.Add(new TextBlock { Text = title, FontSize = 13, FontWeight = FontWeights.SemiBold, Foreground = (Brush)FindResource("TextBrush"), VerticalAlignment = VerticalAlignment.Center });
        var arrow = new TextBlock { Text = "⌄", FontSize = 18, Foreground = (Brush)FindResource("AccentBrush"), VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(10, -3, 0, 0) };
        Grid.SetColumn(arrow, 1);
        header.Children.Add(arrow);

        var headerShell = new Border { Padding = new Thickness(14, 9, 12, 9), CornerRadius = new CornerRadius(7), BorderThickness = new Thickness(1), Child = header, Cursor = System.Windows.Input.Cursors.Hand, Margin = new Thickness(0, 0, 0, 4) };
        headerShell.SetResourceReference(BackgroundProperty, "RaisedBrush");
        headerShell.SetResourceReference(BorderBrushProperty, "LineBrush");
        headerShell.MouseEnter += (_, _) => headerShell.SetResourceReference(BackgroundProperty, "HoverBrush");
        headerShell.MouseLeave += (_, _) => headerShell.SetResourceReference(BackgroundProperty, "RaisedBrush");
        headerShell.MouseLeftButtonUp += (_, e) =>
        {
            expanded = !expanded;
            body.Visibility = expanded ? Visibility.Visible : Visibility.Collapsed;
            arrow.Text = expanded ? "⌃" : "⌄";
            e.Handled = true;
        };

        var group = new StackPanel { Margin = new Thickness(0, 2, 0, 7) };
        group.Children.Add(headerShell);
        group.Children.Add(body);
        return group;
    }

    private UIElement CreateStatusCard()
    {
        var grid = new Grid();
        grid.ColumnDefinitions.Add(new ColumnDefinition()); grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var copy = new StackPanel();
        copy.Children.Add(new TextBlock { Text = UiLanguage.Text("GAME STATUS", "游戏状态"), FontSize = 10, Foreground = (Brush)FindResource("MutedBrush"), FontWeight = FontWeights.Bold });
        var mapText = new TextBlock { FontSize = 14, Margin = new Thickness(0, 6, 0, 0) };
        copy.Children.Add(mapText);
        var connectionText = new TextBlock { VerticalAlignment = VerticalAlignment.Center, FontSize = 10, FontWeight = FontWeights.Bold };
        Grid.SetColumn(connectionText, 1); grid.Children.Add(copy); grid.Children.Add(connectionText);
        // SetResourceReference is the code-behind equivalent of DynamicResource:
        // a direct brush reference would keep pointing at the palette that was
        // active when the element was built, so the cached pages would never
        // repaint after a theme switch.
        var card = new Border { BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(9), Padding = new Thickness(14, 12, 14, 12), Margin = new Thickness(0, 0, 0, 14), Child = grid };
        card.SetResourceReference(BackgroundProperty, "RaisedBrush");
        card.SetResourceReference(BorderBrushProperty, "LineBrush");
        _mapStatusCards.Add(card); _mapStatusTexts.Add(mapText); _mapStatusConnections.Add(connectionText);
        UpdateMapStatus();
        return card;
    }

    private UIElement CreateAimModelPanel()
    {
        _selectedAimBone = Math.Max(0, Math.Min(4, (int)GetNumberState("cfg_AimbotTargetBone", 0)));
        _aimRegionShapes.Clear();
        var panel = new Border
        {
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(10),
            Padding = new Thickness(15),
            MinHeight = 480,
            VerticalAlignment = VerticalAlignment.Top
        };
        panel.SetResourceReference(BackgroundProperty, "RaisedBrush");
        panel.SetResourceReference(BorderBrushProperty, "LineBrush");
        var content = new StackPanel();
        content.Children.Add(new TextBlock { Text = UiLanguage.Text("TARGET MODEL", "目标模型"), FontSize = 11, FontWeight = FontWeights.Bold, Foreground = (Brush)FindResource("AccentBrush") });
        content.Children.Add(new TextBlock { Text = UiLanguage.Text("Selected hit region", "当前标注部位"), FontSize = 10, Foreground = (Brush)FindResource("MutedBrush"), Margin = new Thickness(0, 5, 0, 0) });
        _aimRegionLabel = new TextBlock { FontSize = 16, FontWeight = FontWeights.SemiBold, Margin = new Thickness(0, 3, 0, 6) };
        content.Children.Add(_aimRegionLabel);

        var preview = new Grid { Width = 220, Height = 330, HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 8, 0, 8) };
        preview.Children.Add(new EmbeddedGlbPreview());
        var markerLayer = new Canvas { Width = 220, Height = 330, IsHitTestVisible = false };
        _aimRegionMarker = new Ellipse { Width = 38, Height = 48, StrokeThickness = 2, Fill = new SolidColorBrush(Color.FromArgb(55, 42, 216, 196)), Stroke = (Brush)FindResource("AccentBrush"), Effect = new System.Windows.Media.Effects.DropShadowEffect { Color = ((SolidColorBrush)FindResource("AccentBrush")).Color, BlurRadius = 18, ShadowDepth = 0, Opacity = 0.95 } };
        markerLayer.Children.Add(_aimRegionMarker);
        preview.Children.Add(markerLayer);
        content.Children.Add(new Border { Background = new SolidColorBrush(Color.FromArgb(50, 5, 13, 18)), BorderBrush = new SolidColorBrush(Color.FromRgb(42, 83, 91)), BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(8), Child = preview });
        content.Children.Add(new Border { Height = 1, Background = new SolidColorBrush(Color.FromRgb(42, 67, 74)), Margin = new Thickness(0, 4, 0, 10) });
        content.Children.Add(new TextBlock { Text = UiLanguage.Text("GLB · embedded in the menu", "GLB · 已封装在菜单中"), FontSize = 9, Foreground = (Brush)FindResource("MutedBrush"), Margin = new Thickness(0, 8, 0, 0) });
        panel.Child = content;
        UpdateAimRegionHighlight();
        return panel;
    }

    private void AddAimRegion(Canvas canvas, Shape shape, double left, double top, int region)
    {
        shape.Stroke = new SolidColorBrush(Color.FromRgb(90, 132, 139));
        shape.StrokeThickness = Math.Max(shape.StrokeThickness, 1.5);
        shape.Fill = new SolidColorBrush(Color.FromRgb(35, 61, 67));
        if (shape is Line line)
        {
            line.Stroke = new SolidColorBrush(Color.FromRgb(90, 132, 139));
            line.Fill = Brushes.Transparent;
        }
        Canvas.SetLeft(shape, left);
        Canvas.SetTop(shape, top);
        canvas.Children.Add(shape);
        _aimRegionShapes.Add(shape);
        shape.Tag = region;
    }

    private void UpdateAimRegionHighlight()
    {
        if (_aimRegionLabel is not null)
        {
            var region = _selectedAimBone switch
            {
                0 => UiLanguage.Text("Head", "头部"),
                1 => UiLanguage.Text("Neck", "颈部"),
                2 => UiLanguage.Text("Chest", "胸部"),
                3 => UiLanguage.Text("Abdomen", "小腹"),
                _ => UiLanguage.Text("Legs", "腿部")
            };
            _aimRegionLabel.Text = region;
        }
        var accent = (Brush)FindResource("AccentBrush");
        if (_aimRegionMarker is not null)
        {
            _aimRegionMarker.Width = _selectedAimBone is 0 or 1 ? 38 : 54;
            _aimRegionMarker.Height = _selectedAimBone is 0 or 1 ? 42 : 48;
            var markerTop = _selectedAimBone switch { 0 => 53.0, 1 => 82.0, 2 => 127.0, 3 => 172.0, _ => 242.0 };
            Canvas.SetLeft(_aimRegionMarker, (220.0 - _aimRegionMarker.Width) * 0.5);
            Canvas.SetTop(_aimRegionMarker, markerTop);
        }
        var baseStroke = new SolidColorBrush(Color.FromRgb(90, 132, 139));
        foreach (var shape in _aimRegionShapes)
        {
            var region = (int)shape.Tag;
            var selected = _selectedAimBone switch
            {
                0 => region == 0,
                1 => region == 1,
                2 or 3 => region == 2,
                4 => region == 4,
                _ => region == 5
            };
            shape.Stroke = selected ? accent : baseStroke;
            shape.Fill = selected ? new SolidColorBrush(Color.FromArgb(190, 42, 216, 196)) : new SolidColorBrush(Color.FromRgb(35, 61, 67));
            if (shape is Line line) line.Stroke = selected ? accent : baseStroke;
            shape.Effect = selected ? new System.Windows.Media.Effects.DropShadowEffect { Color = ((SolidColorBrush)accent).Color, BlurRadius = 15, ShadowDepth = 0, Opacity = 0.8 } : null;
        }
    }

    private UIElement CreateLanguageSetting()
    {
        var combo = new ComboBox { Width = 190, ItemsSource = UiLanguage.Chinese ? new[] { "简体中文", "英语" } : new[] { "Chinese (Simplified)", "English" }, SelectedIndex = UiLanguage.Chinese ? 0 : 1, Padding = new Thickness(8, 5, 8, 5) };
        combo.SelectionChanged += (_, _) =>
        {
            if (combo.SelectedIndex < 0) return;
            UiLanguage.Set(combo.SelectedIndex == 0);
            MenuPreferences.Save();
            _pageCache.Clear();
            _mapStatusCards.Clear();
            _mapStatusTexts.Clear();
            _mapStatusConnections.Clear();
            ApplyLocalization();
            ShowPage(_page);
        };
        return CreateSettingRow(new FeatureSetting("Settings", "Interface Language", "Choose the menu display language.", "ui_language"), combo);
    }

    private void ApplyLocalization()
    {
        MenuLabel.Text = UiLanguage.Text("MENU", "菜单");
        BrandSubtitle.Text = UiLanguage.Text("BY ASWAVE", "BY ASWAVE  ·  战术控制台");
        HeaderMeta.Text = UiLanguage.Text("BY ASWAVE  ·  MENU", "BY ASWAVE  ·  菜单");
        FooterHint.Text = UiLanguage.Text("Settings apply through the in-game bridge", "设置通过游戏内接口生效");
        VisualsNav.Content = "◈   " + UiLanguage.Category("Visuals");
        PlayerNav.Content = "♙   " + UiLanguage.Category("Player");
        WeaponNav.Content = "⌁   " + UiLanguage.Category("Weapons");
        WorldNav.Content = "◎   " + UiLanguage.Category("World");
        AimNav.Content = "⊙   " + UiLanguage.Category("Aim");
        SettingsNav.Content = "⚙   " + UiLanguage.Category("Settings");
    }

    private UIElement CreateThemeSetting()
    {
        var combo = new ComboBox
        {
            Width = 190,
            ItemsSource = UiLanguage.Chinese ? new[] { "暗色", "全息", "品红", "熔岩" } : new[] { "Dark", "Holo", "Magenta", "Lava" },
            SelectedIndex = _selectedTheme,
            Padding = new Thickness(8, 5, 8, 5)
        };
        combo.SelectionChanged += (_, _) => ApplyTheme(combo.SelectedIndex);
        return CreateSettingRow(new FeatureSetting("Settings", UiLanguage.Text("Menu Theme", "菜单主题"), UiLanguage.Text("Choose a color palette for the menu.", "选择菜单配色方案。"), "menu_theme"), combo);
    }

    private UIElement CreateMenuHotkeySetting()
    {
        var setting = new FeatureSetting("Settings", "Menu Display Hotkey", "Press this key to show or hide the menu.", "ui_MenuHotkey", "key");
        var button = new Button { Content = GetKeyLabel(MenuPreferences.Hotkey), MinWidth = 112, Padding = new Thickness(12, 7, 12, 7), Cursor = System.Windows.Input.Cursors.Hand };
        button.SetResourceReference(BackgroundProperty, "RaisedBrush");
        button.SetResourceReference(ForegroundProperty, "TextBrush");
        button.SetResourceReference(BorderBrushProperty, "LineBrush");
        button.Click += (_, _) =>
        {
            _captureMenuHotkey = setting;
            _captureMenuHotkeyButton = button;
            button.Content = UiLanguage.Text("Press a key…", "请按下按键…");
            BeginKeyboardCapture();
        };
        return CreateSettingRow(setting, button);
    }

    private UIElement CreateSaveConfigSetting()
    {
        var controls = new StackPanel { Orientation = Orientation.Vertical, MinWidth = 390 };
        var nameRow = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 6) };
        _profileNameBox = CreateProfileTextBox(UiLanguage.Text("New profile name", "新配置名称"));
        var saveNew = CreateProfileButton(UiLanguage.Text("Save as new", "另存为"));
        saveNew.Click += async (_, _) => await SaveProfileAsync(GetEnteredProfileName(), true);
        var saveCustom = CreateProfileButton(UiLanguage.Text("Save", "保存"));
        saveCustom.Click += async (_, _) =>
        {
            var customName = GetEnteredProfileName();
            if (customName is not null) await SaveProfileAsync(customName, false);
            else if (_profileCombo?.SelectedItem is string selected) await SaveProfileAsync(selected, false);
            else SetProfileStatus(UiLanguage.Text("Enter a profile name first.", "请先输入配置名称。"), true);
        };
        nameRow.Children.Add(_profileNameBox);
        nameRow.Children.Add(saveCustom);
        nameRow.Children.Add(saveNew);
        controls.Children.Add(nameRow);

        var selectRow = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 6) };
        _profileCombo = new ComboBox { MinWidth = 190, Padding = new Thickness(8, 5, 8, 5), Margin = new Thickness(0, 0, 6, 0) };
        _profileCombo.SetResourceReference(BackgroundProperty, "RaisedBrush");
        _profileCombo.SetResourceReference(ForegroundProperty, "TextBrush");
        _profileCombo.SetResourceReference(BorderBrushProperty, "LineBrush");
        selectRow.Children.Add(_profileCombo);
        controls.Children.Add(selectRow);

        var actions = new StackPanel { Orientation = Orientation.Horizontal };
        var load = CreateProfileButton(UiLanguage.Text("Load", "载入"));
        load.Click += async (_, _) => await LoadSelectedProfileAsync();
        var deleteSelected = CreateProfileButton(UiLanguage.Text("Delete selected", "删除选中配置"));
        deleteSelected.Click += (_, _) => DeleteSelectedProfile();
        var deleteAll = CreateProfileButton(UiLanguage.Text("Delete all", "删除全部配置"));
        deleteAll.Click += (_, _) => DeleteAllProfiles();
        actions.Children.Add(load);
        actions.Children.Add(deleteSelected);
        actions.Children.Add(deleteAll);
        controls.Children.Add(actions);
        _profileStatus = new TextBlock { FontSize = 10, Margin = new Thickness(0, 5, 0, 0), TextWrapping = TextWrapping.Wrap };
        _profileStatus.SetResourceReference(TextBlock.ForegroundProperty, "MutedBrush");
        controls.Children.Add(_profileStatus);
        RefreshProfiles();
        return CreateSettingRow(new FeatureSetting("Settings", "Save Configuration", "Create, load or remove saved configuration profiles.", "ui_SaveConfig"), controls);
    }

    private TextBox CreateProfileTextBox(string placeholder)
    {
        var box = new TextBox { MinWidth = 190, Padding = new Thickness(8, 6, 8, 6), Margin = new Thickness(0, 0, 6, 0), Text = placeholder, ToolTip = placeholder };
        box.SetResourceReference(BackgroundProperty, "RaisedBrush");
        box.SetResourceReference(ForegroundProperty, "TextBrush");
        box.SetResourceReference(BorderBrushProperty, "LineBrush");
        box.PreviewMouseDown += (_, _) => BeginProfileTextInput();
        box.GotKeyboardFocus += (_, _) => BeginProfileTextInput();
        box.GotFocus += (_, _) => { if (box.Text == placeholder) box.Clear(); };
        box.LostKeyboardFocus += (_, _) => Dispatcher.BeginInvoke(new Action(() =>
        {
            if (!box.IsKeyboardFocusWithin) EndProfileTextInput();
        }), DispatcherPriority.Background);
        box.LostFocus += (_, _) => { if (string.IsNullOrWhiteSpace(box.Text)) box.Text = placeholder; };
        return box;
    }

    private void BeginProfileTextInput()
    {
        if (_profileInputActive) return;
        var handle = new WindowInteropHelper(this).Handle;
        if (handle == IntPtr.Zero) return;
        var style = GetWindowLongPtr(handle, GwlExStyle).ToInt64();
        SetWindowLongPtr(handle, GwlExStyle, new IntPtr(style & ~WsExNoActivate));
        _profileInputActive = true;
        Activate();
        Dispatcher.BeginInvoke(new Action(() => _profileNameBox?.Focus()), DispatcherPriority.Input);
    }

    private void EndProfileTextInput()
    {
        if (!_profileInputActive) return;
        var handle = new WindowInteropHelper(this).Handle;
        if (handle != IntPtr.Zero)
        {
            var style = GetWindowLongPtr(handle, GwlExStyle).ToInt64();
            SetWindowLongPtr(handle, GwlExStyle, new IntPtr(style | WsExNoActivate));
        }
        _profileInputActive = false;
    }

    private Button CreateProfileButton(string label)
    {
        var button = new Button { Content = label, Padding = new Thickness(9, 6, 9, 6), Margin = new Thickness(0, 0, 6, 0), Cursor = System.Windows.Input.Cursors.Hand };
        button.SetResourceReference(BackgroundProperty, "RaisedBrush");
        button.SetResourceReference(ForegroundProperty, "TextBrush");
        button.SetResourceReference(BorderBrushProperty, "LineBrush");
        return button;
    }

    private void RefreshProfiles(string? select = null)
    {
        if (_profileCombo is null) return;
        try
        {
            Directory.CreateDirectory(ProfileDirectory);
            var profiles = Directory.EnumerateFiles(ProfileDirectory, "*.json").Select(System.IO.Path.GetFileNameWithoutExtension).Where(name => !string.IsNullOrWhiteSpace(name)).OrderBy(name => name, StringComparer.CurrentCultureIgnoreCase).ToArray();
            _profileCombo.ItemsSource = profiles;
            _profileCombo.SelectedItem = select is not null && profiles.Contains(select) ? select : profiles.FirstOrDefault();
        }
        catch (Exception error) { SetProfileStatus(error.Message, true); }
    }

    private string? GetEnteredProfileName()
    {
        var raw = _profileNameBox?.Text.Trim();
        if (string.IsNullOrWhiteSpace(raw) || raw == UiLanguage.Text("New profile name", "新配置名称")) return null;
        return raw;
    }

    private async Task SaveProfileAsync(string? name, bool requireNew)
    {
        if (string.IsNullOrWhiteSpace(name) || name == UiLanguage.Text("New profile name", "新配置名称"))
        {
            SetProfileStatus(UiLanguage.Text("Enter a profile name first.", "请先输入配置名称。"), true);
            return;
        }
        name = string.Join("_", name.Split(System.IO.Path.GetInvalidFileNameChars(), StringSplitOptions.RemoveEmptyEntries)).Trim();
        if (name.Length == 0)
        {
            SetProfileStatus(UiLanguage.Text("Enter a valid profile name.", "请输入有效的配置名称。"), true);
            return;
        }
        try
        {
            var path = System.IO.Path.Combine(ProfileDirectory, name + ".json");
            if (requireNew && File.Exists(path))
            {
                SetProfileStatus(UiLanguage.Text("A profile with this name already exists.", "已存在同名配置文件。"), true);
                return;
            }
            var state = await _pipe.GetStateAsync();
            if (state is null || state.Count == 0) state = new Dictionary<string, object>(_nativeState);
            state.Remove("__status_map");
            if (state.Count == 0)
            {
                SetProfileStatus(UiLanguage.Text("No settings are available to save yet.", "当前没有可保存的配置数据。"), true);
                return;
            }
            state["__menuTheme"] = _selectedTheme;
            Directory.CreateDirectory(ProfileDirectory);
            File.WriteAllText(path, ProfileJson.Serialize(state));
            _ = _pipe.SetAsync("ui_SaveConfig", true);
            RefreshProfiles(name);
            EndProfileTextInput();
            if (_profileNameBox is not null) _profileNameBox.Text = UiLanguage.Text("New profile name", "新配置名称");
            SetProfileStatus(UiLanguage.Text("Configuration saved.", "配置已保存。"));
        }
        catch (Exception error) { SetProfileStatus(error.Message, true); }
    }

    private async Task LoadSelectedProfileAsync()
    {
        if (_profileCombo?.SelectedItem is not string name)
        {
            SetProfileStatus(UiLanguage.Text("No saved profiles found.", "没有可读取的配置文件。"), true);
            return;
        }
        try
        {
            var settings = ProfileJson.Deserialize<Dictionary<string, object>>(File.ReadAllText(System.IO.Path.Combine(ProfileDirectory, name + ".json")));
            if (settings is null) return;
            foreach (var setting in settings)
            {
                var key = setting.Key;
                var value = setting.Value;
                if (key == "__menuTheme") continue;
                if (await _pipe.SetAsync(key, value))
                    _nativeState[key] = value;
            }
            if (settings.TryGetValue("ui_MenuHotkey", out var hotkey))
                MenuPreferences.Save(hotkey: Convert.ToInt32(hotkey, CultureInfo.InvariantCulture));
            if (settings.TryGetValue("__menuTheme", out var theme)) ApplyTheme(Convert.ToInt32(theme, CultureInfo.InvariantCulture));
            await UpdateConnectionAsync();
            _pageCache.Clear();
            if (IsVisible) ShowPage(_page);
            SetProfileStatus(UiLanguage.Text("Configuration loaded.", "配置已载入。"));
        }
        catch (Exception error)
        {
            MessageBox.Show(error.Message, UiLanguage.Text("Configuration", "配置"), MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private void SetProfileStatus(string message, bool error = false)
    {
        if (_profileStatus is null) return;
        _profileStatus.Text = message;
        if (error) _profileStatus.Foreground = Brushes.IndianRed;
        else _profileStatus.SetResourceReference(TextBlock.ForegroundProperty, "MutedBrush");
    }

    private void DeleteSelectedProfile()
    {
        if (_profileCombo?.SelectedItem is not string name) return;
        File.Delete(System.IO.Path.Combine(ProfileDirectory, name + ".json"));
        RefreshProfiles();
    }

    private void DeleteAllProfiles()
    {
        if (!Directory.Exists(ProfileDirectory)) return;
        foreach (var file in Directory.EnumerateFiles(ProfileDirectory, "*.json")) File.Delete(file);
        RefreshProfiles();
    }

    // Bg / Panel / Raised / Accent / AccentAlt. Everything else the theme needs is
    // derived from these five, so a palette cannot drift out of sync with the
    // surface colours and every theme repaints the whole window.
    private static readonly (string Bg, string Panel, string Raised, string Accent, string AccentAlt)[] Themes =
    {
        ("#F2081118", "#E90E1D24", "#EB152B31", "#FF35D8C4", "#FF4AA8FF"),
        ("#F2061020", "#E90B1A2E", "#EB11304C", "#FF55D8FF", "#FF8A6CFF"),
        ("#F2171215", "#E921191E", "#EB2C2026", "#FFFF79AC", "#FFB48CFF"),
        ("#F2171311", "#E9211B18", "#EB2D221D", "#FFFF9A50", "#FFFF5E6C")
    };

    private void ApplyTheme(int theme)
    {
        // -1 means "nothing selected" (the ComboBox clears its selection while its
        // template is being applied), and there is no palette to apply then.
        if (theme < 0 || theme >= Themes.Length) return;
        _selectedTheme = theme;
        MenuPreferences.Save(theme: theme);
        var palette = Themes[theme];
        try
        {
            var bg = ParseColor(palette.Bg, Color.FromRgb(0x08, 0x11, 0x18));
            var panel = ParseColor(palette.Panel, Color.FromRgb(0x0E, 0x1D, 0x24));
            var raised = ParseColor(palette.Raised, Color.FromRgb(0x15, 0x2B, 0x31));
            var accent = ParseColor(palette.Accent, Color.FromRgb(0x35, 0xD8, 0xC4));
            var alt = ParseColor(palette.AccentAlt, Color.FromRgb(0x4A, 0xA8, 0xFF));
            var light = Color.FromRgb(0xF3, 0xF5, 0xF4);

            SetThemeBrush("BgBrush", bg);
            SetThemeBrush("PanelBrush", panel);
            SetThemeBrush("RaisedBrush", raised);
            SetThemeBrush("AccentBrush", accent);
            SetThemeBrush("AccentAltBrush", alt);

            // The window surface. These used to be hard-coded hex values in the
            // XAML, which is why switching a theme only recoloured the toggles and
            // left the panel, sidebar and glows untouched.
            SetThemeBrush("SidebarBrush", WithAlpha(Mix(bg, accent, 0.07), 0xF2));
            SetThemeBrush("LineBrush", WithAlpha(Mix(bg, accent, 0.45), 0xFF));
            SetThemeBrush("HoverBrush", WithAlpha(Mix(panel, accent, 0.17), 0xFF));
            SetThemeBrush("ActiveBrush", WithAlpha(Mix(panel, accent, 0.28), 0xFF));
            SetThemeBrush("DividerBrush", WithAlpha(Mix(panel, accent, 0.13), 0xFF));
            SetThemeBrush("TrackBrush", WithAlpha(Mix(bg, light, 0.20), 0xFF));
            SetThemeBrush("MetaBrush", WithAlpha(Mix(Color.FromRgb(0x89, 0x92, 0x8F), accent, 0.40), 0xFF));
            SetThemeBrush("SubtleBrush", WithAlpha(Mix(Color.FromRgb(0x89, 0x92, 0x8F), accent, 0.18), 0xFF));

            SetThemeGlow("GlowBrush", accent, 0.62);
            SetThemeGlow("GlowAltBrush", alt, 0.54);
            SetThemeScan("ScanBrush", accent, alt);

            _accent = accent;
            _cursorOverlay?.SetAccent(accent);
        }
        catch (System.Exception error)
        {
            // A palette must never be able to take the host process down.
            System.Diagnostics.Debug.WriteLine("Theme " + theme + " failed: " + error.Message);
        }
    }

    private static Color ParseColor(string hex, Color fallback)
        => ColorConverter.ConvertFromString(hex) is Color color ? color : fallback;

    private static Color WithAlpha(Color color, byte alpha) => Color.FromArgb(alpha, color.R, color.G, color.B);

    // Linear blend between two colours; alpha is discarded on purpose so callers
    // decide the opacity explicitly.
    private static Color Mix(Color from, Color to, double amount)
    {
        if (amount < 0) amount = 0;
        else if (amount > 1) amount = 1;
        return Color.FromRgb(
            (byte)(from.R + (to.R - from.R) * amount),
            (byte)(from.G + (to.G - from.G) * amount),
            (byte)(from.B + (to.B - from.B) * amount));
    }

    // Swaps the theme brush for a new one. It must never mutate the existing
    // brush: WPF freezes a brush as soon as a Style setter that references it is
    // sealed, and assigning Color on a frozen brush throws - which used to crash
    // the menu, and the whole game with it, as soon as a theme was picked. Every
    // theme brush is referenced through DynamicResource, so replacing the
    // dictionary entry repaints all consumers (sealed styles included).
    private void SetThemeBrush(string key, Color color)
    {
        var brush = new SolidColorBrush(color);
        brush.Freeze();
        PublishThemeResource(key, brush);
    }

    private void SetThemeGlow(string key, Color color, double peak)
    {
        var brush = new RadialGradientBrush();
        brush.GradientStops.Add(new GradientStop(WithAlpha(color, (byte)(peak * 255)), 0));
        brush.GradientStops.Add(new GradientStop(WithAlpha(color, (byte)(peak * 0.55 * 255)), 0.48));
        brush.GradientStops.Add(new GradientStop(WithAlpha(color, 0), 1));
        brush.Freeze();
        PublishThemeResource(key, brush);
    }

    private void SetThemeScan(string key, Color from, Color to)
    {
        var brush = new LinearGradientBrush { StartPoint = new Point(0, 0), EndPoint = new Point(1, 0) };
        brush.GradientStops.Add(new GradientStop(WithAlpha(from, 0), 0));
        brush.GradientStops.Add(new GradientStop(WithAlpha(from, 0xAA), 0.5));
        brush.GradientStops.Add(new GradientStop(WithAlpha(to, 0), 1));
        brush.Freeze();
        PublishThemeResource(key, brush);
    }

    private void PublishThemeResource(string key, Brush brush)
    {
        Resources[key] = brush;
        if (Application.Current != null) Application.Current.Resources[key] = brush;
    }

    private UIElement CreateSetting(FeatureSetting setting)
    {
        if (setting.Type == "action")
        {
            var action = new Button { Content = UiLanguage.Text("Run", "执行"), MinWidth = 112, Padding = new Thickness(12, 7, 12, 7), Cursor = System.Windows.Input.Cursors.Hand };
            action.SetResourceReference(BackgroundProperty, "RaisedBrush");
            action.SetResourceReference(ForegroundProperty, "TextBrush");
            action.SetResourceReference(BorderBrushProperty, "LineBrush");
            action.Click += async (_, _) => { action.IsEnabled = false; await SendAsync(setting, true, () => { }); action.IsEnabled = true; };
            return CreateSettingRow(setting, action);
        }
        if (setting.Type == "number") return CreateNumberSetting(setting);
        if (setting.Type == "key") return CreateKeySetting(setting);
        if (setting.Type == "text") return CreateTextSetting(setting);
        if (setting.Type == "choice") return CreateChoiceSetting(setting);
        if (setting.Type == "color") return CreateColorSetting(setting);

        var toggle = new CheckBox { VerticalAlignment = VerticalAlignment.Center, IsChecked = GetBoolState(setting.Key), ToolTip = UiLanguage.SettingDescription(setting) };
        bool suppress = false;
        toggle.Checked += async (_, _) => { if (!suppress) await SendAsync(setting, true, () => { suppress = true; toggle.IsChecked = GetBoolState(setting.Key); suppress = false; }); };
        toggle.Unchecked += async (_, _) => { if (!suppress) await SendAsync(setting, false, () => { suppress = true; toggle.IsChecked = GetBoolState(setting.Key); suppress = false; }); };
        return CreateSettingRow(setting, toggle);
    }

    private UIElement CreateNumberSetting(FeatureSetting setting)
    {
        double initial = Math.Min(setting.Maximum, Math.Max(setting.Minimum, GetNumberState(setting.Key, setting.Minimum)));
        var valueText = new TextBlock { Text = FormatValue(initial, setting.Step), Width = 46, TextAlignment = TextAlignment.Right, VerticalAlignment = VerticalAlignment.Center, FontSize = 12 };
        valueText.SetResourceReference(TextBlock.ForegroundProperty, "AccentBrush");
        // Kept deliberately compact: on the Aim page the settings column shares the
        // card with the model panel, and a wider control squeezed the label column
        // down to nothing (labels were clipped away entirely).
        var slider = new Slider { Minimum = setting.Minimum, Maximum = setting.Maximum, Value = initial, Width = 140, TickFrequency = setting.Step, IsSnapToTickEnabled = setting.Step >= 1, VerticalAlignment = VerticalAlignment.Center };
        var control = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        control.Children.Add(slider);
        control.Children.Add(valueText);
        var debounce = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(180) };
        slider.ValueChanged += (_, _) =>
        {
            valueText.Text = FormatValue(slider.Value, setting.Step);
            debounce.Stop();
            debounce.Start();
        };
        debounce.Tick += async (_, _) =>
        {
            debounce.Stop();
            var value = setting.Step >= 1 ? Math.Round(slider.Value) : slider.Value;
            await SendAsync(setting, value, () => slider.Value = GetNumberState(setting.Key, initial));
        };
        return CreateSettingRow(setting, control);
    }

    private UIElement CreateKeySetting(FeatureSetting setting)
    {
        var button = new Button { Content = GetKeyLabel(GetNumberState(setting.Key, 0)), MinWidth = 112, Padding = new Thickness(12, 7, 12, 7), Cursor = System.Windows.Input.Cursors.Hand };
        button.SetResourceReference(BackgroundProperty, "RaisedBrush");
        button.SetResourceReference(ForegroundProperty, "TextBrush");
        button.SetResourceReference(BorderBrushProperty, "LineBrush");
        button.Click += (_, _) => { _captureSetting = setting; _captureButton = button; button.Content = UiLanguage.Text("Press a key…", "请按下按键…"); BeginKeyboardCapture(); };
        return CreateSettingRow(setting, button);
    }

    private UIElement CreateTextSetting(FeatureSetting setting)
    {
        var textBox = new TextBox { Text = GetStringState(setting.Key), Width = 190, Padding = new Thickness(9, 6, 9, 6) };
        textBox.SetResourceReference(BackgroundProperty, "RaisedBrush");
        textBox.SetResourceReference(ForegroundProperty, "TextBrush");
        textBox.SetResourceReference(BorderBrushProperty, "LineBrush");
        textBox.LostFocus += async (_, _) => await SendAsync(setting, textBox.Text, () => textBox.Text = GetStringState(setting.Key));
        return CreateSettingRow(setting, textBox);
    }

    private UIElement CreateChoiceSetting(FeatureSetting setting)
    {
        var options = setting.Key switch
        {
            "cfg_AimbotTargetBone" => UiLanguage.Chinese
                ? new[] { "头部", "颈部", "胸部", "小腹", "腿部" }
                : new[] { "Head", "Neck", "Chest", "Abdomen", "Legs" },
            "cfg_AimbotBox" => UiLanguage.Chinese ? new[] { "方框", "四角框" } : new[] { "Box", "Corner Box" },
            "cfg_SelectedLanguage" => UiLanguage.Chinese ? new[] { "英语", "西班牙语", "葡萄牙语", "中文" } : new[] { "English", "Español", "Português", "中文" },
            "cfg_TeleportMethod" => UiLanguage.Chinese
                ? new[] { "智能（有目标传目标/无目标向前）", "仅选中目标", "向前穿透", "准星射线" }
                : new[] { "Smart (Target / Forward)", "Selected Target Only", "Forward Through Walls", "Crosshair Raycast" },
            _ => Array.Empty<string>()
        };
        var combo = new ComboBox
        {
            Width = 190,
            ItemsSource = options,
            SelectedIndex = Math.Max(0, Math.Min(options.Length - 1, (int)GetNumberState(setting.Key, 0))),
            Padding = new Thickness(8, 5, 8, 5)
        };
        combo.SelectionChanged += async (_, _) =>
        {
            if (combo.SelectedIndex >= 0)
            {
                if (setting.Key == "cfg_AimbotTargetBone")
                {
                    _selectedAimBone = combo.SelectedIndex;
                    UpdateAimRegionHighlight();
                }
                await SendAsync(setting, combo.SelectedIndex, () => combo.SelectedIndex = (int)GetNumberState(setting.Key, 0));
            }
        };
        return CreateSettingRow(setting, combo);
    }

    private UIElement CreateColorSetting(FeatureSetting setting)
    {
        var textBox = new TextBox
        {
            Text = GetStringState(setting.Key),
            Width = 112,
            MaxLength = 9,
            Padding = new Thickness(8, 6, 8, 6)
        };
        textBox.SetResourceReference(BackgroundProperty, "RaisedBrush");
        textBox.SetResourceReference(ForegroundProperty, "TextBrush");
        textBox.SetResourceReference(BorderBrushProperty, "LineBrush");
        var swatch = new Border
        {
            Width = 24,
            Height = 24,
            CornerRadius = new CornerRadius(5),
            BorderThickness = new Thickness(1),
            BorderBrush = (Brush)FindResource("MutedBrush"),
            Margin = new Thickness(8, 0, 0, 0),
            Background = TryParseUiColor(textBox.Text, out var initialColor)
                ? new SolidColorBrush(initialColor)
                : (Brush)FindResource("RaisedBrush")
        };
        textBox.TextChanged += (_, _) =>
        {
            if (TryParseUiColor(textBox.Text, out var color)) swatch.Background = new SolidColorBrush(color);
        };
        textBox.LostFocus += async (_, _) =>
        {
            if (TryParseUiColor(textBox.Text, out _))
                await SendAsync(setting, textBox.Text, () => textBox.Text = GetStringState(setting.Key));
            else
                textBox.Text = GetStringState(setting.Key);
        };
        var control = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
        control.Children.Add(textBox);
        control.Children.Add(swatch);
        return CreateSettingRow(setting, control);
    }

    private static bool TryParseUiColor(string value, out Color color)
    {
        color = Colors.Transparent;
        if (value.Length != 9 || value[0] != '#' || !uint.TryParse(value.Substring(1), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var rgba))
            return false;
        color = Color.FromArgb((byte)rgba, (byte)(rgba >> 24), (byte)(rgba >> 16), (byte)(rgba >> 8));
        return true;
    }

    private UIElement CreateSettingRow(FeatureSetting setting, FrameworkElement control)
    {
        var row = new Grid { Margin = new Thickness(0, 3, 0, 3) };
        row.ColumnDefinitions.Add(new ColumnDefinition());
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        var copy = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 7, 20, 7) };
        copy.Children.Add(new TextBlock { Text = setting.Key == "ui_language" ? UiLanguage.Text("Interface Language", "界面语言") : UiLanguage.SettingTitle(setting), FontSize = 13, FontWeight = FontWeights.Medium, TextTrimming = TextTrimming.CharacterEllipsis });
        var description = UiLanguage.SettingDescription(setting);
        copy.Children.Add(new TextBlock { Text = description, FontSize = 10, Foreground = (Brush)FindResource("MutedBrush"), Margin = new Thickness(0, 3, 0, 0), TextTrimming = TextTrimming.CharacterEllipsis });
        Grid.SetColumn(copy, 0); Grid.SetColumn(control, 1);
        row.Children.Add(copy); row.Children.Add(control);
        var divider = new Border { Height = 1, Margin = new Thickness(0, 4, 0, 4), Opacity = 0.7 };
        divider.SetResourceReference(BackgroundProperty, "DividerBrush");
        return new StackPanel { Children = { row, divider } };
    }

    private async Task SendAsync(FeatureSetting setting, object value, Action rollback)
    {
        if (await _pipe.SetAsync(setting.Key, value))
        {
            _nativeState[setting.Key] = value;
            SetConnection(true);
            return;
        }
        SetConnection(false);
        rollback();
    }

    private async void Window_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (_captureSetting is null && _captureMenuHotkey is null && KeyInterop.VirtualKeyFromKey(e.Key) == (MenuPreferences.Hotkey & HotkeyKeyMask))
        {
            HideMenu();
            e.Handled = true;
            return;
        }
        if (_captureMenuHotkey is not null && _captureMenuHotkeyButton is not null)
        {
            var menuKey = e.Key == Key.System ? e.SystemKey : e.Key;
            var vk = menuKey == Key.Escape ? VkEscape : KeyInterop.VirtualKeyFromKey(menuKey);
            if (vk == VkEscape) CancelMenuHotkeyCapture();
            else if (vk is not (VkControl or VkShift or VkMenu)) _ = CompleteMenuHotkeyCaptureAsync(vk | GetWpfModifierMask());
            e.Handled = true;
            return;
        }
        if (_captureSetting is null || _captureButton is null) return;
        var key = e.Key == Key.System ? e.SystemKey : e.Key;
        var vkKey = KeyInterop.VirtualKeyFromKey(key);
        if (key is Key.LeftCtrl or Key.RightCtrl or Key.LeftShift or Key.RightShift or Key.LeftAlt or Key.RightAlt) { e.Handled = true; return; }
        FinishKeyCapture(key == Key.Escape ? VkEscape : key is Key.Delete or Key.Back ? 0 : vkKey | GetWpfModifierMask());
        e.Handled = true;
    }

    private static int GetWpfModifierMask() => (Keyboard.Modifiers.HasFlag(ModifierKeys.Control) ? HotkeyCtrl : 0)
        | (Keyboard.Modifiers.HasFlag(ModifierKeys.Shift) ? HotkeyShift : 0)
        | (Keyboard.Modifiers.HasFlag(ModifierKeys.Alt) ? HotkeyAlt : 0);

    private void UpdateCapturePrompt(int modifiers)
    {
        var prompt = modifiers == 0 ? UiLanguage.Text("Press a key…", "请按下按键…") : GetModifierLabel(modifiers) + "+…";
        if (_captureButton is { } featureButton) featureButton.Content = prompt;
        if (_captureMenuHotkeyButton is { } menuButton) menuButton.Content = prompt;
    }

    private static string GetModifierLabel(int modifiers) => string.Join("+", new[]
    {
        (modifiers & HotkeyCtrl) != 0 ? "Ctrl" : null,
        (modifiers & HotkeyShift) != 0 ? "Shift" : null,
        (modifiers & HotkeyAlt) != 0 ? "Alt" : null
    }.Where(part => part != null));

    private void CancelMenuHotkeyCapture()
    {
        if (_captureMenuHotkeyButton is { } button) button.Content = GetKeyLabel(MenuPreferences.Hotkey);
        _captureMenuHotkey = null;
        _captureMenuHotkeyButton = null;
        EndKeyboardCapture();
    }

    private async Task CompleteMenuHotkeyCaptureAsync(int virtualKey)
    {
        var button = _captureMenuHotkeyButton;
        _captureMenuHotkey = null;
        _captureMenuHotkeyButton = null;
        EndKeyboardCapture();
        if (button is null) return;
        if (await _pipe.SetAsync("ui_MenuHotkey", virtualKey))
        {
            MenuPreferences.Save(hotkey: virtualKey);
            button.Content = GetKeyLabel(virtualKey);
        }
        else button.Content = GetKeyLabel(MenuPreferences.Hotkey);
    }

    private async void FinishKeyCapture(int virtualKey)
    {
        var setting = _captureSetting;
        var button = _captureButton;
        if (setting == null || button == null) return;
        EndKeyboardCapture();
        _captureSetting = null;
        _captureButton = null;
        if (virtualKey == VkEscape) button.Content = GetKeyLabel(GetNumberState(setting.Key, 0));
        else await CompleteKeyCaptureAsync(setting, button, virtualKey is VkBack or VkDelete ? 0 : virtualKey);
    }

    private async void Window_PreviewMouseDown(object sender, MouseButtonEventArgs e)
    {
        AnimateClickFeedback(e.OriginalSource as DependencyObject);
        if (_captureSetting is null || _captureButton is null) return;
        var virtualKey = e.ChangedButton switch
        {
            MouseButton.Left => 0x01,
            MouseButton.Right => 0x02,
            MouseButton.Middle => 0x04,
            MouseButton.XButton1 => 0x05,
            MouseButton.XButton2 => 0x06,
            _ => 0
        };
        if (virtualKey == 0) return;
        var setting = _captureSetting;
        var button = _captureButton;
        _captureSetting = null;
        _captureButton = null;
        var binding = virtualKey | GetWpfModifierMask();
        EndKeyboardCapture();
        await CompleteKeyCaptureAsync(setting, button, binding);
        e.Handled = true;
    }

    private static void AnimateClickFeedback(DependencyObject? source)
    {
        while (source is not null && source is not ButtonBase && source is not ComboBox && source is not CheckBox && source is not Slider && source is not TextBox)
            source = VisualTreeHelper.GetParent(source);
        if (source is not FrameworkElement target) return;
        target.RenderTransformOrigin = new Point(0.5, 0.5);
        if (target.RenderTransform is not ScaleTransform scale)
        {
            scale = new ScaleTransform(1, 1);
            target.RenderTransform = scale;
        }
        var animation = new DoubleAnimationUsingKeyFrames { Duration = TimeSpan.FromMilliseconds(190) };
        animation.KeyFrames.Add(new EasingDoubleKeyFrame(0.95, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(55)), new CubicEase { EasingMode = EasingMode.EaseOut }));
        animation.KeyFrames.Add(new EasingDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(190)), new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.18 }));
        scale.BeginAnimation(ScaleTransform.ScaleXProperty, animation);
        scale.BeginAnimation(ScaleTransform.ScaleYProperty, animation.Clone());
    }

    private async Task CompleteKeyCaptureAsync(FeatureSetting setting, Button button, int virtualKey)
    {
        button.Content = GetKeyLabel(virtualKey);
        await SendAsync(setting, virtualKey, () => button.Content = GetKeyLabel(GetNumberState(setting.Key, 0)));
    }

    private bool GetBoolState(string key) => _nativeState.TryGetValue(key, out var value) && value is bool enabled && enabled;
    private double GetNumberState(string key, double fallback) => _nativeState.TryGetValue(key, out var value) && value is IConvertible ? Convert.ToDouble(value, CultureInfo.InvariantCulture) : fallback;
    private string GetStringState(string key) => _nativeState.TryGetValue(key, out var value) ? Convert.ToString(value, CultureInfo.InvariantCulture) ?? "" : "";
    private static string FormatValue(double value, double step) => step < 1 ? value.ToString("0.##") : Math.Round(value).ToString("0");
    private static string GetKeyLabel(double code)
    {
        if (code <= 0) return UiLanguage.Text("Unbound", "未绑定");
        if (code == 1) return UiLanguage.Text("Mouse Left", "鼠标左键");
        if (code == 2) return UiLanguage.Text("Mouse Right", "鼠标右键");
        if (code == 4) return UiLanguage.Text("Mouse Middle", "鼠标中键");
        if (code == 5) return UiLanguage.Text("Mouse X1", "鼠标侧键 1");
        if (code == 6) return UiLanguage.Text("Mouse X2", "鼠标侧键 2");
        var binding = (int)code;
        var keyCode = binding & HotkeyKeyMask;
        var key = KeyInterop.KeyFromVirtualKey(keyCode);
        var label = keyCode switch
        {
            1 => UiLanguage.Text("Mouse Left", "鼠标左键"),
            2 => UiLanguage.Text("Mouse Right", "鼠标右键"),
            4 => UiLanguage.Text("Mouse Middle", "鼠标中键"),
            5 => UiLanguage.Text("Mouse X1", "鼠标侧键 1"),
            6 => UiLanguage.Text("Mouse X2", "鼠标侧键 2"),
            _ => key == Key.None ? $"VK {keyCode}" : key.ToString()
        };
        var prefix = GetModifierLabel(binding);
        return prefix.Length == 0 ? label : $"{prefix}+{label}";
    }

    private bool _connectionUpdateInProgress;

    private async Task UpdateConnectionAsync()
    {
        if (_connectionUpdateInProgress) return;
        _connectionUpdateInProgress = true;
        try
        {
        var state = await _pipe.GetStateAsync();
        var justConnected = state is not null && !_wasConnected;
        SetConnection(state is not null);
        if (state is null) return;
        _nativeState = state;
        UpdateMapStatus();
        if (justConnected)
        {
            _pageCache.Clear();
            _mapStatusCards.Clear();
            _mapStatusTexts.Clear();
            _mapStatusConnections.Clear();
            if (IsVisible) ShowPage(_page);
        }
        }
        finally { _connectionUpdateInProgress = false; }
    }

    private void SetConnection(bool connected)
    {
        ConnectionDot.Fill = (Brush)FindResource(connected ? "AccentBrush" : "MutedBrush");
        ConnectionText.Text = connected ? UiLanguage.Text("Game link connected", "游戏连接正常") : UiLanguage.Text("Waiting for game link", "等待游戏连接");
        UpdateMapStatus();
        _wasConnected = connected;
    }

    private void UpdateMapStatus()
    {
        var map = _nativeState.TryGetValue("__status_map", out var rawMap) ? Convert.ToString(rawMap, CultureInfo.InvariantCulture) : null;
        for (var i = 0; i < _mapStatusCards.Count; i++)
        {
            _mapStatusCards[i].Visibility = GetBoolState("bShowWatermark") ? Visibility.Visible : Visibility.Collapsed;
            _mapStatusTexts[i].Text = UiLanguage.Text("Current map", "当前地图") + "  ·  " + (string.IsNullOrWhiteSpace(map) ? UiLanguage.Text("Waiting for game...", "等待游戏数据…") : map);
            _mapStatusConnections[i].Text = _wasConnected ? UiLanguage.Text("CONNECTED", "已连接") : UiLanguage.Text("OFFLINE", "未连接");
            _mapStatusConnections[i].Foreground = (Brush)FindResource(_wasConnected ? "AccentBrush" : "MutedBrush");
        }
    }

    private void Minimize_Click(object sender, RoutedEventArgs e)
    {
        // Hiding is used instead of WindowState.Minimized: minimising a window
        // still runs through the window manager's activation logic, while a
        // hidden WS_EX_NOACTIVATE window leaves the game untouched.
        HideMenu();
    }

    private void Close_Click(object sender, RoutedEventArgs e) => HideMenu();
}
