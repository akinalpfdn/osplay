using System.Runtime.InteropServices;
using Osplay.App.Diagnostics;
using Osplay.App.Interop;
using Windows.Networking.Connectivity;
using Windows.Networking.NetworkOperators;

namespace Osplay.App.Services;

/// <summary>What the PC's own network is doing.</summary>
public enum PcNetworkState
{
    Off,
    Starting,
    On,
    /// <summary>Wi-Fi is off on this PC.</summary>
    WifiOff,
    /// <summary>Windows is already turning Mobile hotspot on or off.</summary>
    InUse,
    /// <summary>Windows cannot open a hotspot on this PC (hardware, policy or driver).</summary>
    NotSupported,
    /// <summary>Windows refused to open it for another reason; the log has the status.</summary>
    Failed,
}

/// <summary>A Wi-Fi network this PC opens for devices when there is no router (TAB-04).</summary>
internal interface IPcNetwork : IDisposable
{
    /// <summary>Raised on a Windows thread pool thread.</summary>
    event Action<PcNetworkState>? StateChanged;

    void Open(string name, string passphrase);

    void Close();
}

/// <summary>
/// Windows' Mobile hotspot under Osplay's name and password (192.168.137.0/24; a PC that is online
/// shares its connection). Wi-Fi Direct legacy mode was dropped: Windows disconnects each of its
/// clients two minutes after it joins (DECISIONS.md, 2026-10-28). The hotspot's name, password and
/// idle timeout are Windows-wide settings, so the user's own come back when the network closes.
/// </summary>
internal sealed class MobileHotspotPcNetwork : IPcNetwork
{
    private readonly SemaphoreSlim _gate = new(1, 1); // one open or close at a time
    private readonly Timer _poll;
    private NetworkOperatorTetheringManager? _manager; // set while Osplay's hotspot is configured
    private NetworkOperatorTetheringAccessPointConfiguration? _userSettings;
    private bool _userTimeoutEnabled;
    private bool _on;

    public MobileHotspotPcNetwork()
    {
        _poll = new Timer(_ => _ = CheckAsync(), null, Timeout.InfiniteTimeSpan, Timeout.InfiniteTimeSpan);
    }

    public event Action<PcNetworkState>? StateChanged;

    public void Open(string name, string passphrase) => _ = OpenAsync(name, passphrase);

    public void Close() => _ = CloseAsync();

    public void Dispose()
    {
        _poll.Dispose();
        // Quitting: the hotspot stops and the user's settings return before Osplay exits. On the
        // thread pool, so no continuation waits for the UI thread this call blocks.
        _ = Task.Run(CloseAsync).Wait(AppTunables.PcNetworkCloseTimeout);
    }

    private async Task OpenAsync(string name, string passphrase)
    {
        await _gate.WaitAsync().ConfigureAwait(false);
        try
        {
            StateChanged?.Invoke(PcNetworkState.Starting);
            await StopAsync().ConfigureAwait(false);
            NetworkOperatorTetheringManager? manager = CreateManager();
            if (manager is null)
            {
                StateChanged?.Invoke(PcNetworkState.NotSupported);
                return;
            }
            if (manager.TetheringOperationalState != TetheringOperationalState.Off)
            {
                // A hotspot the user opened runs with their settings; Osplay's start over it.
                _ = await manager.StopTetheringAsync();
            }
            _userSettings = manager.GetCurrentAccessPointConfiguration();
            _userTimeoutEnabled = NetworkOperatorTetheringManager.IsNoConnectionsTimeoutEnabled();
            _manager = manager;
            await manager.ConfigureAccessPointAsync(new NetworkOperatorTetheringAccessPointConfiguration
            {
                Ssid = name,
                Passphrase = passphrase,
            });
            // Windows turns an idle hotspot off after a few minutes; devices may join much later.
            await NetworkOperatorTetheringManager.DisableNoConnectionsTimeoutAsync();
            NetworkOperatorTetheringOperationResult result = await manager.StartTetheringAsync();
            if (result.Status != TetheringOperationStatus.Success)
            {
                Log.Write(LogLevel.Warning, $"The PC's network could not start: {result.Status} {result.AdditionalErrorMessage}");
                await StopAsync().ConfigureAwait(false);
                StateChanged?.Invoke(StateFor(result.Status));
                return;
            }
            _on = true;
            _ = _poll.Change(AppTunables.PcNetworkPollInterval, AppTunables.PcNetworkPollInterval);
            StateChanged?.Invoke(PcNetworkState.On);
        }
        catch (Exception error) when (error is COMException or UnauthorizedAccessException or InvalidOperationException)
        {
            Log.Write(LogLevel.Warning, $"The PC's network could not start: {error.Message}");
            await StopAsync().ConfigureAwait(false);
            StateChanged?.Invoke(PcNetworkState.Failed);
        }
        finally
        {
            _ = _gate.Release();
        }
    }

