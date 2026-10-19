using CommunityToolkit.Mvvm.Input;
using Osplay.App.Connection;

namespace Osplay.App.Tray;

/// <summary>What the tray icon's context menu shows and does.</summary>
public sealed partial class TrayViewModel(ConnectionViewModel connection, Action shutdown)
{
    private readonly Action _shutdown = shutdown;

    public ConnectionViewModel Connection { get; } = connection;

    [RelayCommand]
    private void Quit() => _shutdown();
}
