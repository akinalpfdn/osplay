using Osplay.App.Interop;

namespace Osplay.App.Connection;

/// <summary>Localized, actionable text for engine and core results.</summary>
internal static class ErrorMessages
{
    public static string For(EngineResult result) => result switch
    {
        EngineResult.HelperUnavailable or EngineResult.HelperAccessDenied or EngineResult.HelperNotTrusted
            or EngineResult.ProtocolMismatch or EngineResult.HelperBusy => Localization.Strings.ErrorHelper,
        EngineResult.VddNotInstalled => Localization.Strings.ErrorVddMissing,
        EngineResult.KeyStoreUnavailable => Localization.Strings.ErrorKeyStoreUnavailable,
        EngineResult.KeyStoreDamaged => Localization.Strings.ErrorKeyStoreDamaged,
        EngineResult.DiscoveryUnavailable => Localization.Strings.ErrorDiscovery,
        EngineResult.MonitorNotFound => Localization.Strings.ErrorMonitorNotFound,
        EngineResult.EncoderUnavailable or EngineResult.EncoderFailed => Localization.Strings.ErrorEncoder,
        EngineResult.DecoderUnavailable => Localization.Strings.ErrorDecoder,
        EngineResult.PeerTooOld => Localization.Strings.ErrorSwapUnsupported,
        _ => LocalizedText.Format(Localization.Strings.ErrorEngine, (int)result),
    };

    public static string For(CoreResult result) => result switch
    {
        CoreResult.NotTrusted => Localization.Strings.ErrorNotTrusted,
        CoreResult.PairingRejected => Localization.Strings.ErrorPairingRejected,
        CoreResult.UnsupportedVersion => Localization.Strings.ErrorIncompatible,
        CoreResult.PeerBusy => Localization.Strings.ErrorPeerBusy,
        CoreResult.TimedOut or CoreResult.Network or CoreResult.ConnectionClosed => Localization.Strings.ErrorUnreachable,
        _ => LocalizedText.Format(Localization.Strings.ErrorSession, (int)result),
    };
}
