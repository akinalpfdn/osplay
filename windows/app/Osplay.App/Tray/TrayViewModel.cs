using CommunityToolkit.Mvvm.Input;
using Osplay.App.Connection;

namespace Osplay.App.Tray;

/// <summary>What the tray icon does: a click opens the panel; its menu has the quick actions.</summary>
public sealed partial class TrayViewModel(
    ConnectionViewModel connection, Action togglePanel, Action openSettings, Func<Task> quit)
{
    public ConnectionViewModel Connection { get; } = connection;

    [RelayCommand]
    private void TogglePanel() => togglePanel();

    [RelayCommand]
    private void OpenSettings() => openSettings();

    [RelayCommand]
    private Task QuitAsync() => quit();
}
