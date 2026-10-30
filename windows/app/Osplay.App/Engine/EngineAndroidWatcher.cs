using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Windows.Threading;
using Osplay.App.Diagnostics;
using Osplay.App.Interop;

namespace Osplay.App.Engine;

/// <summary>
/// Android devices on a USB cable (TAB-05): the engine switches each one to Osplay's accessory
/// through UsbDk and reports those that come back on WinUSB; events arrive on the dispatcher's
/// thread. Without UsbDk nothing is switched.
/// </summary>
internal sealed class EngineAndroidWatcher : IAsyncDisposable
{
    private readonly Dispatcher _dispatcher;
    private GCHandle _self;

    private EngineAndroidWatcher(Dispatcher dispatcher) => _dispatcher = dispatcher;

    /// <summary>The argument is the accessory ID; it changes when the cable is plugged in again.</summary>
    public event Action<uint>? Attached;
    public event Action<uint>? Detached;

    /// <summary>A device with USB debugging on was plugged in: the cable cannot reach it.</summary>
    public event Action? DebuggingOn;

    /// <summary>The engine handle, for <see cref="EngineHost.StartAccessoryAsync"/>; 0 once disposed.</summary>
    internal nint Handle { get; private set; }

    public static async Task<(EngineResult Result, EngineAndroidWatcher? Watcher)> StartAsync(Dispatcher dispatcher)
    {
        var created = new EngineAndroidWatcher(dispatcher);
        created._self = GCHandle.Alloc(created);
        nint context = GCHandle.ToIntPtr(created._self);
        (EngineResult result, nint handle) = await Native.Run(() => StartNative(context)).ConfigureAwait(true);
        if (result != EngineResult.Ok)
        {
            created._self.Free();
            return (result, null);
        }
        created.Handle = handle;
        return (result, created);
    }

    public async ValueTask DisposeAsync()
    {
        nint watcher = Handle;
        Handle = 0;
        if (watcher != 0)
        {
            // No callback runs after this returns.
            await Native.Run(() => NativeMethods.AndroidWatcherDestroy(watcher)).ConfigureAwait(true);
        }
        if (_self.IsAllocated)
        {
            _self.Free();
        }
    }

    private static unsafe (EngineResult Result, nint Watcher) StartNative(nint context)
    {
        var callbacks = new NativeAndroidCallbacks
        {
            Context = context,
            OnAttached = &OnAttached,
            OnDetached = &OnDetached,
            OnDebuggingOn = &OnDebuggingOn,
            OnLog = &OnLog,
        };
        nint watcher;
        EngineResult started = NativeMethods.AndroidWatcherStart(&callbacks, &watcher);
        return (started, watcher);
    }

    private static EngineAndroidWatcher From(nint context) => (EngineAndroidWatcher)GCHandle.FromIntPtr(context).Target!;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnAttached(nint context, uint accessoryId)
    {
        EngineAndroidWatcher self = From(context);
        self._dispatcher.BeginInvoke(() => self.Attached?.Invoke(accessoryId));
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnDetached(nint context, uint accessoryId)
    {
        EngineAndroidWatcher self = From(context);
        self._dispatcher.BeginInvoke(() => self.Detached?.Invoke(accessoryId));
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnDebuggingOn(nint context)
    {
        EngineAndroidWatcher self = From(context);
        self._dispatcher.BeginInvoke(() => self.DebuggingOn?.Invoke());
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static unsafe void OnLog(nint context, LogLevel level, byte* message) => Log.Write(level, Utf8.Read(message));
}
