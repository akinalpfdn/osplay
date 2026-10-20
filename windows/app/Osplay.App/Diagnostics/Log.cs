using System.Globalization;
using System.IO;
using System.Text;
using System.Threading.Channels;
using Osplay.App.Interop;

namespace Osplay.App.Diagnostics;

/// <summary>
/// The shell's and the engine's log lines in %LOCALAPPDATA%\Osplay\logs\osplay.log, rolled over at
/// <see cref="AppTunables.LogFileLimitBytes"/>. Writing never blocks the caller (engine callbacks
/// must return quickly); a background task appends.
/// </summary>
internal static class Log
{
    private const string _fileName = "osplay.log";
    private const string _previousFileName = "osplay.1.log";

    private static readonly Channel<string> _lines = Channel.CreateUnbounded<string>(
        new UnboundedChannelOptions { SingleReader = true });

    private static readonly Task _writer = Task.Run(WriteLinesAsync);

    public static string Folder { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Osplay", "logs");

    public static void Write(LogLevel level, string message)
    {
        string line = string.Create(
            CultureInfo.InvariantCulture, $"{DateTimeOffset.UtcNow:O} {level} {message}");
        _ = _lines.Writer.TryWrite(line);
    }

    /// <summary>Writes what is queued; called when the app exits.</summary>
    public static async Task FlushAsync()
    {
        _ = _lines.Writer.TryComplete();
        await _writer.ConfigureAwait(false);
    }

    private static async Task WriteLinesAsync()
    {
        await foreach (string line in _lines.Reader.ReadAllAsync().ConfigureAwait(false))
        {
            try
            {
                _ = Directory.CreateDirectory(Folder);
                string path = Path.Combine(Folder, _fileName);
                var file = new FileInfo(path);
                if (file.Exists && file.Length > AppTunables.LogFileLimitBytes)
                {
                    File.Move(path, Path.Combine(Folder, _previousFileName), overwrite: true);
                }
                await File.AppendAllTextAsync(path, line + Environment.NewLine, Encoding.UTF8).ConfigureAwait(false);
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException)
            {
                // Logging must never take the app down; a line that cannot be written is dropped.
                System.Diagnostics.Debug.WriteLine($"Osplay log write failed: {error.Message}");
            }
        }
    }
}
