using Osplay.App.Engine;
using Osplay.App.Settings;

namespace Osplay.App.Connection;

/// <summary>Turns the engine's monitors into picker tiles, keeping each one's place on the desktop.</summary>
internal static class MonitorLayout
{
    public static IReadOnlyList<MonitorTile> Tiles(IReadOnlyList<MonitorInfo> monitors, IReadOnlyCollection<string> excluded)
    {
        var tiles = new List<MonitorTile>();
        for (int i = 0; i < monitors.Count; i++)
        {
            MonitorInfo m = monitors[i];
            string key = AppSettings.MonitorKey(m.Name, m.Left, m.Top);
            string name = m.Name.Length > 0
                ? m.Name
                : LocalizedText.Format(Localization.Strings.MonitorFallbackName, i + 1);
            string size = LocalizedText.Format(Localization.Strings.MonitorSize, m.WidthPixels, m.HeightPixels);
            tiles.Add(new MonitorTile(m.Id, key, name, size)
            {
                Left = m.Left,
                Top = m.Top,
                Width = m.Right - m.Left,
                Height = m.Bottom - m.Top,
                IsSelected = !excluded.Contains(key),
            });
        }
        return tiles;
    }
}
