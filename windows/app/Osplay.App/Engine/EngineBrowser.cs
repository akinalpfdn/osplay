using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Windows.Threading;
using Osplay.App.Interop;

namespace Osplay.App.Engine;

/// <summary>Bonjour browsing through the engine; events arrive on the dispatcher's thread.</summary>
internal sealed class EngineBrowser : IAsyncDisposable
{
    private readonly Dispatcher _dispatcher;
    private GCHandle _self;
    private nint _browser;

    private EngineBrowser(Dispatcher dispatcher) => _dispatcher = dispatcher;

    public event Action<DeviceInfo>? Found;
    public event Action<string>? Lost;

    public static async Task<(EngineResult Result, EngineBrowser? Browser)> StartAsync(Dispatcher dispatcher)
    {
        var created = new EngineBrowser(dispatcher);
        created._self = GCHandle.Alloc(created);
        nint context = GCHandle.ToIntPtr(created._self);
        (EngineResult result, nint handle) = await Native.Run(() => StartNative(context)).ConfigureAwait(true);
        if (result != EngineResult.Ok)
        {
            created._self.Free();
            return (result, null);
        }
        created._browser = handle;
        return (result, created);
    }

    public async ValueTask DisposeAsync()
    {
        nint browser = _browser;
        _browser = 0;
        if (browser != 0)
        {
            // No callback runs after this returns.
            await Native.Run(() => NativeMethods.BrowserDestroy(browser)).ConfigureAwait(true);
        }
        if (_self.IsAllocated)
        {
            _self.Free();
        }
    }

    private static unsafe (EngineResult Result, nint Browser) StartNative(nint context)
    {
        var callbacks = new NativeBrowserCallbacks { Context = context, OnFound = &OnFound, OnLost = &OnLost };
        nint browser;
        EngineResult started = NativeMethods.BrowserStart(&callbacks, &browser);
        return (started, browser);
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static unsafe void OnFound(nint context, NativeDevice* device)
    {
        var self = (EngineBrowser)GCHandle.FromIntPtr(context).Target!;
        NativeAdvertisement advertisement = device->Advertisement;
        var info = new DeviceInfo(
            Utf8.Read(device->Label, NativeSizes.NameSize),
            Utf8.Read(advertisement.Name, NativeSizes.NameSize),
            Convert.ToHexStringLower(new ReadOnlySpan<byte>(advertisement.DeviceId, NativeSizes.DeviceIdSize)),
            advertisement.Platform,
            advertisement.Roles,
            advertisement.ProtocolVersion,
            Utf8.Read(device->Address, NativeSizes.AddressSize),
            device->Port);
        self._dispatcher.BeginInvoke(() => self.Found?.Invoke(info));
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static unsafe void OnLost(nint context, byte* label)
    {
        var self = (EngineBrowser)GCHandle.FromIntPtr(context).Target!;
        string text = Utf8.Read(label);
        self._dispatcher.BeginInvoke(() => self.Lost?.Invoke(text));
    }
}
