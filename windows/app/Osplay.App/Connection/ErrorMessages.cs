using Osplay.App.Interop;
using Osplay.Localization;

namespace Osplay.App.Connection;

/// <summary>Localized, actionable text for engine and core results.</summary>
internal static class ErrorMessages
{
    public static string For(EngineResult result) => result switch
    {
        EngineResult.HelperUnavailable or EngineResult.HelperAccessDenied or EngineResult.HelperNotTrusted
            or EngineResult.ProtocolMismatch or EngineResult.HelperBusy => Strings.ErrorHelper,
        EngineResult.VddNotInstalled => Strings.ErrorVddMissing,
        EngineResult.KeyStoreUnavailable => Strings.ErrorKeyStoreUnavailable,
        EngineResult.KeyStoreDamaged => Strings.ErrorKeyStoreDamaged,
        EngineResult.DiscoveryUnavailable => Strings.ErrorDiscovery,
        EngineResult.MonitorNotFound => Strings.ErrorMonitorNotFound,
        EngineResult.EncoderUnavailable or EngineResult.EncoderFailed => Strings.ErrorEncoder,
        EngineResult.DecoderUnavailable => Strings.ErrorDecoder,
        _ => LocalizedText.Format(Strings.ErrorEngine, (int)result),
    };

    public static string For(CoreResult result) => result switch
    {
        CoreResult.NotTrusted => Strings.ErrorNotTrusted,
        CoreResult.PairingRejected => Strings.ErrorPairingRejected,
        CoreResult.UnsupportedVersion => Strings.ErrorIncompatible,
        CoreResult.PeerBusy => Strings.ErrorPeerBusy,
        CoreResult.TimedOut or CoreResult.Network or CoreResult.ConnectionClosed => Strings.ErrorUnreachable,
        _ => LocalizedText.Format(Strings.ErrorSession, (int)result),
    };
}
