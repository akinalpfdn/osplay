using System.Windows;
using System.Windows.Interop;
using H.NotifyIcon;
using Osplay.App.Connection;
using Osplay.App.Diagnostics;
using Osplay.App.TrayPanel;
using Osplay.App.Services;
using Osplay.App.Settings;
using Osplay.App.Tray;
using Wpf.Ui.Appearance;

namespace Osplay.App;

/// <summary>Tray application: a panel from the tray icon, a Settings window, and the pairing dialog.</summary>
public partial class App : Application, IDisposable
{
    private TaskbarIcon? _trayIcon;
    private TrayIconTheme? _trayIconTheme;
    private ConnectionViewModel? _connection;
    private PanelWindow? _panel;
    private SettingsWindow? _settingsWindow;
    private SettingsViewModel? _settingsViewModel;
    private bool _quitting;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        ApplySystemTheme();

        var settings = AppSettings.Load();
        _connection = new ConnectionViewModel(Dispatcher, new PairingWindowPrompt(), settings);
        var updates = new UpdatesViewModel(settings);
        _settingsViewModel = new SettingsViewModel(settings, _connection, updates);
        _panel = new PanelWindow(new PanelViewModel(_connection, updates, settings, OpenSettings, QuitAsync));
        // A window handle now, so the theme follows the system before the panel is first shown.
        _ = new WindowInteropHelper(_panel).EnsureHandle();

        _trayIcon = (TaskbarIcon)FindResource(TrayResourceKeys.TrayIcon);
        _trayIcon.DataContext = new TrayViewModel(_connection, _panel.Toggle, OpenSettings, QuitAsync);
        _trayIconTheme = new TrayIconTheme(_trayIcon);
        _trayIcon.ForceCreate(enablesEfficiencyMode: false);

        _ = StartAsync(updates);
    }

    private async Task StartAsync(UpdatesViewModel updates)
    {
        await _connection!.InitializeAsync().ConfigureAwait(true);
        await updates.CheckIfDueAsync().ConfigureAwait(true);
    }

    private static void ApplySystemTheme()
    {
        ApplicationTheme theme = ApplicationThemeManager.GetSystemTheme() switch
        {
            SystemTheme.Dark or SystemTheme.CapturedMotion or SystemTheme.Glow => ApplicationTheme.Dark,
            SystemTheme.HCBlack or SystemTheme.HCWhite or SystemTheme.HC1 or SystemTheme.HC2 => ApplicationTheme.HighContrast,
            _ => ApplicationTheme.Light,
        };
        ApplicationThemeManager.Apply(theme, Wpf.Ui.Controls.WindowBackdropType.Mica, true);
    }

    private void OpenSettings()
    {
        _panel?.Hide();
        if (_settingsWindow is null)
        {
            _settingsWindow = new SettingsWindow(_settingsViewModel!);
            _settingsWindow.Closed += (_, _) => _settingsWindow = null;
            _settingsWindow.Show();
        }
        if (_settingsWindow.WindowState == WindowState.Minimized)
        {
            _settingsWindow.WindowState = WindowState.Normal;
        }
        _ = _settingsWindow.Activate();
    }

    public void Dispose()
    {
        _trayIconTheme?.Dispose();
        _trayIconTheme = null;
        _trayIcon?.Dispose();
        _trayIcon = null;
        _connection?.Dispose();
        _connection = null;
        GC.SuppressFinalize(this);
    }

    /// <summary>Ends sessions first (the virtual monitor and Bonjour service go away), then exits.</summary>
    private async Task QuitAsync()
    {
        if (_quitting)
        {
            return;
        }
        _quitting = true;
        _panel?.Hide();
        if (_connection is not null)
        {
            await _connection.ShutdownAsync().ConfigureAwait(true);
        }
        Dispose();
        await Log.FlushAsync().ConfigureAwait(true);
        if (_panel is not null)
        {
            _panel.IsQuitting = true;
        }
        Shutdown();
    }
}
