namespace Osplay.App.Connection;

/// <summary>Shows the six-digit pairing code with accept and reject.</summary>
internal interface IPairingPrompt
{
    void Show(uint code, string peerName, Action<bool> decide);

    void Close();
}
