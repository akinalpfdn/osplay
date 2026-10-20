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

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasHotkeyMessage))]
    private string? _hotkeyMessage;

    public bool HasHotkeyMessage => HotkeyMessage is not null;

    /// <summary>The viewer's hide/show shortcut; refused when it is the swap shortcut.</summary>
    public HotkeyGesture ViewerHotkey
    {
        get => Settings.ViewerHotkey;
        set => Take(value, Settings.SwapHotkey, gesture => Settings.ViewerHotkey = gesture, nameof(ViewerHotkey));
    }

    /// <summary>The role swap shortcut; refused when it is the viewer's.</summary>
    public HotkeyGesture SwapHotkey
    {
        get => Settings.SwapHotkey;
        set => Take(value, Settings.ViewerHotkey, gesture => Settings.SwapHotkey = gesture, nameof(SwapHotkey));
    }

    [RelayCommand]
    private void ResetHotkey() => ViewerHotkey = HotkeyGesture.Default;

    [RelayCommand]
    private void ResetSwapHotkey() => SwapHotkey = HotkeyGesture.DefaultSwap;

    private void Take(HotkeyGesture gesture, HotkeyGesture other, Action<HotkeyGesture> store, string property)
    {
        if (gesture == other)
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

    [RelayCommand]
    private void OpenNetworkSettings() => Shell.Open(NetworkSettings);

    private Uri NetworkSettings { get; } = AppTunables.NetworkSettingsUri;
}
