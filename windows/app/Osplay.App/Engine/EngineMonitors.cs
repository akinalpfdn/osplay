using Osplay.App.Interop;

namespace Osplay.App.Engine;

/// <summary>A physical monitor this PC can offer as a display, where it stands on the desktop.</summary>
internal sealed record MonitorInfo(
    byte Id, string Name, int WidthPixels, int HeightPixels, uint RefreshMillihertz, int Left, int Top, int Right,
    int Bottom);

/// <summary>The monitors the engine would offer to a host (never the virtual display driver's).</summary>
internal static unsafe class EngineMonitors
{
    public static Task<IReadOnlyList<MonitorInfo>> ListAsync() => Native.Run<IReadOnlyList<MonitorInfo>>(() =>
    {
        var buffer = new NativeMonitor[NativeSizes.MaxMonitors];
        nuint count;
        fixed (NativeMonitor* monitors = buffer)
        {
            if (NativeMethods.ListMonitors(monitors, (nuint)buffer.Length, &count) != EngineResult.Ok)
            {
                return [];
            }
            var result = new List<MonitorInfo>();
            for (int i = 0; i < (int)Math.Min(count, (nuint)buffer.Length); i++)
            {
                NativeMonitor* m = &monitors[i];
                result.Add(new MonitorInfo(
                    m->MonitorId, Utf8.Read(m->Name, NativeSizes.NameSize), m->WidthPixels, m->HeightPixels,
                    m->RefreshMillihertz, m->Left, m->Top, m->Right, m->Bottom));
            }
            return result;
        }
    });
}
