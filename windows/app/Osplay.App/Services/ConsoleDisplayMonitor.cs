using System.Runtime.InteropServices;
using System.Windows.Interop;

namespace Osplay.App.Services;

/// <summary>
/// Reports when the PC's display turns off and on. Modern Standby (S0 low-power idle) sends no
/// suspend notification and keeps the network up, so for Osplay the display going off is the
/// moment the PC goes to sleep: no more frames come, and the other computer should know.
/// </summary>
internal sealed partial class ConsoleDisplayMonitor : IDisposable
{
    private const int _powerBroadcast = 0x0218;       // WM_POWERBROADCAST
    private const nint _powerSettingChange = 0x8013;  // PBT_POWERSETTINGCHANGE
    private const int _notifyWindowHandle = 0;         // DEVICE_NOTIFY_WINDOW_HANDLE
    private const uint _displayOff = 0;
    private static readonly nint _messageOnlyParent = -3; // HWND_MESSAGE

    /// <summary>GUID_CONSOLE_DISPLAY_STATE: 0 off, 1 on, 2 dimmed.</summary>
    private static readonly Guid _consoleDisplayState = new("6FE69556-704A-47A0-8F24-C28D936FDA47");

    private readonly HwndSource _window;
    private readonly nint _registration;
    private bool? _on;

    public ConsoleDisplayMonitor()
    {
        _window = new HwndSource(new HwndSourceParameters("Osplay display state") { ParentWindow = _messageOnlyParent });
        _window.AddHook(OnMessage);
        Guid setting = _consoleDisplayState;
        _registration = RegisterPowerSettingNotification(_window.Handle, ref setting, _notifyWindowHandle);
    }

    /// <summary>The display turned off (true: it is off now) or back on (false). Raised on the UI thread.</summary>
    public event Action<bool>? DisplayOffChanged;

    public void Dispose()
    {
        if (_registration != 0)
        {
            _ = UnregisterPowerSettingNotification(_registration);
        }
        _window.RemoveHook(OnMessage);
        _window.Dispose();
    }

    private unsafe nint OnMessage(nint hwnd, int message, nint wParam, nint lParam, ref bool handled)
    {
        if (message != _powerBroadcast || wParam != _powerSettingChange || lParam == 0)
        {
            return 0;
        }
        var setting = (PowerBroadcastSetting*)lParam;
        if (setting->PowerSetting != _consoleDisplayState || setting->DataLength < sizeof(uint))
        {
            return 0;
        }
        // Dimmed still shows the picture; only "off" counts.
        bool on = setting->Data != _displayOff;
        bool? previous = _on;
        _on = on;
        // The first notification only reports the state at registration.
        if (previous is not null && previous != on)
        {
            DisplayOffChanged?.Invoke(!on);
        }
        return 0;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct PowerBroadcastSetting
    {
        public Guid PowerSetting;
        public uint DataLength;
        public uint Data; // the first four bytes of the data; display states fit
    }

    [LibraryImport("user32.dll", SetLastError = true)]
    private static partial nint RegisterPowerSettingNotification(nint recipient, ref Guid powerSetting, int flags);

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool UnregisterPowerSettingNotification(nint handle);
}
