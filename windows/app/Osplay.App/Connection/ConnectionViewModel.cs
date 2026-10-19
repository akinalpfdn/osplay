using System.Collections.ObjectModel;
using System.Windows.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Osplay.App.Engine;
using Osplay.App.Interop;

namespace Osplay.App.Connection;

/// <summary>What this PC does on the network.</summary>
public enum ConnectionMode
{
    Off,
    /// <summary>Hosting: advertised over Bonjour, a viewer connects.</summary>
    Sharing,
    /// <summary>Viewing: browsing for hosts, the user picks one.</summary>
    Viewing,
}

/// <summary>A tray menu entry: text and what it does.</summary>
public sealed class MenuAction(string header, IRelayCommand command)
{
    public string Header { get; } = header;

    public IRelayCommand Command { get; } = command;
}

/// <summary>A paired device in the tray, opening its actions.</summary>
public sealed class PeerItem(string header, IReadOnlyList<MenuAction> actions)
{
    public string Header { get; } = header;

    public IReadOnlyList<MenuAction> Actions { get; } = actions;
}

/// <summary>
/// Temporary connection controls in the tray (DEVPLAN Phase 10; Phase 12 builds the final UI):
/// share this PC's screen or use it as a display, discovered hosts, pairing, paired devices.
/// Runs on the UI thread.
/// </summary>
public sealed partial class ConnectionViewModel : ObservableObject, IDisposable
{
    private static readonly TimeSpan _relistenDelay = TimeSpan.FromSeconds(1);

    private readonly Dispatcher _dispatcher;
    private readonly IPairingPrompt _pairing;
    private readonly string _deviceName = System.Net.Dns.GetHostName();
    private readonly Dictionary<string, DeviceInfo> _found = [];
    private EngineBrowser? _browser;
    private EngineHost? _host;
    private EngineAdvertiser? _advertiser;
    private EngineViewer? _viewer;
    private string _peerName = string.Empty;

