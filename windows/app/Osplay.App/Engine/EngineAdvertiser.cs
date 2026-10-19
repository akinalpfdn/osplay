using Osplay.App.Interop;

namespace Osplay.App.Engine;

/// <summary>Announces this PC over Bonjour until disposed.</summary>
internal sealed unsafe class EngineAdvertiser : IDisposable
{
    private nint _handle;

    private EngineAdvertiser(nint handle) => _handle = handle;

    public static EngineResult Start(string name, Roles roles, ushort port, out EngineAdvertiser? advertiser)
    {
        nint text = Utf8.Allocate(name);
        try
        {
            nint handle;
            EngineResult result = NativeMethods.AdvertiserStart((byte*)text, roles, port, &handle);
            advertiser = result == EngineResult.Ok ? new EngineAdvertiser(handle) : null;
            return result;
        }
        finally
        {
            Utf8.Free(text);
        }
    }

    public void Dispose()
    {
        if (_handle != 0)
        {
            NativeMethods.AdvertiserDestroy(_handle);
            _handle = 0;
        }
    }
}
