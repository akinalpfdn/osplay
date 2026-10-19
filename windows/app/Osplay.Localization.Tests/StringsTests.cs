using System.Globalization;
using System.Reflection;
using Xunit;

namespace Osplay.Localization.Tests;

/// <summary>Every user-facing string loads in every shipped language (the shell has no other check).</summary>
public sealed class StringsTests
{
    private static readonly PropertyInfo[] _strings = typeof(Strings)
        .GetProperties(BindingFlags.Public | BindingFlags.Static)
        .Where(property => property.PropertyType == typeof(string))
        .ToArray();

    [Theory]
    [InlineData("en")]
    [InlineData("tr")]
    public void LoadsEveryStringInEachShippedCulture(string culture)
    {
        Strings.Culture = CultureInfo.GetCultureInfo(culture);
        try
        {
            Assert.NotEmpty(_strings);
            foreach (PropertyInfo property in _strings)
            {
                Assert.False(string.IsNullOrWhiteSpace((string?)property.GetValue(null)), property.Name);
            }
        }
        finally
        {
            Strings.Culture = null;
        }
    }

    [Fact]
    public void TranslatesEveryStringIntoTurkish()
    {
        System.Resources.ResourceSet? turkish = Strings.ResourceManager.GetResourceSet(CultureInfo.GetCultureInfo("tr"), true, false);
        Assert.NotNull(turkish);
        foreach (PropertyInfo property in _strings)
        {
            Assert.NotNull(turkish.GetString(property.Name));
        }
    }
}
