using System.Runtime.InteropServices;
using Osplay.App.Diagnostics;
using Osplay.App.Interop;
using Windows.Devices.WiFiDirect;
using Windows.Security.Credentials;

namespace Osplay.App.Services;

/// <summary>What the PC's own network is doing.</summary>
public enum PcNetworkState
{
    Off,
    Starting,
    On,
    /// <summary>Wi-Fi is off on this PC.</summary>
    WifiOff,
    /// <summary>Mobile hotspot or another app holds the Wi-Fi card.</summary>
    InUse,
    /// <summary>The Wi-Fi card cannot be an access point.</summary>
    NotSupported,
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
/// Wi-Fi Direct in legacy mode: the PC becomes the group owner of a WPA2 network any device joins
/// like an ordinary one (192.168.137.0/24, no internet through it). Windows' Mobile hotspot takes
/// the card over and stops it.
/// </summary>
internal sealed class WiFiDirectPcNetwork : IPcNetwork
{
    private readonly Lock _lock = new();
    private WiFiDirectAdvertisementPublisher? _publisher;

    public event Action<PcNetworkState>? StateChanged;

    public void Open(string name, string passphrase)
    {
        Close();
        var publisher = new WiFiDirectAdvertisementPublisher();
        WiFiDirectAdvertisement advertisement = publisher.Advertisement;
        advertisement.IsAutonomousGroupOwnerEnabled = true;
        advertisement.LegacySettings.IsEnabled = true;
        advertisement.LegacySettings.Ssid = name;
        advertisement.LegacySettings.Passphrase = new PasswordCredential { Password = passphrase };
        publisher.StatusChanged += OnStatusChanged;
        lock (_lock)
        {
            _publisher = publisher;
        }
        StateChanged?.Invoke(PcNetworkState.Starting);
        try
        {
            publisher.Start();
        }
        catch (Exception error) when (error is COMException or UnauthorizedAccessException)
        {
            Log.Write(LogLevel.Warning, $"The PC's network could not start: {error.Message}");
            Close();
            StateChanged?.Invoke(PcNetworkState.NotSupported);
        }
    }

    public void Close()
    {
        WiFiDirectAdvertisementPublisher? publisher;
        lock (_lock)
        {
            publisher = _publisher;
            _publisher = null;
        }
        if (publisher is null)
        {
            return;
        }
        publisher.StatusChanged -= OnStatusChanged;
        if (publisher.Status is WiFiDirectAdvertisementPublisherStatus.Started or WiFiDirectAdvertisementPublisherStatus.Created)
        {
            publisher.Stop();
        }
        StateChanged?.Invoke(PcNetworkState.Off);
    }

    public void Dispose() => Close();

    private void OnStatusChanged(
        WiFiDirectAdvertisementPublisher sender, WiFiDirectAdvertisementPublisherStatusChangedEventArgs args)
    {
        lock (_lock)
        {
            if (!ReferenceEquals(sender, _publisher))
            {
                return; // a publisher closed meanwhile
            }
        }
        PcNetworkState state = args.Status switch
        {
            WiFiDirectAdvertisementPublisherStatus.Created => PcNetworkState.Starting,
            WiFiDirectAdvertisementPublisherStatus.Started => PcNetworkState.On,
            WiFiDirectAdvertisementPublisherStatus.Stopped => PcNetworkState.Off,
            _ => args.Error switch
            {
                WiFiDirectError.RadioNotAvailable => PcNetworkState.WifiOff,
                WiFiDirectError.ResourceInUse => PcNetworkState.InUse,
                _ => PcNetworkState.NotSupported,
            },
        };
        if (args.Status == WiFiDirectAdvertisementPublisherStatus.Aborted)
        {
            Log.Write(LogLevel.Warning, $"The PC's network stopped: {args.Error}");
        }
        StateChanged?.Invoke(state);
    }
}
