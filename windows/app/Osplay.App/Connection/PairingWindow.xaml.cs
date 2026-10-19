using System.Globalization;
using System.Windows;
using Osplay.Localization;

namespace Osplay.App.Connection;

/// <summary>The pairing code dialog; closing it without a choice rejects.</summary>
public partial class PairingWindow : Window
{
    private Action<bool>? _decide;

    public PairingWindow()
    {
        InitializeComponent();
    }

    internal void Present(uint code, string peerName, Action<bool> decide)
    {
        _decide = decide;
        Message.Text = LocalizedText.Format(Strings.PairingMessage, peerName);
        string digits = code.ToString("D6", CultureInfo.InvariantCulture);
        Code.Text = $"{digits[..3]} {digits[3..]}";
        Show();
        Activate();
    }

    /// <summary>Closes without reporting a decision (the session ended).</summary>
    internal void Dismiss()
    {
        _decide = null;
        Close();
    }

    protected override void OnClosed(EventArgs e)
    {
        Decide(false);
        base.OnClosed(e);
    }

    private void OnAccept(object sender, RoutedEventArgs e)
    {
        Decide(true);
        Close();
    }

    private void OnReject(object sender, RoutedEventArgs e) => Close();

    private void Decide(bool accepted)
    {
        Action<bool>? decide = _decide;
        _decide = null;
        decide?.Invoke(accepted);
    }
}

/// <summary>Opens one <see cref="PairingWindow"/> at a time.</summary>
internal sealed class PairingWindowPrompt : IPairingPrompt
{
    private PairingWindow? _window;

    public void Show(uint code, string peerName, Action<bool> decide)
    {
        Close();
        _window = new PairingWindow();
        _window.Present(code, peerName, decide);
    }

    public void Close()
    {
        _window?.Dismiss();
        _window = null;
    }
}
