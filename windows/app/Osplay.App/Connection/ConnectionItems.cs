using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

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

/// <summary>A computer found nearby that this PC can show.</summary>
public sealed class DeviceItem(string name, string detail, bool isCompatible, IRelayCommand connect)
{
    public string Name { get; } = name;

    /// <summary>Platform and pairing state, or why it cannot be used.</summary>
    public string Detail { get; } = detail;

    public bool IsCompatible { get; } = isCompatible;

    public IRelayCommand ConnectCommand { get; } = connect;
}

/// <summary>A paired device in Settings.</summary>
public sealed class PeerItem(string name, string detail, bool isRevoked, IRelayCommand revoke, IRelayCommand remove)
{
    public string Name { get; } = name;

    public string Detail { get; } = detail;

    public bool IsRevoked { get; } = isRevoked;

    public IRelayCommand RevokeCommand { get; } = revoke;

    public IRelayCommand RemoveCommand { get; } = remove;
}

/// <summary>One of this PC's monitors in the layout picker; selected ones are offered to hosts.</summary>
public sealed partial class MonitorTile(byte id, string key, string name, string size) : ObservableObject
{
    public byte Id { get; } = id;

    /// <summary>Its identity across launches (AppSettings.MonitorKey).</summary>
    public string Key { get; } = key;

    public string Name { get; } = name;

    /// <summary>"2560 × 1440".</summary>
    public string Size { get; } = size;

    /// <summary>Place on the desktop, physical pixels; the picker scales it.</summary>
    public double Left { get; init; }

    public double Top { get; init; }

    public double Width { get; init; }

    public double Height { get; init; }

    [ObservableProperty]
    private bool _isSelected = true;
}
