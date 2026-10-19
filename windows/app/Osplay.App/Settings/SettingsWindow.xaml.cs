using Wpf.Ui.Appearance;

namespace Osplay.App.Settings;

/// <summary>The Settings window; one instance, brought to the front when opened again.</summary>
public partial class SettingsWindow
{
    public SettingsWindow(SettingsViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
        SystemThemeWatcher.Watch(this);
    }
}
