namespace Osplay.App.Connection;

/// <summary>Shows the six-digit pairing code with accept and reject (Phase 12 builds the final dialog).</summary>
internal interface IPairingPrompt
{
    void Show(uint code, string peerName, Action<bool> decide);

    void Close();
}
