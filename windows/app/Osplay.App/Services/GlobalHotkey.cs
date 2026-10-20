using System.Runtime.InteropServices;
using System.Windows.Interop;
using Osplay.App.Settings;

namespace Osplay.App.Services;

/// <summary>A system-wide shortcut, received by a hidden message window on the UI thread.</summary>
internal sealed partial class GlobalHotkey : IDisposable
{
    private const int _hotkeyMessage = 0x0312; // WM_HOTKEY
    private const uint _noRepeat = 0x4000;     // MOD_NOREPEAT
    private static readonly nint _messageOnlyParent = -3; // HWND_MESSAGE
    private static int _lastId;

    private readonly HwndSource _window;
    private readonly int _id;
    private readonly Action _pressed;

    private GlobalHotkey(HwndSource window, int id, Action pressed)
    {
        _window = window;
        _id = id;
        _pressed = pressed;
        _window.AddHook(OnMessage);
    }

    /// <summary>Null when another program already owns the combination.</summary>
    public static GlobalHotkey? Register(HotkeyGesture gesture, Action pressed)
    {
        var window = new HwndSource(new HwndSourceParameters("Osplay hotkey") { ParentWindow = _messageOnlyParent });
        int id = Interlocked.Increment(ref _lastId);
        if (!RegisterHotKey(window.Handle, id, gesture.Modifiers | _noRepeat, gesture.VirtualKey))
        {
            window.Dispose();
            return null;
        }
        return new GlobalHotkey(window, id, pressed);
    }

    public void Dispose()
    {
        _ = UnregisterHotKey(_window.Handle, _id);
        _window.RemoveHook(OnMessage);
        _window.Dispose();
    }

    private nint OnMessage(nint hwnd, int message, nint wParam, nint lParam, ref bool handled)
    {
        if (message == _hotkeyMessage && wParam == _id)
        {
            handled = true;
            _pressed();
        }
        return 0;
    }

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool RegisterHotKey(nint window, int id, uint modifiers, uint virtualKey);

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool UnregisterHotKey(nint window, int id);
}
