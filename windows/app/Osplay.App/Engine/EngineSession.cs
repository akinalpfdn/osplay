using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Windows.Threading;
using Osplay.App.Interop;

namespace Osplay.App.Engine;

/// <summary>
/// One host or viewer session in the engine, started with the stored identity (the engine answers
/// trust and stores pairings). Events arrive on the dispatcher's thread; dispose on that thread
/// too, after <see cref="Closed"/> or to abandon the session.
/// </summary>
internal abstract unsafe class EngineSession : IDisposable
{
    private readonly Dispatcher _dispatcher;
    private GCHandle _self;
    private string _peerName = string.Empty;

    protected EngineSession(Dispatcher dispatcher)
    {
        _dispatcher = dispatcher;
        _self = GCHandle.Alloc(this);
    }

    /// <summary>The engine handle; 0 before start and after dispose.</summary>
    protected nint Handle { get; set; }

    protected nint Context => GCHandle.ToIntPtr(_self);

    public string PeerName => _peerName;

    public event Action<SessionState>? StateChanged;
    public event Action<uint>? PairingCode;
    public event Action? Paired;
    /// <summary>The session ended; the reason is the core's (Ok for an orderly close).</summary>
    public event Action<CoreResult>? Closed;
    /// <summary>The engine itself failed (helper, capture...); Closed follows.</summary>
    public event Action<EngineResult>? Failed;

    /// <summary>Viewer: one stream could not be shown; the session continues.</summary>
    public event Action<EngineResult>? StreamFailed;

    public abstract void ConfirmPairing(bool accepted);

    public abstract void Stop();

    public void Dispose()
    {
        if (Handle != 0)
        {
            Destroy(Handle); // joins the engine's threads; no callback runs afterwards
            Handle = 0;
        }
        if (_self.IsAllocated)
        {
            _self.Free();
        }
    }

    protected abstract void Destroy(nint handle);

    protected void Post(Action action) => _dispatcher.BeginInvoke(action);

    protected static EngineSession From(nint context) => (EngineSession)GCHandle.FromIntPtr(context).Target!;

    protected void RaiseStreamFailed(EngineResult reason) => StreamFailed?.Invoke(reason);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    protected static void OnState(nint context, SessionState state, CoreResult reason)
    {
        var self = From(context);
        self.Post(() =>
        {
            self.StateChanged?.Invoke(state);
            if (state == SessionState.Closed)
            {
                self.Closed?.Invoke(reason);
            }
        });
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    protected static void OnPairingCode(nint context, uint code, byte* key)
    {
        var self = From(context);
        self.Post(() => self.PairingCode?.Invoke(code));
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    protected static void OnPaired(nint context, byte* key)
    {
        var self = From(context);
        self.Post(() => self.Paired?.Invoke());
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    protected static void OnFailed(nint context, EngineResult reason)
    {
        var self = From(context);
        self.Post(() => self.Failed?.Invoke(reason));
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    protected static void OnPeer(nint context, Platform platform, byte* name)
    {
        var self = From(context);
        string text = Utf8.Read(name);
        self.Post(() => self._peerName = text);
    }
}

/// <summary>Windows shares its screen: a virtual monitor streamed to one viewer.</summary>
internal sealed unsafe class EngineHost : EngineSession
{
    private EngineHost(Dispatcher dispatcher)
        : base(dispatcher)
    {
    }

    /// <summary>The TCP port to advertise.</summary>
    public ushort ControlPort { get; private set; }

    public static EngineResult Start(Dispatcher dispatcher, string deviceName, out EngineHost? host)
    {
        var created = new EngineHost(dispatcher);
        nint name = Utf8.Allocate(deviceName);
        try
        {
            var config = new NativeHostConfig { SecretKey = 0, DeviceName = name, DumpDirectory = 0 };
            var callbacks = new NativeHostCallbacks
            {
                Context = created.Context,
                OnState = &OnState,
                OnTrustQuery = null, // the stored identity: the engine answers trust
                OnPairingCode = &OnPairingCode,
                OnPaired = &OnPaired,
                OnStreaming = null,
                OnFailed = &OnFailed,
                OnStats = 0,
                OnLog = 0,
                OnPeer = &OnPeer,
            };
            nint handle;
            EngineResult result = NativeMethods.HostStart(&config, &callbacks, &handle);
            if (result != EngineResult.Ok)
            {
                created.Dispose();
                host = null;
                return result;
            }
            created.Handle = handle;
            ushort port;
            _ = NativeMethods.HostControlPort(handle, &port);
            created.ControlPort = port;
            host = created;
            return result;
        }
        finally
        {
            Utf8.Free(name);
        }
    }

    public override void ConfirmPairing(bool accepted) =>
        _ = NativeMethods.HostConfirmPairing(Handle, accepted ? (byte)1 : (byte)0);

    public override void Stop() => _ = NativeMethods.HostStop(Handle);

    protected override void Destroy(nint handle) => NativeMethods.HostDestroy(handle);
}

/// <summary>Windows shows a remote screen on its monitors.</summary>
internal sealed unsafe class EngineViewer : EngineSession
{
    private EngineViewer(Dispatcher dispatcher)
        : base(dispatcher)
    {
    }

    public bool HotkeyRegistered { get; private set; }

    public static EngineResult Start(
        Dispatcher dispatcher, string deviceName, string address, ushort port, out EngineViewer? viewer)
    {
        var created = new EngineViewer(dispatcher);
        nint name = Utf8.Allocate(deviceName);
        nint host = Utf8.Allocate(address);
        try
        {
            var config = new NativeViewerConfig
            {
                SecretKey = 0,
                DeviceName = name,
                Host = host,
                Port = port,
                MonitorIds = 0,
                MonitorIdCount = 0,
            };
            var callbacks = new NativeViewerCallbacks
            {
                Context = created.Context,
                OnState = &OnState,
                OnTrustQuery = null, // the stored identity: the engine answers trust
                OnPairingCode = &OnPairingCode,
                OnPaired = &OnPaired,
                OnVisibility = null,
                OnStreamFailed = &OnStreamFailed,
                OnStats = 0,
                OnLog = 0,
                OnPeer = &OnPeer,
            };
            nint handle;
            EngineResult result = NativeMethods.ViewerStart(&config, &callbacks, &handle);
            if (result != EngineResult.Ok)
            {
                created.Dispose();
                viewer = null;
                return result;
            }
            created.Handle = handle;
            byte registered;
            _ = NativeMethods.ViewerHotkeyRegistered(handle, &registered);
            created.HotkeyRegistered = registered == 1;
            viewer = created;
            return result;
        }
        finally
        {
            Utf8.Free(name);
            Utf8.Free(host);
        }
    }

    public override void ConfirmPairing(bool accepted) =>
        _ = NativeMethods.ViewerConfirmPairing(Handle, accepted ? (byte)1 : (byte)0);

    public override void Stop() => _ = NativeMethods.ViewerStop(Handle);

    protected override void Destroy(nint handle) => NativeMethods.ViewerDestroy(handle);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void OnStreamFailed(nint context, byte streamId, EngineResult reason)
    {
        var self = (EngineViewer)From(context);
        self.Post(() => self.RaiseStreamFailed(reason));
    }
}