    private async Task CloseAsync()
    {
        await _gate.WaitAsync().ConfigureAwait(false);
        try
        {
            await StopAsync().ConfigureAwait(false);
            StateChanged?.Invoke(PcNetworkState.Off);
        }
        finally
        {
            _ = _gate.Release();
        }
    }

    /// <summary>Notices when Windows or the user turned the hotspot off while it was open.</summary>
    private async Task CheckAsync()
    {
        if (!await _gate.WaitAsync(0).ConfigureAwait(false))
        {
            return; // an open or close is running; it reports the state itself
        }
        try
        {
            if (!_on || _manager?.TetheringOperationalState != TetheringOperationalState.Off)
            {
                return;
            }
            Log.Write(LogLevel.Warning, "The PC's network was turned off outside Osplay");
            await StopAsync().ConfigureAwait(false);
            StateChanged?.Invoke(PcNetworkState.Off);
        }
        finally
        {
            _ = _gate.Release();
        }
    }

    /// <summary>Stops Osplay's hotspot and puts the user's own settings back. Called with the gate held.</summary>
    private async Task StopAsync()
    {
        _on = false;
        _ = _poll.Change(Timeout.InfiniteTimeSpan, Timeout.InfiniteTimeSpan);
        NetworkOperatorTetheringManager? manager = _manager;
        _manager = null;
        if (manager is null)
        {
            return;
        }
        try
        {
            if (manager.TetheringOperationalState != TetheringOperationalState.Off)
            {
                _ = await manager.StopTetheringAsync();
            }
            if (_userSettings is not null)
            {
                await manager.ConfigureAccessPointAsync(_userSettings);
            }
            if (_userTimeoutEnabled)
            {
                await NetworkOperatorTetheringManager.EnableNoConnectionsTimeoutAsync();
            }
        }
        catch (Exception error) when (error is COMException or UnauthorizedAccessException or InvalidOperationException)
        {
            Log.Write(LogLevel.Warning, $"Restoring Mobile hotspot settings failed: {error.Message}");
        }
        _userSettings = null;
    }

    /// <summary>
    /// A hotspot manager for the internet connection, or for any connection Windows knows when the PC
    /// is offline (no router): the hotspot then shares nothing but still opens.
    /// </summary>
    private static NetworkOperatorTetheringManager? CreateManager()
    {
        IReadOnlyList<ConnectionProfile> known = NetworkInformation.GetConnectionProfiles();
        ConnectionProfile? profile = NetworkInformation.GetInternetConnectionProfile()
            ?? known.FirstOrDefault(p => p.IsWlanConnectionProfile)
            ?? (known.Count > 0 ? known[0] : null);
        if (profile is null)
        {
            Log.Write(LogLevel.Warning, "The PC's network cannot start: Windows lists no network connection");
            return null;
        }
        TetheringCapability capability = NetworkOperatorTetheringManager.GetTetheringCapabilityFromConnectionProfile(profile);
        if (capability != TetheringCapability.Enabled)
        {
            Log.Write(LogLevel.Warning, $"The PC's network cannot start: Mobile hotspot is {capability}");
            return null;
        }
        return NetworkOperatorTetheringManager.CreateFromConnectionProfile(profile);
    }

    private static PcNetworkState StateFor(TetheringOperationStatus status) => status switch
    {
        TetheringOperationStatus.WiFiDeviceOff => PcNetworkState.WifiOff,
        TetheringOperationStatus.OperationInProgress => PcNetworkState.InUse,
        _ => PcNetworkState.Failed,
    };
}
