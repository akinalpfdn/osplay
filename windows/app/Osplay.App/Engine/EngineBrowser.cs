using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Windows.Threading;
using Osplay.App.Interop;

namespace Osplay.App.Engine;

/// <summary>Bonjour browsing through the engine; events arrive on the dispatcher's thread.</summary>
internal sealed unsafe class EngineBrowser : IDisposable
{
    private readonly Dispatcher _dispatcher;
    private GCHandle _self;
    private nint _browser;

    private EngineBrowser(Dispatcher dispatcher) => _dispatcher = dispatcher;

    public event Action<DeviceInfo>? Found;
    public event Action<string>? Lost;

    public static EngineResult Start(Dispatcher dispatcher, out EngineBrowser? browser)
    {
        var created = new EngineBrowser(dispatcher);
        created._self = GCHandle.Alloc(created);
        var callbacks = new NativeBrowserCallbacks
        {
            Context = GCHandle.ToIntPtr(created._self),
            OnFound = &OnFound,
            OnLost = &OnLost,
        };
        nint handle;
        EngineResult result = NativeMethods.BrowserStart(&callbacks, &handle);
        if (result != EngineResult.Ok)
        {
            created._self.Free();
            browser = null;
            return result;
        }
        created._browser = handle;
        browser = created;
        return result;
    }

    public void Dispose()
    {
        if (_browser == 0)
        {
            return;
        }
        NativeMethods.BrowserDestroy(_browser); // no callback runs after this returns
        _browser = 0;
        _self.Free();
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnFound(nint context, NativeDevice* device)
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
    private static void OnLost(nint context, byte* label)
    {
        var self = (EngineBrowser)GCHandle.FromIntPtr(context).Target!;
        string text = Utf8.Read(label);
        self._dispatcher.BeginInvoke(() => self.Lost?.Invoke(text));
    }
}