    internal ConnectionViewModel(Dispatcher dispatcher, IPairingPrompt pairing)
    {
        _dispatcher = dispatcher;
        _pairing = pairing;
        RefreshPeers();
    }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsOff), nameof(IsSharing), nameof(IsViewing))]
    private ConnectionMode _mode = ConnectionMode.Off;

    [ObservableProperty]
    private string _status = Localization.Strings.ConnectionStatusOff;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasError))]
    private string? _errorMessage;

    [ObservableProperty]
    private bool _isSessionActive;

    public bool IsOff => Mode == ConnectionMode.Off;

    public bool IsSharing => Mode == ConnectionMode.Sharing;

    public bool IsViewing => Mode == ConnectionMode.Viewing;

    public bool HasError => ErrorMessage is not null;

    /// <summary>Hosts to connect to.</summary>
    public ObservableCollection<MenuAction> Devices { get; } = [];

    public ObservableCollection<PeerItem> Peers { get; } = [];

    public void Dispose()
    {
        StopAll();
        _pairing.Close();
    }

    [RelayCommand]
    private void SetMode(ConnectionMode mode)
    {
        if (mode == Mode)
        {
            return;
        }
        StopAll();
        Mode = mode;
        ErrorMessage = null;
        switch (mode)
        {
            case ConnectionMode.Off:
                Status = Localization.Strings.ConnectionStatusOff;
                break;
            case ConnectionMode.Sharing:
                StartSharing();
                break;
            case ConnectionMode.Viewing:
                StartBrowsing();
                break;
        }
    }

    [RelayCommand]
    private void Disconnect()
    {
        _host?.Stop();
        _viewer?.Stop();
    }

    // --- Sharing ------------------------------------------------------------------------------

    private void StartSharing()
    {
        EngineResult started = EngineHost.Start(_dispatcher, _deviceName, out EngineHost? host);
        if (host is null)
        {
            Fail(ErrorMessages.For(started));
            return;
        }
        EngineResult advertised = EngineAdvertiser.Start(_deviceName, Roles.Host, host.ControlPort, out _advertiser);
        if (_advertiser is null)
        {
            host.Dispose();
            Fail(ErrorMessages.For(advertised));
            return;
        }
        _host = host;
        Attach(host);
        Status = Localization.Strings.ConnectionStatusWaiting;
    }

    // --- Viewing ------------------------------------------------------------------------------

    private void StartBrowsing()
    {
        EngineResult started = EngineBrowser.Start(_dispatcher, out _browser);
        if (_browser is null)
        {
            Fail(ErrorMessages.For(started));
            return;
        }
        _browser.Found += OnFound;
        _browser.Lost += OnLost;
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
            Devices.Add(new MenuAction($"{device.Name} ({platform})", new RelayCommand(() => Connect(device))));
        }
    }

    private void Connect(DeviceInfo device)
    {
        if (_viewer is not null)
        {
            return;
        }
        if (!device.IsCompatible)
        {
            ErrorMessage = Localization.Strings.ErrorIncompatible;
            return;
        }
        ErrorMessage = null;
        EngineResult started = EngineViewer.Start(_dispatcher, _deviceName, device.Address, device.Port, out _viewer);
        if (_viewer is null)
        {
            ErrorMessage = ErrorMessages.For(started);
            return;
        }
        _peerName = device.Name;
        Attach(_viewer);
        IsSessionActive = true;
        Status = Format(Localization.Strings.ConnectionStatusConnecting, _peerName);
        if (!_viewer.HotkeyRegistered)
        {
            ErrorMessage = Localization.Strings.ErrorHotkeyUnavailable;
        }
    }

    // --- Shared ---------------------------------------------------------------------------------

    private void Attach(EngineSession session)
    {
        session.StateChanged += state => OnState(session, state);
        session.PairingCode += code => _pairing.Show(code, PeerName(session), session.ConfirmPairing);
        session.Paired += RefreshPeers;
        session.Failed += reason => ErrorMessage = ErrorMessages.For(reason);
        session.StreamFailed += reason => ErrorMessage = ErrorMessages.For(reason);
        session.Closed += reason => OnClosed(session, reason);
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
                RefreshPeers();
                break;
            default:
                break;
        }
    }

    private void OnClosed(EngineSession session, CoreResult reason)
    {
        _pairing.Close();
        if (reason != CoreResult.Ok && reason != CoreResult.ConnectionClosed)
        {
            ErrorMessage = ErrorMessages.For(reason);
        }
        session.Dispose();
        IsSessionActive = false;
        if (ReferenceEquals(session, _viewer))
        {
            _viewer = null;
            Status = Localization.Strings.ConnectionStatusSearching;
        }
        else if (ReferenceEquals(session, _host))
        {
            _host = null;
            _advertiser?.Dispose();
            _advertiser = null;
            if (Mode == ConnectionMode.Sharing)
            {
                // A core host session takes one viewer; listen again for the next one.
                Status = Localization.Strings.ConnectionStatusWaiting;
                _ = _dispatcher.InvokeAsync(async () =>
                {
                    await Task.Delay(_relistenDelay).ConfigureAwait(true);
                    if (Mode == ConnectionMode.Sharing && _host is null)
                    {
                        StartSharing();
                    }
                });
            }
        }
    }

    private void RefreshPeers()
    {
        Peers.Clear();
        foreach (PairedDevice peer in PairedDevices.List())
        {
            string header = peer.Revoked ? Format(Localization.Strings.PeerRevokedHeader, peer.Name) : peer.Name;
            var actions = new List<MenuAction>();
            if (!peer.Revoked)
            {
                actions.Add(new MenuAction(
                    Localization.Strings.TrayRevoke, new RelayCommand(() => Change(PairedDevices.Revoke(peer.PublicKey)))));
            }
            actions.Add(new MenuAction(
                Localization.Strings.TrayRemove, new RelayCommand(() => Change(PairedDevices.Remove(peer.PublicKey)))));
            Peers.Add(new PeerItem(header, actions));
        }
    }

    private void Change(EngineResult result)
    {
        if (result != EngineResult.Ok)
        {
            ErrorMessage = ErrorMessages.For(result);
        }
        RefreshPeers();
    }

    private void Fail(string message)
    {
        ErrorMessage = message;
        Mode = ConnectionMode.Off;
        Status = Localization.Strings.ConnectionStatusOff;
    }

    private void StopAll()
    {
        _pairing.Close();
        _browser?.Dispose();
        _browser = null;
        _found.Clear();
        Devices.Clear();
        _advertiser?.Dispose();
        _advertiser = null;
        _host?.Dispose();
        _host = null;
        _viewer?.Dispose();
        _viewer = null;
        IsSessionActive = false;
    }

    private static string Format(string format, string value) => LocalizedText.Format(format, value);
}
