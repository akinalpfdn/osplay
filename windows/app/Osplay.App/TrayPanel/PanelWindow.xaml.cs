using System.Windows;
using System.Windows.Input;
using Wpf.Ui.Appearance;

namespace Osplay.App.TrayPanel;

/// <summary>
/// The panel the tray icon opens, in the corner above the taskbar; it closes when it loses focus,
/// like the Mac's menu bar window.
/// </summary>
public partial class PanelWindow : Wpf.Ui.Controls.FluentWindow
{
    private DateTime _hiddenAt = DateTime.MinValue;

    public PanelWindow(PanelViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
        SystemThemeWatcher.Watch(this);
        SizeChanged += (_, _) => PlaceAboveTaskbar();
    }

    /// <summary>Shows the panel, or hides it when it is already open (a second tray click).</summary>
    public void Toggle()
    {
        if (IsVisible)
        {
            Hide();
            return;
        }
        if (DateTime.UtcNow - _hiddenAt < AppTunables.PanelReopenGuard)
        {
            return; // the tray click that took focus away already closed it
        }
        Show();
        PlaceAboveTaskbar();
        _ = Activate();
    }

    protected override void OnDeactivated(EventArgs e)
    {
        base.OnDeactivated(e);
        _hiddenAt = DateTime.UtcNow;
        Hide();
    }

    protected override void OnPreviewKeyDown(KeyEventArgs e)
    {
        base.OnPreviewKeyDown(e);
        if (e.Key == Key.Escape)
        {
            Hide();
            e.Handled = true;
        }
    }

    /// <summary>Set when the app quits; until then closing only hides the panel.</summary>
    internal bool IsQuitting { get; set; }

    protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
    {
        if (!IsQuitting)
        {
            // Alt+F4 only hides it: the panel lives as long as the tray icon.
            e.Cancel = true;
            Hide();
        }
        base.OnClosing(e);
    }

    private void PlaceAboveTaskbar()
    {
        Rect area = SystemParameters.WorkArea; // the primary monitor without the taskbar
        double margin = (double)FindResource("Osplay.Panel.EdgeMargin");
        Left = area.Right - ActualWidth - margin;
        Top = area.Bottom - ActualHeight - margin;
    }
}
