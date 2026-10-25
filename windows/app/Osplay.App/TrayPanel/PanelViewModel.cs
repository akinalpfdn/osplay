using CommunityToolkit.Mvvm.Input;
using Osplay.App.Connection;
using Osplay.App.Services;
using Osplay.App.Settings;

namespace Osplay.App.TrayPanel;

/// <summary>A quality preset as the picker shows it.</summary>
public sealed record QualityOption(QualityPreset Preset, string Name);

/// <summary>A stream size as the picker shows it.</summary>
public sealed record StreamSizeOption(StreamSizePreset Preset, string Name);

/// <summary>The tray panel: roles, devices, monitors and quality (the Mac menu's counterpart).</summary>
public sealed partial class PanelViewModel(
    ConnectionViewModel connection, UpdatesViewModel updates, AppSettings settings, Action openSettings,
    Func<Task> quit)
{
    public ConnectionViewModel Connection { get; } = connection;

    public UpdatesViewModel Updates { get; } = updates;

    public AppSettings Settings { get; } = settings;

    public IReadOnlyList<QualityOption> QualityOptions { get; } =
        [.. Enum.GetValues<QualityPreset>().Select(preset => new QualityOption(preset, preset.DisplayName()))];

    public IReadOnlyList<StreamSizeOption> StreamSizeOptions { get; } =
        [.. StreamSizePresetExtensions.Ordered.Select(preset => new StreamSizeOption(preset, preset.DisplayName()))];

    [RelayCommand]
    private void OpenSettings() => openSettings();

    [RelayCommand]
    private Task QuitAsync() => quit();
}
