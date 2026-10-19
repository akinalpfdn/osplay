using System.Runtime.InteropServices;
using System.Text;

namespace Osplay.App.Interop;

/// <summary>UTF-8 strings between C buffers and .NET.</summary>
internal static unsafe class Utf8
{
    /// <summary>A NUL-terminated string, or empty for null.</summary>
    public static string Read(byte* text) =>
        text == null ? string.Empty : Marshal.PtrToStringUTF8((nint)text) ?? string.Empty;

    /// <summary>A fixed C array: up to the first NUL or its end.</summary>
    public static string Read(byte* field, int size)
    {
        int length = new ReadOnlySpan<byte>(field, size).IndexOf((byte)0);
        return Encoding.UTF8.GetString(field, length < 0 ? size : length);
    }

    /// <summary>Allocates a NUL-terminated copy; release it with <see cref="Free"/>.</summary>
    public static nint Allocate(string text) => Marshal.StringToCoTaskMemUTF8(text);

    public static void Free(nint text) => Marshal.FreeCoTaskMem(text);
}
