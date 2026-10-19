using Osplay.App.Interop;

namespace Osplay.App.Engine;

/// <summary>Announces this PC over Bonjour until disposed.</summary>
internal sealed class EngineAdvertiser : IAsyncDisposable
{
    private nint _handle;

    private EngineAdvertiser(nint handle) => _handle = handle;

    public static Task<(EngineResult Result, EngineAdvertiser? Advertiser)> StartAsync(
        string name, Roles roles, ushort port) => Native.Run(() => Start(name, roles, port));

    private static unsafe (EngineResult Result, EngineAdvertiser? Advertiser) Start(string name, Roles roles, ushort port)
    {
        nint text = Utf8.Allocate(name);
        try
        {
            nint handle;
            EngineResult result = NativeMethods.AdvertiserStart((byte*)text, roles, port, &handle);
            return (result, result == EngineResult.Ok ? new EngineAdvertiser(handle) : null);
        }
        finally
        {
            Utf8.Free(text);
        }
    }

    public async ValueTask DisposeAsync()
    {
        nint handle = _handle;
        _handle = 0;
        if (handle != 0)
        {
            await Native.Run(() => NativeMethods.AdvertiserDestroy(handle)).ConfigureAwait(true);
        }
    }
}
