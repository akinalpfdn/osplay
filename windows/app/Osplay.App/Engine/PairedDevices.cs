using Osplay.App.Interop;

namespace Osplay.App.Engine;

/// <summary>The paired devices stored by the engine (DPAPI, %APPDATA%\Osplay).</summary>
internal static unsafe class PairedDevices
{
    private const int _capacity = 256;

    public static IReadOnlyList<PairedDevice> List()
    {
        var peers = new NativePeer[_capacity];
        nuint count;
        fixed (NativePeer* buffer = peers)
        {
            if (NativeMethods.Peers(buffer, _capacity, &count) != EngineResult.Ok)
            {
                return [];
            }
        }
        var result = new List<PairedDevice>();
        for (int i = 0; i < (int)Math.Min(count, _capacity); i++)
        {
            fixed (NativePeer* peer = &peers[i])
            {
                result.Add(new PairedDevice(
                    new ReadOnlySpan<byte>(peer->PublicKey, NativeSizes.KeySize).ToArray(),
                    Utf8.Read(peer->Name, NativeSizes.NameSize),
                    peer->Revoked == 1,
                    DateTimeOffset.FromUnixTimeSeconds(peer->LastSeenUnix)));
            }
        }
        return result;
    }

    public static EngineResult Revoke(byte[] publicKey)
    {
        fixed (byte* key = publicKey)
        {
            return NativeMethods.PeerRevoke(key);
        }
    }

    public static EngineResult Remove(byte[] publicKey)
    {
        fixed (byte* key = publicKey)
        {
            return NativeMethods.PeerRemove(key);
        }
    }
}
