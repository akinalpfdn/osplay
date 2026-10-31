using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Windows.Threading;
using Osplay.App.Interop;

namespace Osplay.App.Engine;

/// <summary>
/// The iPads on a USB cable, followed through the Apple Mobile Device Service; events arrive on the
/// dispatcher's thread. Without the service none are reported and the engine keeps trying.
/// </summary>
internal sealed class EngineUsbWatcher : IAsyncDisposable
{
    private readonly Dispatcher _dispatcher;
    private GCHandle _self;
    private nint _watcher;

    private EngineUsbWatcher(Dispatcher dispatcher) => _dispatcher = dispatcher;

    /// <summary>The argument is the device ID; it changes when the cable is plugged in again.</summary>
    public event Action<uint>? Attached;
    public event Action<uint>? Detached;

    public static async Task<(EngineResult Result, EngineUsbWatcher? Watcher)> StartAsync(Dispatcher dispatcher)
    {
        var created = new EngineUsbWatcher(dispatcher);
        created._self = GCHandle.Alloc(created);
        nint context = GCHandle.ToIntPtr(created._self);
        (EngineResult result, nint handle) = await Native.Run(() => StartNative(context)).ConfigureAwait(true);
        if (result != EngineResult.Ok)
        {
            created._self.Free();
            return (result, null);
        }
        created._watcher = handle;
        return (result, created);
    }

    public async ValueTask DisposeAsync()
    {
        nint watcher = _watcher;
        _watcher = 0;
        if (watcher != 0)
        {
            // No callback runs after this returns.
            await Native.Run(() => NativeMethods.UsbWatcherDestroy(watcher)).ConfigureAwait(true);
        }
        if (_self.IsAllocated)
        {
            _self.Free();
        }
    }

    private static unsafe (EngineResult Result, nint Watcher) StartNative(nint context)
    {
        var callbacks = new NativeUsbCallbacks { Context = context, OnAttached = &OnAttached, OnDetached = &OnDetached };
        nint watcher;
        EngineResult started = NativeMethods.UsbWatcherStart(&callbacks, &watcher);
        return (started, watcher);
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnAttached(nint context, uint deviceId)
    {
        var self = (EngineUsbWatcher)GCHandle.FromIntPtr(context).Target!;
        self._dispatcher.BeginInvoke(() => self.Attached?.Invoke(deviceId));
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnDetached(nint context, uint deviceId)
    {
        var self = (EngineUsbWatcher)GCHandle.FromIntPtr(context).Target!;
        self._dispatcher.BeginInvoke(() => self.Detached?.Invoke(deviceId));
    }
}
