using Osplay.App.Interop;

namespace Osplay.App.Engine;

/// <summary>The paired devices stored by the engine (DPAPI, %APPDATA%\Osplay).</summary>
internal static unsafe class PairedDevices
{
    public static Task<IReadOnlyList<PairedDevice>> ListAsync() => Native.Run<IReadOnlyList<PairedDevice>>(() =>
    {
        var peers = new NativePeer[AppTunables.MaxPairedDevices];
        nuint count;
        fixed (NativePeer* buffer = peers)
        {
            if (NativeMethods.Peers(buffer, (nuint)peers.Length, &count) != EngineResult.Ok)
            {
                return [];
            }
            var result = new List<PairedDevice>();
            for (int i = 0; i < (int)Math.Min(count, (nuint)peers.Length); i++)
            {
                NativePeer* peer = &buffer[i];
                result.Add(new PairedDevice(
                    new ReadOnlySpan<byte>(peer->PublicKey, NativeSizes.KeySize).ToArray(),
                    Utf8.Read(peer->Name, NativeSizes.NameSize),
                    peer->Revoked == 1,
                    DateTimeOffset.FromUnixTimeSeconds(peer->LastSeenUnix)));
            }
            return result;
        }
    });

    public static Task<EngineResult> RevokeAsync(byte[] publicKey) => Native.Run(() =>
    {
        fixed (byte* key = publicKey)
        {
            return NativeMethods.PeerRevoke(key);
        }
    });

    public static Task<EngineResult> RemoveAsync(byte[] publicKey) => Native.Run(() =>
    {
        fixed (byte* key = publicKey)
        {
            return NativeMethods.PeerRemove(key);
        }
    });
}
