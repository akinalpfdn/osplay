using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Osplay.App.Connection;
using Osplay.App.Diagnostics;
using Osplay.App.Engine;
using Osplay.App.Interop;
using Osplay.App.Services;

namespace Osplay.App.Settings;

/// <summary>The Settings window: general options, paired devices, and diagnostics.</summary>
public sealed partial class SettingsViewModel : ObservableObject
{
    public SettingsViewModel(AppSettings settings, ConnectionViewModel connection, UpdatesViewModel updates)
    {
        Settings = settings;
        Connection = connection;
        Updates = updates;
        _startsWithWindows = StartupRegistration.IsEnabled;
    }

    public AppSettings Settings { get; }

    public ConnectionViewModel Connection { get; }

    public UpdatesViewModel Updates { get; }

    [ObservableProperty]
    private bool _startsWithWindows;

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasStartupError))]
    private string? _startupError;

    public bool HasStartupError => StartupError is not null;

    public string LogsFolder { get; } = Log.Folder;

    partial void OnStartsWithWindowsChanged(bool value)
    {
        if (StartupRegistration.SetEnabled(value))
        {
            StartupError = null;
            return;
        }
        StartupError = Localization.Strings.SettingsStartupFailed;
        StartsWithWindows = StartupRegistration.IsEnabled;
    }

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasHotkeyMessage))]
    private string? _hotkeyMessage;

    public bool HasHotkeyMessage => HotkeyMessage is not null;

    /// <summary>The viewer's hide/show shortcut; refused when it is the swap shortcut.</summary>
    public HotkeyGesture ViewerHotkey
    {
        get => Settings.ViewerHotkey;
        set => Take(value, [Settings.SwapHotkey, Settings.StatsHotkey], gesture => Settings.ViewerHotkey = gesture,
            nameof(ViewerHotkey));
    }

    /// <summary>The role swap shortcut; refused when it is the viewer's.</summary>
    public HotkeyGesture SwapHotkey
    {
        get => Settings.SwapHotkey;
        set => Take(value, [Settings.ViewerHotkey, Settings.StatsHotkey], gesture => Settings.SwapHotkey = gesture,
            nameof(SwapHotkey));
    }

    /// <summary>The stats overlay shortcut; refused when it is one of the others.</summary>
    public HotkeyGesture StatsHotkey
    {
        get => Settings.StatsHotkey;
        set => Take(value, [Settings.ViewerHotkey, Settings.SwapHotkey], gesture => Settings.StatsHotkey = gesture,
            nameof(StatsHotkey));
    }

    [RelayCommand]
    private void ResetStatsHotkey() => StatsHotkey = HotkeyGesture.DefaultStats;

    [RelayCommand]
    private void ResetHotkey() => ViewerHotkey = HotkeyGesture.Default;

    [RelayCommand]
    private void ResetSwapHotkey() => SwapHotkey = HotkeyGesture.DefaultSwap;

    private void Take(
        HotkeyGesture gesture, IReadOnlyList<HotkeyGesture> others, Action<HotkeyGesture> store, string property)
    {
        if (others.Contains(gesture))
        {
            HotkeyMessage = LocalizedText.Format(Localization.Strings.HotkeyDuplicate, gesture.DisplayText);
        }
        else
        {
            store(gesture);
            HotkeyMessage = null;
        }
        OnPropertyChanged(property); // the recorder shows the stored value again after a refusal
    }

    [RelayCommand]
    private void OpenLogs() => Shell.OpenFolder(LogsFolder);

    [ObservableProperty]
    private string _diagnosticsResult = Localization.Strings.DiagnosticsDescription;

    /// <summary>Copies versions, settings, state and the recent log; names and addresses are replaced.</summary>
    [RelayCommand]
    private async Task CopyDiagnosticsAsync()
    {
        string coreVersion = await Native.Run(() =>
        {
            NativeVersion version = NativeMethods.CoreVersion();
            return $"{version.Major}.{version.Minor}.{version.Patch}";
        }).ConfigureAwait(true);
        IReadOnlyList<string> lines = await Task.Run(() => DiagnosticsReport.RecentLogLines(AppTunables.DiagnosticsLogLines))
            .ConfigureAwait(true);
        var report = new DiagnosticsReport(
            UpdateChecker.CurrentVersion.ToString(3), coreVersion, System.Runtime.InteropServices.RuntimeInformation.OSDescription,
            [
                ("Quality", Settings.Quality.ToString()),
                ("Stream size", Settings.StreamSize.ToString()),
                ("Stats overlay", Settings.ShowsStatsOverlay.ToString()),
                ("Start with Windows", StartsWithWindows.ToString()),
                ("Paired devices", Connection.Peers.Count.ToString(System.Globalization.CultureInfo.InvariantCulture)),
                ("Mode", Connection.Mode.ToString()),
                ("Sharing health", Connection.HostProblem?.ToString() ?? "Ready"),
                ("Public network", Connection.IsNetworkPublic.ToString()),
            ],
            lines,
            [Environment.MachineName, System.Net.Dns.GetHostName(), .. Connection.Peers.Select(p => p.Name)]);
        try
        {
            System.Windows.Clipboard.SetText(report.Text);
            DiagnosticsResult = Localization.Strings.DiagnosticsCopied;
        }
        catch (System.Runtime.InteropServices.ExternalException)
        {
            DiagnosticsResult = Localization.Strings.DiagnosticsFailed; // another program holds the clipboard
        }
    }
}
