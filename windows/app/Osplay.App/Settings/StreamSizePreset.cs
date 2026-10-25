namespace Osplay.App.Settings;

/// <summary>
/// The largest size a shared monitor is streamed at (DEVPLAN Phase 13c); the values match the Mac's.
/// The virtual monitor keeps the viewer monitor's mode; the stream is fitted inside this box and the
/// viewer scales it up. 1080p is 0 so a settings file without the field reads as the default.
/// </summary>
public enum StreamSizePreset
{
    Hd1080 = 0,
    Hd720 = 1,
    Qhd1440 = 2,
    Native = 3,
}

internal static class StreamSizePresetExtensions
{
    /// <summary>In the order the picker shows them.</summary>
    public static IReadOnlyList<StreamSizePreset> Ordered { get; } =
        [StreamSizePreset.Hd720, StreamSizePreset.Hd1080, StreamSizePreset.Qhd1440, StreamSizePreset.Native];

    /// <summary>Width and height of the box (engine host config); 0 × 0 for the monitor's own size.</summary>
    public static (uint Width, uint Height) Box(this StreamSizePreset preset) => preset switch
    {
        StreamSizePreset.Hd720 => AppTunables.StreamBox720,
        StreamSizePreset.Qhd1440 => AppTunables.StreamBox1440,
        StreamSizePreset.Native => (0, 0),
        _ => AppTunables.StreamBox1080,
    };

    public static string DisplayName(this StreamSizePreset preset) => preset switch
    {
        StreamSizePreset.Hd720 => Localization.Strings.StreamSize720,
        StreamSizePreset.Qhd1440 => Localization.Strings.StreamSize1440,
        StreamSizePreset.Native => Localization.Strings.StreamSizeNative,
        _ => Localization.Strings.StreamSize1080,
    };
}
