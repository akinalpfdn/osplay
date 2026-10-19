using System.Windows;
using System.Windows.Input;
using Osplay.App.Settings;

namespace Osplay.App.Controls;

/// <summary>Click, then press a combination: records a global hotkey. Escape cancels.</summary>
public partial class HotkeyRecorder
{
    public static readonly DependencyProperty GestureProperty = DependencyProperty.Register(
        nameof(Gesture), typeof(HotkeyGesture), typeof(HotkeyRecorder),
        new FrameworkPropertyMetadata(
            HotkeyGesture.Default, FrameworkPropertyMetadataOptions.BindsTwoWayByDefault,
            (d, _) => ((HotkeyRecorder)d).ShowGesture()));

    private bool _recording;

    public HotkeyRecorder()
    {
        InitializeComponent();
        ShowGesture();
    }

    public HotkeyGesture Gesture
    {
        get => (HotkeyGesture)GetValue(GestureProperty);
        set => SetValue(GestureProperty, value);
    }

    private void OnClick(object sender, RoutedEventArgs e)
    {
        _recording = true;
        Recorder.Content = Localization.Strings.HotkeyRecording;
        Hint.Text = Localization.Strings.HotkeyRecordHint;
    }

    private void OnPreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (!_recording)
        {
            return;
        }
        e.Handled = true;
        Key key = e.Key == Key.System ? e.SystemKey : e.Key;
        if (key == Key.Escape)
        {
            StopRecording();
            return;
        }
        var gesture = HotkeyGesture.From(key, Keyboard.Modifiers);
        if (gesture is not null)
        {
            Gesture = gesture;
            StopRecording();
        }
        else if (key is not (Key.LeftCtrl or Key.RightCtrl or Key.LeftAlt or Key.RightAlt or Key.LeftShift
            or Key.RightShift or Key.LWin or Key.RWin))
        {
            Hint.Text = Localization.Strings.HotkeyNeedsModifier;
        }
    }

    private void OnLostFocus(object sender, KeyboardFocusChangedEventArgs e) => StopRecording();

    private void StopRecording()
    {
        _recording = false;
        Hint.Text = Localization.Strings.HotkeyRecordHint;
        ShowGesture();
    }

    private void ShowGesture()
    {
        if (Recorder is not null && !_recording)
        {
            Recorder.Content = Gesture.DisplayText;
        }
    }
}
