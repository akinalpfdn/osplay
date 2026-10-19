using System.Globalization;
using System.Text;

namespace Osplay.App.Connection;

/// <summary>Fills a localized format string (from Osplay.Localization) for the current culture.</summary>
internal static class LocalizedText
{
    public static string Format(string format, object? argument) =>
        string.Format(CultureInfo.CurrentCulture, CompositeFormat.Parse(format), argument);
}
