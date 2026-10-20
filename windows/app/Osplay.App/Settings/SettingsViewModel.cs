using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Osplay.App.Connection;
using Osplay.App.Diagnostics;
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

    [RelayCommand]
    private void ResetHotkey() => Settings.ViewerHotkey = HotkeyGesture.Default;

    [RelayCommand]
    private void OpenLogs() => Shell.OpenFolder(LogsFolder);

    [RelayCommand]
    private void OpenNetworkSettings() => Shell.Open(NetworkSettings);

    private Uri NetworkSettings { get; } = AppTunables.NetworkSettingsUri;
}
