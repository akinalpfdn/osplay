using Osplay.App.Interop;

namespace Osplay.App.Engine;

/// <summary>An Osplay device Bonjour found on the network.</summary>
internal sealed record DeviceInfo(
    string Label, string Name, string DeviceId, Platform Platform, Roles Roles, uint ProtocolVersion, string Address,
    ushort Port)
{
    public bool IsCompatible => ProtocolVersion == NativeSizes.ProtocolVersion;
}

/// <summary>A device this PC paired with.</summary>
internal sealed record PairedDevice(byte[] PublicKey, string Name, bool Revoked, DateTimeOffset LastSeen);
