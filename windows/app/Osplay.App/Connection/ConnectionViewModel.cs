using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.Net.NetworkInformation;
using System.Windows.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Osplay.App.Diagnostics;
using Osplay.App.Engine;
using Osplay.App.Interop;
using Osplay.App.Services;
using Osplay.App.Settings;

namespace Osplay.App.Connection;

/// <summary>
/// Sessions, discovery, pairing and devices: share this PC's screen or use it as a display. Runs
/// on the UI thread; every engine call goes through <see cref="Native"/>, so nothing here waits
/// for native code. One operation (mode change, connect) runs at a time.
/// </summary>
public sealed partial class ConnectionViewModel : ObservableObject, IDisposable
{
    private readonly Dispatcher _dispatcher;
    private readonly IPairingPrompt _pairing;
    private readonly AppSettings _settings;
    private readonly string _deviceName = System.Net.Dns.GetHostName();
    private readonly Dictionary<string, DeviceInfo> _found = [];
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly HashSet<string> _pairedNames = new(StringComparer.CurrentCultureIgnoreCase);
    private EngineBrowser? _browser;
    private EngineHost? _host;
    private EngineAdvertiser? _advertiser;
    private EngineViewer? _viewer;
    private string _peerName = string.Empty;
    private IReadOnlyList<StreamFigures> _figures = [];
    private PendingSwap? _pendingSwap;
    private string? _autoConnectDeviceId;
    private CancellationTokenSource? _autoConnectTimeout;
    private GlobalHotkey? _swapHotkey;
    private GlobalHotkey? _statsHotkey;
    private ushort _sharePort;
    private readonly ConsoleDisplayMonitor _display = new();
    private ViewedHost? _viewedHost;
    /// <summary>The user ended the session; it does not come back by itself.</summary>
    private bool _endedByUser;
    /// <summary>This PC closed the session itself (sleep, monitor change) and resumes it afterwards.</summary>
    private bool _resumesAfterClose;

    /// <summary>The host being viewed, so the session can come back after an interruption.</summary>
    private sealed record ViewedHost(string DeviceId, string Name);

    /// <summary>After an accepted role swap: the role this PC takes once the session has closed.</summary>
    private sealed record PendingSwap(ConnectionMode Role, string PeerDeviceId, string PeerName);

