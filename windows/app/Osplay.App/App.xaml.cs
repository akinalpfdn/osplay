using System.Windows;
using H.NotifyIcon;
using Osplay.App.Connection;
using Osplay.App.Tray;

namespace Osplay.App;

/// <summary>Tray application; Phase 12 adds the main window.</summary>
public partial class App : Application, IDisposable
{
    private TaskbarIcon? _trayIcon;
    private ConnectionViewModel? _connection;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        _connection = new ConnectionViewModel(Dispatcher, new PairingWindowPrompt());
        _trayIcon = (TaskbarIcon)FindResource(TrayResourceKeys.TrayIcon);
        _trayIcon.DataContext = new TrayViewModel(_connection, Shutdown);
        _trayIcon.ForceCreate(enablesEfficiencyMode: false);
    }

    public void Dispose()
    {
        _connection?.Dispose(); // ends sessions and withdraws the Bonjour service
        _connection = null;
        _trayIcon?.Dispose();
        _trayIcon = null;
        GC.SuppressFinalize(this);
    }

    protected override void OnExit(ExitEventArgs e)
    {
        Dispose();
        base.OnExit(e);
    }
}
