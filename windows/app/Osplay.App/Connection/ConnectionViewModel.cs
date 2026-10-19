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

    internal ConnectionViewModel(Dispatcher dispatcher, IPairingPrompt pairing, AppSettings settings)
    {
        _dispatcher = dispatcher;
        _pairing = pairing;
        _settings = settings;
        _settings.PropertyChanged += OnSettingChanged;
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

    /// <summary>False when the engine library could not be loaded; everything is disabled.</summary>
    [ObservableProperty]
    private bool _isEngineAvailable = true;

    /// <summary>Why this PC cannot share its screen right now, or null.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasHostProblem), nameof(HostProblemMessage), nameof(HealthStatusText))]
    private HostHealth? _hostProblem;

    [ObservableProperty]
    private bool _isNetworkPublic;

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

    /// <summary>The viewer hotkey, for hints ("Ctrl+Alt+Shift+V hides and shows the display").</summary>
    public string HotkeyHint => LocalizedText.Format(Localization.Strings.ViewerHotkeyHint, _settings.ViewerHotkey.DisplayText);

    /// <summary>Hosts to connect to.</summary>
    public ObservableCollection<DeviceItem> Devices { get; } = [];

    public ObservableCollection<PeerItem> Peers { get; } = [];

    /// <summary>This PC's monitors; the selected ones are offered to hosts.</summary>
    public ObservableCollection<MonitorTile> Monitors { get; } = [];

    public void Dispose() => _gate.Dispose();

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
    private async Task SetModeAsync(ConnectionMode mode)
    {
        await _gate.WaitAsync().ConfigureAwait(true);
        IsBusy = true;
        try
        {
            // Selecting the active role again turns it off, like the Mac's role cards.
            ConnectionMode target = mode == Mode ? ConnectionMode.Off : mode;
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

    [RelayCommand]
    private async Task DisconnectAsync()
    {
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
            .StartAsync(_dispatcher, _deviceName, _settings.Quality.MillibitsPerPixel())
            .ConfigureAwait(true);
        if (host is null)
        {
            HostProblem = EngineHealth.FromFailure(started) ?? HostProblem;
            Fail(ErrorMessages.For(started));
            return;
        }
        HostProblem = null;
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
            Attach(viewer);
            viewer.VisibilityChanged += visible => IsViewerVisible = visible;
            viewer.Statistics += OnStatistics;
            IsViewerVisible = true;
            IsSessionActive = true;
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
                _ = RefreshPeersAsync();
                break;
            default:
                break;
        }
    }

    private async Task OnClosedAsync(EngineSession session, CoreResult reason)
    {
        _pairing.Close();
        if (reason != CoreResult.Ok && reason != CoreResult.ConnectionClosed)
        {
            ErrorMessage = ErrorMessages.For(reason);
        }
        if (ReferenceEquals(session, _viewer))
        {
            _viewer = null;
            _figures = [];
            IsSessionActive = false;
            await session.DisposeAsync().ConfigureAwait(true);
            if (Mode == ConnectionMode.Viewing)
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
            if (Mode == ConnectionMode.Sharing)
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