    internal ConnectionViewModel(Dispatcher dispatcher, IPairingPrompt pairing, AppSettings settings)
    {
        _dispatcher = dispatcher;
        _pairing = pairing;
        _settings = settings;
        _settings.PropertyChanged += OnSettingChanged;
        Microsoft.Win32.SystemEvents.PowerModeChanged += OnPowerModeChanged;
        // Modern Standby sends no suspend; the display turning off is the moment the PC sleeps.
        _display.DisplayOffChanged += off => _ = off ? SuspendForSleepAsync() : RefreshAfterWakeAsync();
        NetworkChange.NetworkAddressChanged += (_, _) => _dispatcher.BeginInvoke(RefreshNetworkAsync);
    }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsOff), nameof(IsSharing), nameof(IsViewing))]
    private ConnectionMode _mode = ConnectionMode.Off;

    [ObservableProperty]
    private string _status = Localization.Strings.ConnectionStatusOff;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasError), nameof(IsErrorOpen))]
    private string? _errorMessage;

    [ObservableProperty]
    private bool _isSessionActive;

    /// <summary>Viewer streams are on screen (the hotkey or the tray hides them).</summary>
    [ObservableProperty]
    private bool _isViewerVisible = true;

    [ObservableProperty]
    private bool _isBusy;

    /// <summary>Frames flow: the roles can be swapped.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(SwapRolesCommand))]
    private bool _isStreaming;

    /// <summary>The swap shortcut is owned by another program; the button still works.</summary>
    [ObservableProperty]
    private bool _isSwapHotkeyTaken;

    /// <summary>False when the engine library could not be loaded; everything is disabled.</summary>
    [ObservableProperty]
    private bool _isEngineAvailable = true;

    /// <summary>Why this PC cannot share its screen right now, or null.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasHostProblem), nameof(HostProblemMessage), nameof(HealthStatusText))]
    private HostHealth? _hostProblem;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ShowsNetworkWarning))]
    private bool _isNetworkPublic;

    /// <summary>
    /// The panel's hint about a Public network, until the user closes it once. Public networks often
    /// work (the firewall rule may allow Osplay), so it only suggests what to try when nothing connects.
    /// </summary>
    public bool ShowsNetworkWarning
    {
        get => IsNetworkPublic && !_settings.HidesNetworkWarning;
        set
        {
            if (!value)
            {
                _settings.HidesNetworkWarning = true;
            }
        }
    }

    /// <summary>A viewer session started: the panel gets out of the way of the new display.</summary>
    public event Action? ViewerStarted;

    public bool IsOff => Mode == ConnectionMode.Off;

    public bool IsSharing => Mode == ConnectionMode.Sharing;

    public bool IsViewing => Mode == ConnectionMode.Viewing;

    public bool HasError => ErrorMessage is not null;

    /// <summary>The error banner; closing it clears the error.</summary>
    public bool IsErrorOpen
    {
        get => HasError;
        set
        {
            if (!value)
            {
                ErrorMessage = null;
            }
        }
    }

    public bool HasHostProblem => HostProblem is not null;

    public string HostProblemMessage => HostProblem switch
    {
        HostHealth.HelperMissing => Localization.Strings.HealthHelperMissing,
        HostHealth.HelperRefused => Localization.Strings.HealthHelperRefused,
        HostHealth.DriverMissing => Localization.Strings.HealthDriverMissing,
        _ => string.Empty,
    };

    /// <summary>Settings › Advanced: the problem, or that sharing is ready.</summary>
    public string HealthStatusText => HostProblem is null ? Localization.Strings.HealthReady : HostProblemMessage;

    /// <summary>"Ctrl+Alt+Shift+S swaps the roles…", or that another program owns the shortcut.</summary>
    public string SwapHint => LocalizedText.Format(
        IsSwapHotkeyTaken ? Localization.Strings.SwapHotkeyTaken : Localization.Strings.SwapHint,
        _settings.SwapHotkey.DisplayText);

    partial void OnIsSwapHotkeyTakenChanged(bool value) => OnPropertyChanged(nameof(SwapHint));

    /// <summary>The viewer hotkey, for hints ("Ctrl+Alt+Shift+V hides and shows the display").</summary>
    public string HotkeyHint => LocalizedText.Format(Localization.Strings.ViewerHotkeyHint, _settings.ViewerHotkey.DisplayText);

    /// <summary>Hosts to connect to.</summary>
    public ObservableCollection<DeviceItem> Devices { get; } = [];

    public ObservableCollection<PeerItem> Peers { get; } = [];

    /// <summary>This PC's monitors; the selected ones are offered to hosts.</summary>
    public ObservableCollection<MonitorTile> Monitors { get; } = [];

    public void Dispose()
    {
        Microsoft.Win32.SystemEvents.PowerModeChanged -= OnPowerModeChanged;
        _display.Dispose();
        _statsHotkey?.Dispose();
        _gate.Dispose();
        _autoConnectTimeout?.Dispose();
        _swapHotkey?.Dispose();
    }

    /// <summary>Loads the engine state; called once after start.</summary>
    public async Task InitializeAsync()
    {
        IsEngineAvailable = await Native.IsAvailableAsync().ConfigureAwait(true);
        if (!IsEngineAvailable)
        {
            ErrorMessage = Localization.Strings.ErrorEngineMissing;
            return;
        }
        await RefreshPeersAsync().ConfigureAwait(true);
        await RefreshMonitorsAsync().ConfigureAwait(true);
        await CheckHealthAsync().ConfigureAwait(true);
        await RefreshNetworkAsync().ConfigureAwait(true);
    }

    /// <summary>Ends every session and withdraws the Bonjour service; called before quitting.</summary>
    public async Task ShutdownAsync()
    {
        await _gate.WaitAsync().ConfigureAwait(true);
        try
        {
            await StopAllAsync().ConfigureAwait(true);
            _pairing.Close();
        }
        finally
        {
            _ = _gate.Release();
        }
    }

    /// <summary>Asks the helper whether sharing can work; skipped while a host session holds it.</summary>
    [RelayCommand]
    public async Task CheckHealthAsync()
    {
        if (!IsEngineAvailable || _host is not null)
        {
            return;
        }
        HostHealth health = await EngineHealth.CheckAsync().ConfigureAwait(true);
        HostProblem = health is HostHealth.Ready or HostHealth.InUse ? null : health;
    }

    [RelayCommand]
    private Task SetModeAsync(ConnectionMode mode) =>
        // Selecting the active role again turns it off, like the Mac's role cards.
        SwitchModeAsync(mode == Mode ? ConnectionMode.Off : mode);

    private async Task SwitchModeAsync(ConnectionMode target)
    {
        await _gate.WaitAsync().ConfigureAwait(true);
        IsBusy = true;
        try
        {
            await StopAllAsync().ConfigureAwait(true);
            Mode = target;
            ErrorMessage = null;
            switch (target)
            {
                case ConnectionMode.Off:
                    Status = Localization.Strings.ConnectionStatusOff;
                    await CheckHealthAsync().ConfigureAwait(true);
                    break;
                case ConnectionMode.Sharing:
                    await StartSharingAsync().ConfigureAwait(true);
                    break;
                case ConnectionMode.Viewing:
                    await RefreshMonitorsAsync().ConfigureAwait(true);
                    await StartBrowsingAsync().ConfigureAwait(true);
                    break;
            }
        }
        finally
        {
            IsBusy = false;
            _ = _gate.Release();
        }
    }

    /// <summary>
    /// Reverses the roles with the connected computer: this PC hosts if it was the display, and
    /// becomes the display if it was hosting. The other computer answers at once.
    /// </summary>
    [RelayCommand(CanExecute = nameof(IsStreaming))]
    private async Task SwapRolesAsync()
    {
        EngineSession? session = (EngineSession?)_host ?? _viewer;
        if (session is null || _pendingSwap is not null)
        {
            return;
        }
        EngineResult result = await session.RequestRoleSwapAsync().ConfigureAwait(true);
        if (result != EngineResult.Ok)
        {
            ErrorMessage = result == EngineResult.PeerTooOld ? Localization.Strings.ErrorSwapUnsupported : ErrorMessages.For(result);
        }
    }

    [RelayCommand]
    private async Task DisconnectAsync()
    {
        _endedByUser = true;
        if (_host is not null)
        {
            await _host.StopAsync().ConfigureAwait(true);
        }
        if (_viewer is not null)
        {
            await _viewer.StopAsync().ConfigureAwait(true);
        }
    }

    [RelayCommand]
    private async Task ToggleViewerAsync()
    {
        if (_viewer is not null)
        {
            await _viewer.SetVisibleAsync(!IsViewerVisible).ConfigureAwait(true);
        }
    }

    [RelayCommand]
    private Task RefreshMonitorsAsync() => LoadMonitorsAsync();

    // --- Sharing ------------------------------------------------------------------------------

    private async Task StartSharingAsync()
    {
        (EngineResult started, EngineHost? host) = await EngineHost
            .StartAsync(_dispatcher, _deviceName, _settings.Quality.MillibitsPerPixel(), _sharePort)
            .ConfigureAwait(true);
        if (host is null)
        {
            HostProblem = EngineHealth.FromFailure(started) ?? HostProblem;
            Fail(ErrorMessages.For(started));
            return;
        }
        HostProblem = null;
        // The next session listens here again, so the advertisement viewers resolved stays right.
        _sharePort = host.ControlPort;
        (EngineResult advertised, EngineAdvertiser? advertiser) = await EngineAdvertiser
            .StartAsync(_deviceName, Roles.Host, host.ControlPort)
            .ConfigureAwait(true);
        if (advertiser is null)
        {
            await host.DisposeAsync().ConfigureAwait(true);
            Fail(ErrorMessages.For(advertised));
            return;
        }
        _host = host;
        _advertiser = advertiser;
        Attach(host);
        Status = Localization.Strings.ConnectionStatusWaiting;
    }

    // --- Viewing ------------------------------------------------------------------------------

    private async Task StartBrowsingAsync()
    {
        (EngineResult started, EngineBrowser? browser) = await EngineBrowser.StartAsync(_dispatcher).ConfigureAwait(true);
        if (browser is null)
        {
            Fail(ErrorMessages.For(started));
            return;
        }
        _browser = browser;
        browser.Found += OnFound;
        browser.Lost += OnLost;
        Status = Localization.Strings.ConnectionStatusSearching;
    }

    private void OnFound(DeviceInfo device)
    {
        if (!device.Roles.HasFlag(Roles.Host))
        {
            return;
        }
        _found[device.Label] = device;
        RebuildDevices();
        if (device.DeviceId == _autoConnectDeviceId)
        {
            _autoConnectDeviceId = null;
            _autoConnectTimeout?.Cancel();
            _ = ConnectAsync(device);
        }
    }

    private void OnLost(string label)
    {
        _ = _found.Remove(label);
        RebuildDevices();
    }

    private void RebuildDevices()
    {
        Devices.Clear();
        foreach (DeviceInfo device in _found.Values.OrderBy(d => d.Name, StringComparer.CurrentCultureIgnoreCase))
        {
            string platform = device.Platform == Platform.MacOS ? Localization.Strings.PlatformMac : Localization.Strings.PlatformWindows;
            string state = !device.IsCompatible
                ? Localization.Strings.DeviceIncompatible
                : _pairedNames.Contains(device.Name) ? Localization.Strings.DevicePaired : Localization.Strings.DeviceNew;
            Devices.Add(new DeviceItem(
                device.Name, $"{platform} · {state}", device.IsCompatible,
                new AsyncRelayCommand(() => ConnectAsync(device))));
        }
    }

    private async Task ConnectAsync(DeviceInfo device)
    {
        if (!device.IsCompatible)
        {
            ErrorMessage = Localization.Strings.ErrorIncompatible;
            return;
        }
        await _gate.WaitAsync().ConfigureAwait(true);
        IsBusy = true;
        try
        {
            if (_viewer is not null || Mode != ConnectionMode.Viewing)
            {
                return;
            }
            byte[] offered = [.. Monitors.Where(m => m.IsSelected).Select(m => m.Id)];
            if (Monitors.Count > 0 && offered.Length == 0)
            {
                ErrorMessage = Localization.Strings.ErrorNoMonitorSelected;
                return;
            }
            ErrorMessage = null;
            HotkeyGesture hotkey = _settings.ViewerHotkey;
            var options = new ViewerOptions(_deviceName, device.Address, device.Port, offered, hotkey.Modifiers, hotkey.VirtualKey);
            (EngineResult started, EngineViewer? viewer) = await EngineViewer.StartAsync(_dispatcher, options).ConfigureAwait(true);
            if (viewer is null)
            {
                ErrorMessage = ErrorMessages.For(started);
                return;
            }
            _viewer = viewer;
            _peerName = device.Name;
            _viewedHost = new ViewedHost(device.DeviceId, device.Name);
            _endedByUser = false;
            Attach(viewer);
            viewer.MonitorsChanged += () => _resumesAfterClose = true;
            viewer.VisibilityChanged += visible => IsViewerVisible = visible;
            viewer.Statistics += OnStatistics;
            IsViewerVisible = true;
            IsSessionActive = true;
            ViewerStarted?.Invoke();
            Status = Format(Localization.Strings.ConnectionStatusConnecting, _peerName);
            if (!viewer.HotkeyRegistered)
            {
                ErrorMessage = LocalizedText.Format(Localization.Strings.ErrorHotkeyUnavailable, hotkey.DisplayText);
            }
        }
        finally
        {
            IsBusy = false;
            _ = _gate.Release();
        }
    }

    private void OnStatistics(IReadOnlyList<StreamFigures> figures)
    {
        _figures = figures;
        if (_settings.ShowsStatsOverlay)
        {
            _ = ShowOverlayAsync();
        }
    }

    private async Task ShowOverlayAsync()
    {
        EngineViewer? viewer = _viewer;
        if (viewer is null)
        {
            return;
        }
        foreach (StreamFigures stream in _figures)
        {
            string text = _settings.ShowsStatsOverlay ? OverlayText(stream) : string.Empty;
            await viewer.SetOverlayAsync(stream.StreamId, text).ConfigureAwait(true);
        }
    }

    /// <summary>"60 fps · 22.1 Mbit/s · RTT 4 ms · loss 0.0% · latency 12 ms", like the Mac's overlay.</summary>
    private static string OverlayText(StreamFigures stream)
    {
        CultureInfo culture = CultureInfo.CurrentCulture;
        string latency = stream.LatencyMicroseconds > 0
            ? (stream.LatencyMicroseconds / 1000.0).ToString("F0", culture)
            : Localization.Strings.StatsUnknown;
        return LocalizedText.Format(
            Localization.Strings.StatsOverlay, stream.FramesPerSecond,
            (stream.BitrateKbps / 1000.0).ToString("F1", culture),
            (stream.RoundTripMicroseconds / 1000.0).ToString("F0", culture),
            (stream.LossPermille / 10.0).ToString("F1", culture), latency);
    }

    // --- Shared ---------------------------------------------------------------------------------

    private void Attach(EngineSession session)
    {
        session.StateChanged += state => OnState(session, state);
        session.PairingCode += code => _pairing.Show(code, PeerName(session), accepted => _ = session.ConfirmPairingAsync(accepted));
        session.Paired += () => _ = RefreshPeersAsync();
        session.Failed += reason =>
        {
            HostProblem = EngineHealth.FromFailure(reason) ?? HostProblem;
            ErrorMessage = ErrorMessages.For(reason);
        };
        session.StreamFailed += reason => ErrorMessage = ErrorMessages.For(reason);
        session.Closed += reason => _ = OnClosedAsync(session, reason);
        session.RoleSwapRequested += peer => _ = AnswerSwapAsync(session, peer);
        session.RoleSwapAnswered += (answer, peer) => SwapAnswered(session, answer, peer);
    }

    // --- Role swap ------------------------------------------------------------------------------

    /// <summary>The role this PC takes after swapping away from the given session.</summary>
    private ConnectionMode RoleAfterSwap(EngineSession session) =>
        ReferenceEquals(session, _host) ? ConnectionMode.Viewing : ConnectionMode.Sharing;

    /// <summary>The other computer asks to reverse the roles: agree when this PC can take its new role.</summary>
    private async Task AnswerSwapAsync(EngineSession session, string peerDeviceId)
    {
        ConnectionMode role = RoleAfterSwap(session);
        bool able = role == ConnectionMode.Sharing
            ? await EngineHealth.CheckAsync().ConfigureAwait(true) == HostHealth.Ready
            : Monitors.Count == 0 || Monitors.Any(m => m.IsSelected);
        if (!able || _pendingSwap is not null)
        {
            await session.AnswerRoleSwapAsync(able ? RoleSwapAnswer.Busy : RoleSwapAnswer.CannotHost).ConfigureAwait(true);
            return;
        }
        _pendingSwap = new PendingSwap(role, peerDeviceId, PeerName(session));
        Status = Format(Localization.Strings.ConnectionStatusSwapping, PeerName(session));
        await session.AnswerRoleSwapAsync(RoleSwapAnswer.Accepted).ConfigureAwait(true);
    }

    private void SwapAnswered(EngineSession session, RoleSwapAnswer answer, string peerDeviceId)
    {
        if (answer != RoleSwapAnswer.Accepted)
        {
            ErrorMessage = answer switch
            {
                RoleSwapAnswer.CannotHost => Localization.Strings.ErrorSwapCannotHost,
                RoleSwapAnswer.NoAnswer => Localization.Strings.ErrorSwapNoAnswer,
                _ => Localization.Strings.ErrorSwapBusy,
            };
            return;
        }
        _pendingSwap = new PendingSwap(RoleAfterSwap(session), peerDeviceId, PeerName(session));
        Status = Format(Localization.Strings.ConnectionStatusSwapping, PeerName(session));
    }

    /// <summary>
    /// The session closed after an agreed swap: take the new role. Viewing connects to the same
    /// computer as soon as it advertises itself as a host; it is already paired, so no code.
    /// </summary>
    private async Task PerformSwapAsync()
    {
        PendingSwap? swap = _pendingSwap;
        _pendingSwap = null;
        if (swap is null)
        {
            return;
        }
        await SwitchModeAsync(swap.Role).ConfigureAwait(true);
        if (swap.Role != ConnectionMode.Viewing || Mode != ConnectionMode.Viewing)
        {
            return;
        }
        Status = Format(Localization.Strings.ConnectionStatusSwapping, swap.PeerName);
        if (!await AwaitHostAsync(swap.PeerDeviceId, AppTunables.SwapReconnectTimeout).ConfigureAwait(true))
        {
            ErrorMessage = Localization.Strings.ErrorSwapNotFound;
            Status = Localization.Strings.ConnectionStatusSearching;
        }
    }

    /// <summary>
    /// Connects to this device as soon as Bonjour finds it as a host. Returns false when the
    /// timeout passed first; a null timeout waits as long as viewing stays on.
    /// </summary>
    private async Task<bool> AwaitHostAsync(string deviceId, TimeSpan? timeout)
    {
        DeviceInfo? known = _found.Values.FirstOrDefault(d => d.DeviceId == deviceId);
        if (known is not null)
        {
            await ConnectAsync(known).ConfigureAwait(true);
            return true;
        }
        _autoConnectDeviceId = deviceId;
        _autoConnectTimeout?.Cancel();
        _autoConnectTimeout?.Dispose();
        _autoConnectTimeout = new CancellationTokenSource();
        if (timeout is null)
        {
            return true; // OnFound connects when it appears
        }
        try
        {
            await Task.Delay(timeout.Value, _autoConnectTimeout.Token).ConfigureAwait(true);
        }
        catch (TaskCanceledException)
        {
            return true; // found, or the mode changed
        }
        if (_autoConnectDeviceId != deviceId)
        {
            return true;
        }
        _autoConnectDeviceId = null;
        return false;
    }

    // --- Interruptions --------------------------------------------------------------------------

    // Raised on a system events thread.
    private void OnPowerModeChanged(object? sender, Microsoft.Win32.PowerModeChangedEventArgs e)
    {
        if (e.Mode == Microsoft.Win32.PowerModes.Suspend)
        {
            _ = _dispatcher.BeginInvoke(() => _ = SuspendForSleepAsync());
        }
        else if (e.Mode == Microsoft.Win32.PowerModes.Resume)
        {
            _ = _dispatcher.BeginInvoke(() => _ = RefreshAfterWakeAsync());
        }
    }

    /// <summary>Tells the other computer this PC is going away for a while; a viewer comes back after wake.</summary>
    private async Task SuspendForSleepAsync()
    {
        if (_viewer is not null)
        {
            _resumesAfterClose = true;
            await _viewer.SuspendAsync().ConfigureAwait(true);
        }
        if (_host is not null)
        {
            await _host.SuspendAsync().ConfigureAwait(true);
        }
    }

    /// <summary>
    /// Sockets and Bonjour registrations from before sleep may be stale: sharing without a viewer
    /// listens and advertises anew, and viewing browses anew (a pending reconnect is kept).
    /// </summary>
    private async Task RefreshAfterWakeAsync()
    {
        if (Mode == ConnectionMode.Sharing && _host is not null && !IsSessionActive)
        {
            await _host.StopAsync().ConfigureAwait(true); // OnClosedAsync listens again
        }
        else if (Mode == ConnectionMode.Viewing && _viewer is null && _browser is not null)
        {
            EngineBrowser browser = _browser;
            _browser = null;
            _found.Clear();
            Devices.Clear();
            await browser.DisposeAsync().ConfigureAwait(true);
            string status = Status;
            await StartBrowsingAsync().ConfigureAwait(true);
            if (_autoConnectDeviceId is not null)
            {
                Status = status; // still "waiting for … to come back"
            }
        }
    }

    /// <summary>Whether a closed viewer session should wait for its host and connect again.</summary>
    private bool ComesBack(CoreResult reason) =>
        _viewedHost is not null && !_endedByUser && Mode == ConnectionMode.Viewing &&
        (_resumesAfterClose || reason is CoreResult.PeerSleeping or CoreResult.TimedOut or CoreResult.Network);

    private void RegisterSwapHotkey()
    {
        if (_swapHotkey is not null)
        {
            return;
        }
        _swapHotkey = GlobalHotkey.Register(_settings.SwapHotkey, () => SwapRolesCommand.Execute(null));
        IsSwapHotkeyTaken = _swapHotkey is null;
    }

    /// <summary>The overlay shortcut is optional: a taken one leaves the Settings switch.</summary>
    private void RegisterStatsHotkey() =>
        _statsHotkey ??= GlobalHotkey.Register(
            _settings.StatsHotkey, () => _settings.ShowsStatsOverlay = !_settings.ShowsStatsOverlay);

    private void UnregisterStatsHotkey()
    {
        _statsHotkey?.Dispose();
        _statsHotkey = null;
    }

    private void UnregisterSwapHotkey()
    {
        _swapHotkey?.Dispose();
        _swapHotkey = null;
        IsSwapHotkeyTaken = false;
    }

    private string PeerName(EngineSession session) =>
        session.PeerName.Length > 0 ? session.PeerName : _peerName;

    private void OnState(EngineSession session, SessionState state)
    {
        switch (state)
        {
            case SessionState.Handshaking:
            case SessionState.AwaitingPairing:
            case SessionState.Negotiating:
                IsSessionActive = true;
                Status = Format(Localization.Strings.ConnectionStatusConnecting, PeerName(session));
                break;
            case SessionState.Streaming:
                Status = Format(Localization.Strings.ConnectionStatusConnected, PeerName(session));
                IsStreaming = true;
                RegisterSwapHotkey();
                if (ReferenceEquals(session, _viewer))
                {
                    RegisterStatsHotkey();
                }
                _ = RefreshPeersAsync();
                break;
            default:
                break;
        }
    }

    private async Task OnClosedAsync(EngineSession session, CoreResult reason)
    {
        _pairing.Close();
        IsStreaming = false;
        UnregisterSwapHotkey();
        bool viewerComesBack = ReferenceEquals(session, _viewer) && ComesBack(reason);
        // A peer leaving or going to sleep is not an error; neither is a drop that heals by itself.
        if (reason is not (CoreResult.Ok or CoreResult.ConnectionClosed or CoreResult.PeerSleeping) && !viewerComesBack)
        {
            ErrorMessage = ErrorMessages.For(reason);
        }
        if (ReferenceEquals(session, _viewer))
        {
            ViewedHost? host = _viewedHost;
            _viewer = null;
            _viewedHost = null;
            _resumesAfterClose = false;
            _figures = [];
            IsSessionActive = false;
            UnregisterStatsHotkey();
            await session.DisposeAsync().ConfigureAwait(true);
            if (_pendingSwap is not null)
            {
                await PerformSwapAsync().ConfigureAwait(true);
            }
            else if (viewerComesBack && host is not null)
            {
                Status = Format(Localization.Strings.ConnectionStatusReconnecting, host.Name);
                await Task.Delay(AppTunables.ReconnectDelay).ConfigureAwait(true);
                if (Mode == ConnectionMode.Viewing && _viewer is null)
                {
                    _ = await AwaitHostAsync(host.DeviceId, null).ConfigureAwait(true);
                }
            }
            else if (Mode == ConnectionMode.Viewing)
            {
                Status = Localization.Strings.ConnectionStatusSearching;
            }
        }
        else if (ReferenceEquals(session, _host))
        {
            _host = null;
            IsSessionActive = false;
            EngineAdvertiser? advertiser = _advertiser;
            _advertiser = null;
            await session.DisposeAsync().ConfigureAwait(true);
            if (advertiser is not null)
            {
                await advertiser.DisposeAsync().ConfigureAwait(true);
            }
            if (_pendingSwap is not null)
            {
                await PerformSwapAsync().ConfigureAwait(true);
            }
            else if (Mode == ConnectionMode.Sharing)
            {
                // A core host session takes one viewer; listen again for the next one.
                Status = Localization.Strings.ConnectionStatusWaiting;
                await Task.Delay(AppTunables.RelistenDelay).ConfigureAwait(true);
                await RelistenAsync().ConfigureAwait(true);
            }
        }
    }

    private async Task RelistenAsync()
    {
        await _gate.WaitAsync().ConfigureAwait(true);
        try
        {
            if (Mode == ConnectionMode.Sharing && _host is null)
            {
                await StartSharingAsync().ConfigureAwait(true);
            }
        }
        finally
        {
            _ = _gate.Release();
        }
    }

    private async Task RefreshPeersAsync()
    {
        IReadOnlyList<PairedDevice> peers = await PairedDevices.ListAsync().ConfigureAwait(true);
        Peers.Clear();
        _pairedNames.Clear();
        foreach (PairedDevice peer in peers.OrderBy(p => p.Name, StringComparer.CurrentCultureIgnoreCase))
        {
            if (!peer.Revoked)
            {
                _ = _pairedNames.Add(peer.Name);
            }
            string detail = peer.Revoked
                ? Localization.Strings.PeerRevoked
                : LocalizedText.Format(Localization.Strings.PeerLastSeen, peer.LastSeen.ToLocalTime().ToString("g", CultureInfo.CurrentCulture));
            Peers.Add(new PeerItem(
                peer.Name, detail, peer.Revoked,
                new AsyncRelayCommand(() => ChangeAsync(PairedDevices.RevokeAsync(peer.PublicKey))),
                new AsyncRelayCommand(() => ChangeAsync(PairedDevices.RemoveAsync(peer.PublicKey)))));
        }
        RebuildDevices();
    }

    private async Task ChangeAsync(Task<EngineResult> change)
    {
        EngineResult result = await change.ConfigureAwait(true);
        if (result != EngineResult.Ok)
        {
            ErrorMessage = ErrorMessages.For(result);
        }
        await RefreshPeersAsync().ConfigureAwait(true);
    }

    private async Task LoadMonitorsAsync()
    {
        if (!IsEngineAvailable)
        {
            return;
        }
        IReadOnlyList<MonitorInfo> monitors = await EngineMonitors.ListAsync().ConfigureAwait(true);
        foreach (MonitorTile tile in Monitors)
        {
            tile.PropertyChanged -= OnMonitorSelectionChanged;
        }
        Monitors.Clear();
        foreach (MonitorTile tile in MonitorLayout.Tiles(monitors, [.. _settings.ExcludedMonitors]))
        {
            tile.PropertyChanged += OnMonitorSelectionChanged;
            Monitors.Add(tile);
        }
    }

    private void OnMonitorSelectionChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName != nameof(MonitorTile.IsSelected) || sender is not MonitorTile tile)
        {
            return;
        }
        var excluded = new HashSet<string>(_settings.ExcludedMonitors);
        _ = tile.IsSelected ? excluded.Remove(tile.Key) : excluded.Add(tile.Key);
        _settings.ExcludedMonitors = [.. excluded];
    }

    private async Task RefreshNetworkAsync() =>
        IsNetworkPublic = await Services.NetworkProfile.IsAnyNetworkPublicAsync().ConfigureAwait(true);

    private void OnSettingChanged(object? sender, PropertyChangedEventArgs e)
    {
        switch (e.PropertyName)
        {
            case nameof(AppSettings.ShowsStatsOverlay):
                _ = ShowOverlayAsync(); // shows the latest figures at once, or clears them
                break;
            case nameof(AppSettings.ViewerHotkey):
                OnPropertyChanged(nameof(HotkeyHint));
                break;
            case nameof(AppSettings.HidesNetworkWarning):
                OnPropertyChanged(nameof(ShowsNetworkWarning));
                break;
            case nameof(AppSettings.SwapHotkey):
                OnPropertyChanged(nameof(SwapHint));
                break;
            default:
                break;
        }
    }

    private void Fail(string message)
    {
        Log.Write(LogLevel.Warning, message);
        ErrorMessage = message;
        Mode = ConnectionMode.Off;
        Status = Localization.Strings.ConnectionStatusOff;
    }

    private async Task StopAllAsync()
    {
        _pairing.Close();
        _autoConnectDeviceId = null;
        _autoConnectTimeout?.Cancel();
        _endedByUser = true;
        _viewedHost = null;
        IsStreaming = false;
        UnregisterSwapHotkey();
        UnregisterStatsHotkey();
        _found.Clear();
        Devices.Clear();
        EngineBrowser? browser = _browser;
        EngineAdvertiser? advertiser = _advertiser;
        EngineHost? host = _host;
        EngineViewer? viewer = _viewer;
        _browser = null;
        _advertiser = null;
        _host = null;
        _viewer = null;
        _figures = [];
        IsSessionActive = false;
        if (browser is not null)
        {
            await browser.DisposeAsync().ConfigureAwait(true);
        }
        if (advertiser is not null)
        {
            await advertiser.DisposeAsync().ConfigureAwait(true);
        }
        if (host is not null)
        {
            await host.DisposeAsync().ConfigureAwait(true);
        }
        if (viewer is not null)
        {
            await viewer.DisposeAsync().ConfigureAwait(true);
        }
    }

    private static string Format(string format, string value) => LocalizedText.Format(format, value);
}
