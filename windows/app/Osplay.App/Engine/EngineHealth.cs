using Osplay.App.Interop;

namespace Osplay.App.Engine;

/// <summary>Whether this PC can share its screen: the helper service and the virtual display driver.</summary>
public enum HostHealth
{
    Ready,
    /// <summary>The OsplayHelper service is not running or not installed.</summary>
    HelperMissing,
    /// <summary>The helper answered but refused this app (another build, permissions).</summary>
    HelperRefused,
    /// <summary>The Virtual Display Driver is not installed.</summary>
    DriverMissing,
    /// <summary>A session is using the helper right now, so it could not be checked.</summary>
    InUse,
}

internal static unsafe class EngineHealth
{
    /// <summary>Connects to the helper and asks for the driver's state; adds no monitor.</summary>
    public static Task<HostHealth> CheckAsync() => Native.Run(() =>
    {
        nint display;
        EngineResult opened = NativeMethods.VirtualDisplayOpen(&display);
        if (opened != EngineResult.Ok)
        {
            return FromResult(opened);
        }
        try
        {
            NativeVirtualMonitorStatus status;
            EngineResult result = NativeMethods.VirtualDisplayStatus(display, &status);
            if (result != EngineResult.Ok)
            {
                return FromResult(result);
            }
            return status.DriverInstalled == 1 ? HostHealth.Ready : HostHealth.DriverMissing;
        }
        finally
        {
            NativeMethods.VirtualDisplayClose(display); // no monitor was added, none is removed
        }
    });

    /// <summary>The health problem behind an engine result, or null when it is not one.</summary>
    public static HostHealth? FromFailure(EngineResult result) => result switch
    {
        EngineResult.HelperUnavailable or EngineResult.HelperAccessDenied or EngineResult.HelperNotTrusted
            or EngineResult.ProtocolMismatch or EngineResult.VddNotInstalled => FromResult(result),
        _ => null,
    };

    private static HostHealth FromResult(EngineResult result) => result switch
    {
        EngineResult.HelperUnavailable => HostHealth.HelperMissing,
        EngineResult.HelperBusy => HostHealth.InUse,
        EngineResult.VddNotInstalled => HostHealth.DriverMissing,
        _ => HostHealth.HelperRefused,
    };
}
