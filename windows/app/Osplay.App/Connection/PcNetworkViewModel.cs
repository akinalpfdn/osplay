using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Osplay.App.Services;
using Osplay.App.Settings;
using QRCoder;

namespace Osplay.App.Connection;

/// <summary>
/// The panel's "No router?" card: opens this PC's own network and shows how to join it, as text
/// and as the standard Wi-Fi QR code phone and tablet cameras read (DEVPLAN Phase 15 part TAB-04).
/// </summary>
public sealed partial class PcNetworkViewModel : ObservableObject, IDisposable
{
    private const string _nameAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    private const string _passwordAlphabet = "abcdefghijkmnpqrstuvwxyz23456789";

    private readonly Dispatcher _dispatcher;
    private readonly AppSettings _settings;
    private readonly IPcNetwork _network;

    internal PcNetworkViewModel(Dispatcher dispatcher, AppSettings settings, IPcNetwork network)
    {
        _dispatcher = dispatcher;
        _settings = settings;
        _network = network;
        // Kept across launches, so devices that joined once join again by themselves.
        if (string.IsNullOrEmpty(settings.PcNetworkName) || string.IsNullOrEmpty(settings.PcNetworkPassword))
        {
            settings.PcNetworkName = "Osplay-" + RandomText(_nameAlphabet, AppTunables.PcNetworkNameSuffixLength);
            settings.PcNetworkPassword = RandomText(_passwordAlphabet, AppTunables.PcNetworkPasswordLength);
        }
        _network.StateChanged += state => _dispatcher.BeginInvoke(() => State = state);
    }

    public string Name => _settings.PcNetworkName;

    public string Password => _settings.PcNetworkPassword;

    public string NameText => LocalizedText.Format(Localization.Strings.PcNetworkName, Name);

    public string PasswordText => LocalizedText.Format(Localization.Strings.PcNetworkPassword, Password);

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsOpen), nameof(IsOn), nameof(StatusText), nameof(ActionText))]
    private PcNetworkState _state = PcNetworkState.Off;

    /// <summary>Opening or open: the action closes it.</summary>
    public bool IsOpen => State is PcNetworkState.Starting or PcNetworkState.On;

    public bool IsOn => State == PcNetworkState.On;

    public string ActionText => IsOpen ? Localization.Strings.PcNetworkClose : Localization.Strings.PcNetworkOpen;

    public string StatusText => State switch
    {
        PcNetworkState.Starting => Localization.Strings.PcNetworkStarting,
        PcNetworkState.On => Localization.Strings.PcNetworkOn,
        PcNetworkState.WifiOff => Localization.Strings.PcNetworkWifiOff,
        PcNetworkState.InUse => Localization.Strings.PcNetworkInUse,
        PcNetworkState.NotSupported => Localization.Strings.PcNetworkNotSupported,
        PcNetworkState.Failed => Localization.Strings.PcNetworkFailed,
        _ => Localization.Strings.PcNetworkDetail,
    };

    /// <summary>The Wi-Fi QR code (WIFI:T:WPA;S:…;P:…;;), made once.</summary>
    public ImageSource QrCode => _qrCode ??= MakeQrCode(Name, Password);

    private ImageSource? _qrCode;

    [RelayCommand]
    private void Toggle()
    {
        if (IsOpen)
        {
            _network.Close();
        }
        else
        {
            _network.Open(Name, Password);
        }
    }

    public void Dispose() => _network.Dispose();

    private static string RandomText(string alphabet, int length)
    {
        var text = new StringBuilder(length);
        for (int i = 0; i < length; i++)
        {
            _ = text.Append(alphabet[RandomNumberGenerator.GetInt32(alphabet.Length)]);
        }
        return text.ToString();
    }

    /// <summary>Backslash escapes the characters the Wi-Fi QR format reserves.</summary>
    private static string Escaped(string value)
    {
        var text = new StringBuilder(value.Length);
        foreach (char character in value)
        {
            if (character is '\\' or ';' or ',' or ':' or '"')
            {
                _ = text.Append('\\');
            }
            _ = text.Append(character);
        }
        return text.ToString();
    }

    private static BitmapImage MakeQrCode(string name, string password)
    {
        using var generator = new QRCodeGenerator();
        using QRCodeData data = generator.CreateQrCode($"WIFI:T:WPA;S:{Escaped(name)};P:{Escaped(password)};;", QRCodeGenerator.ECCLevel.M);
        byte[] png = new PngByteQRCode(data).GetGraphic(AppTunables.PcNetworkQrModulePixels);
        var image = new BitmapImage();
        image.BeginInit();
        image.CacheOption = BitmapCacheOption.OnLoad;
        image.StreamSource = new MemoryStream(png);
        image.EndInit();
        image.Freeze();
        return image;
    }
}
