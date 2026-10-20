namespace Osplay.App;

/// <summary>Every tunable of the shell in one place (DEVPLAN: no buried constants).</summary>
internal static class AppTunables
{
    /// <summary>A host session takes one viewer; the next listener starts after this pause.</summary>
    public static readonly TimeSpan RelistenDelay = TimeSpan.FromSeconds(1);

    /// <summary>After a role swap, how long viewing waits for the other computer to advertise itself.</summary>
    public static readonly TimeSpan SwapReconnectTimeout = TimeSpan.FromSeconds(20);

    /// <summary>The log file is rolled over at this size; one older file is kept.</summary>
    public const long LogFileLimitBytes = 5 * 1024 * 1024;

    /// <summary>GitHub's latest release of the repository (no Osplay server exists).</summary>
    public static readonly Uri LatestReleaseUri = new("https://api.github.com/repos/akinalpfdn/osplay/releases/latest");

    /// <summary>Releases page opened when no release could be read.</summary>
    public static readonly Uri ReleasesPageUri = new("https://github.com/akinalpfdn/osplay/releases");

    /// <summary>Automatic update checks happen at most this often, at launch.</summary>
    public static readonly TimeSpan UpdateCheckInterval = TimeSpan.FromDays(1);

    public static readonly TimeSpan UpdateCheckTimeout = TimeSpan.FromSeconds(15);

    /// <summary>Windows network settings, where a network is made private.</summary>
    public static readonly Uri NetworkSettingsUri = new("ms-settings:network-status");

    /// <summary>A tray click right after the panel lost focus to it closes the panel instead of reopening it.</summary>
    public static readonly TimeSpan PanelReopenGuard = TimeSpan.FromMilliseconds(300);

    /// <summary>Copy diagnostics includes this many of the newest log lines.</summary>
    public const int DiagnosticsLogLines = 300;

    /// <summary>The paired devices the engine can store.</summary>
    public const int MaxPairedDevices = 256;
}
