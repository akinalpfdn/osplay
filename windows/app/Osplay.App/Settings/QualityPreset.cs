namespace Osplay.App.Settings;

/// <summary>Stream quality when this PC shares its screen; the values match the Mac's presets.</summary>
public enum QualityPreset
{
    Balanced,
    High,
    Ultra,
}

internal static class QualityPresetExtensions
{
    /// <summary>Target bits per pixel per frame, in thousandths (engine host config).</summary>
    public static uint MillibitsPerPixel(this QualityPreset preset) => preset switch
    {
        QualityPreset.Balanced => 60,
        QualityPreset.Ultra => 160,
        _ => 100,
    };

    public static string DisplayName(this QualityPreset preset) => preset switch
    {
        QualityPreset.Balanced => Localization.Strings.QualityBalanced,
        QualityPreset.Ultra => Localization.Strings.QualityUltra,
        _ => Localization.Strings.QualityHigh,
    };
}
