using System.Runtime.InteropServices;
using Osplay.App.Diagnostics;
using Osplay.App.Interop;

namespace Osplay.App.Services;

/// <summary>
/// Whether a connected network is marked Public: Windows Firewall then blocks Bonjour and incoming
/// connections, so other computers cannot find or reach this PC.
/// </summary>
internal static class NetworkProfile
{
    private const int _connectedNetworks = 0x01; // NLM_ENUM_NETWORK_CONNECTED
    private const int _publicCategory = 0;       // NLM_NETWORK_CATEGORY_PUBLIC

    public static Task<bool> IsAnyNetworkPublicAsync() => Task.Run(() =>
    {
        try
        {
            var manager = (INetworkListManager)new NetworkListManager();
            IEnumNetworks networks = manager.GetNetworks(_connectedNetworks);
            var batch = new INetwork[1];
            while (networks.Next(1, batch, out uint fetched) == 0 && fetched == 1)
            {
                if (batch[0].GetCategory() == _publicCategory)
                {
                    return true;
                }
            }
            return false;
        }
        catch (COMException error)
        {
            Log.Write(LogLevel.Warning, $"The network profile could not be read: {error.Message}");
            return false;
        }
    });

    [ComImport]
    [Guid("DCB00C01-570F-4A9B-8D69-199FDBA5723B")]
    private class NetworkListManager;

    [ComImport]
    [Guid("DCB00000-570F-4A9B-8D69-199FDBA5723B")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    private interface INetworkListManager
    {
        IEnumNetworks GetNetworks(int flags);
    }

    [ComImport]
    [Guid("DCB00003-570F-4A9B-8D69-199FDBA5723B")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    private interface IEnumNetworks
    {
        [return: MarshalAs(UnmanagedType.IUnknown)]
        object GetNewEnum();

        [PreserveSig]
        int Next(uint count, [Out, MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 0)] INetwork[] networks,
            out uint fetched);
    }

    [ComImport]
    [Guid("DCB00002-570F-4A9B-8D69-199FDBA5723B")]
    [InterfaceType(ComInterfaceType.InterfaceIsDual)]
    private interface INetwork
    {
        // Vtable order matters: only GetCategory is called, the rest keep the slots.
        [return: MarshalAs(UnmanagedType.BStr)]
        string GetName();

        void SetName([MarshalAs(UnmanagedType.BStr)] string name);

        [return: MarshalAs(UnmanagedType.BStr)]
        string GetDescription();

        void SetDescription([MarshalAs(UnmanagedType.BStr)] string description);

        Guid GetNetworkId();

        int GetDomainType();

        [return: MarshalAs(UnmanagedType.IUnknown)]
        object GetNetworkConnections();

        void GetTimeCreatedAndConnected(out uint createdLow, out uint createdHigh, out uint connectedLow,
            out uint connectedHigh);

        bool IsConnectedToInternet { [return: MarshalAs(UnmanagedType.VariantBool)] get; }

        bool IsConnected { [return: MarshalAs(UnmanagedType.VariantBool)] get; }

        int GetConnectivity();

        int GetCategory();
    }
}
