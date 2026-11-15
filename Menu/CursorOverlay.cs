using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Effects;
using System.Windows.Shapes;
using System.Windows.Threading;

namespace RonMenu;

/// <summary>
/// Full-screen, click-through window that draws the menu's own cursor.
///
/// The menu window itself only covers the panel, so a cursor drawn inside it gets
/// clipped at the panel edge - which is what made the pointer appear to stick to
/// the last position inside the menu. This overlay spans the whole virtual screen
/// instead, so the menu cursor can follow the pointer anywhere while the game
/// never sees it.
///
/// WS_EX_TRANSPARENT makes every click pass straight through to the panel or the
/// game underneath, and WS_EX_NOACTIVATE keeps the game in the foreground.
/// </summary>
internal sealed class CursorOverlay : Window
{
    private const int GwlExStyle = -20;
    private const long WsExNoActivate = 0x08000000L;
    private const long WsExToolWindow = 0x00000080L;
    private const long WsExTransparent = 0x00000020L;
    private const uint SwpNoActivate = 0x0010;
    private const uint SwpNoMove = 0x0002;
    private const uint SwpNoSize = 0x0001;
    private static readonly IntPtr HwndTopmost = new(-1);

    [StructLayout(LayoutKind.Sequential)]
    private struct NativePoint { public int X, Y; }

    [DllImport("user32.dll")]
    private static extern bool GetCursorPos(out NativePoint point);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW", SetLastError = true)]
    private static extern IntPtr GetWindowLongPtr(IntPtr hwnd, int index);

    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW", SetLastError = true)]
    private static extern IntPtr SetWindowLongPtr(IntPtr hwnd, int index, IntPtr value);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool SetWindowPos(IntPtr hwnd, IntPtr insertAfter, int x, int y, int cx, int cy, uint flags);

    private readonly Canvas _canvas;
    private readonly Path _glyph;
    private readonly DropShadowEffect _glyphGlow;
    private readonly DispatcherTimer _timer;

    public CursorOverlay()
    {
        WindowStyle = WindowStyle.None;
        AllowsTransparency = true;
        Background = Brushes.Transparent;
        ShowInTaskbar = false;
        ShowActivated = false;
        ResizeMode = ResizeMode.NoResize;
        WindowStartupLocation = WindowStartupLocation.Manual;
        Focusable = false;
        Cursor = Cursors.None;
        ForceCursor = true;
        Left = SystemParameters.VirtualScreenLeft;
        Top = SystemParameters.VirtualScreenTop;
        Width = SystemParameters.VirtualScreenWidth;
        Height = SystemParameters.VirtualScreenHeight;

        _glyphGlow = new DropShadowEffect
        {
            Color = Color.FromRgb(0x35, 0xD8, 0xC4),
            BlurRadius = 8,
            ShadowDepth = 0,
            Opacity = 0.85
        };

        _glyph = new Path
        {
            Data = Geometry.Parse("M 1,1 L 1,22 L 6.5,16 L 10.5,25 L 14.5,23 L 10.5,14 L 19,14 Z"),
            Fill = new SolidColorBrush(Color.FromRgb(0x35, 0xD8, 0xC4)),
            Stroke = new SolidColorBrush(Color.FromRgb(0xE8, 0xFF, 0xFF)),
            StrokeThickness = 1.2,
            StrokeLineJoin = PenLineJoin.Round,
            Effect = _glyphGlow
        };

        _canvas = new Canvas { IsHitTestVisible = false, ClipToBounds = false };
        _canvas.Children.Add(_glyph);
        Content = _canvas;

        SourceInitialized += (_, _) => ApplyOverlayStyles();

        // Polled rather than driven by MouseMove: the overlay is click-through and
        // never receives mouse messages, and polling keeps the glyph glued to the
        // real pointer even when it moves over the game area.
        // 16 ms (one frame at 60 Hz) is plenty - the previous 8 ms doubled the
        // number of wake-ups per second without making the pointer look smoother,
        // and every tick dirties a full-screen layered window.
        _timer = new DispatcherTimer(DispatcherPriority.Render) { Interval = TimeSpan.FromMilliseconds(16) };
        _timer.Tick += (_, _) => OnTick();
    }

    private int _ticks;
    private int _lastX = int.MinValue;
    private int _lastY = int.MinValue;

    private void OnTick()
    {
        Follow();
        // Re-assert the topmost position roughly every 250 ms. The menu panel
        // re-orders itself whenever it re-centres or resizes, and a single
        // HWND_TOPMOST call from anything else is enough to bury the glyph under
        // the panel - which leaves the cursor invisible inside the menu and still
        // visible over the game.
        if (++_ticks % 16 == 0) BringAbove();
    }

    /// <summary>Repaints the glyph with the active theme accent.</summary>
    public void SetAccent(Color accent)
    {
        _glyph.Fill = new SolidColorBrush(accent);
        _glyphGlow.Color = accent;
    }

    public void ShowOverlay()
    {
        // Virtual-screen bounds can change (resolution or monitor changes), and
        // the window is reused across toggles, so re-apply them every time.
        Left = SystemParameters.VirtualScreenLeft;
        Top = SystemParameters.VirtualScreenTop;
        Width = SystemParameters.VirtualScreenWidth;
        Height = SystemParameters.VirtualScreenHeight;

        if (!IsVisible) Show();
        // The window may have moved (resolution / monitor change), so the cached
        // screen position can no longer be trusted.
        _lastX = int.MinValue;
        _lastY = int.MinValue;
        BringAbove();
        Follow();
        if (!_timer.IsEnabled) _timer.Start();
    }

    public void HideOverlay()
    {
        _timer.Stop();
        Hide();
    }

    public void BringAbove()
    {
        var handle = new WindowInteropHelper(this).Handle;
        if (handle == IntPtr.Zero) return;
        SetWindowPos(handle, HwndTopmost, 0, 0, 0, 0, SwpNoMove | SwpNoSize | SwpNoActivate);
    }

    private void ApplyOverlayStyles()
    {
        var handle = new WindowInteropHelper(this).Handle;
        if (handle == IntPtr.Zero) return;
        var style = GetWindowLongPtr(handle, GwlExStyle).ToInt64();
        SetWindowLongPtr(handle, GwlExStyle,
            new IntPtr(style | WsExNoActivate | WsExToolWindow | WsExTransparent));
        SetWindowPos(handle, HwndTopmost, 0, 0, 0, 0, SwpNoMove | SwpNoSize | SwpNoActivate);
    }

    private void Follow()
    {
        if (!GetCursorPos(out var point)) return;
        // Skip the move when the pointer has not actually moved: every SetLeft /
        // SetTop dirties a full-screen layered window, and a stationary pointer
        // would otherwise repaint the whole overlay 60 times a second.
        if (point.X == _lastX && point.Y == _lastY) return;
        _lastX = point.X;
        _lastY = point.Y;
        var local = PointFromScreen(new Point(point.X, point.Y));
        Canvas.SetLeft(_glyph, local.X);
        Canvas.SetTop(_glyph, local.Y);
    }
}
