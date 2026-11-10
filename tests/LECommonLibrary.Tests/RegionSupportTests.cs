using System.Globalization;
using LECommonLibrary;
using Xunit;

namespace LECommonLibrary.Tests;

public class RegionSupportTests
{
    // Regions users pick most often. Under .NET's default ICU globalization the
    // region list only offers zh-Hans-CN / zh-Hant-TW (LCID 0x1000), which Core
    // cannot emulate; with System.Globalization.UseNls the list matches Windows
    // (and the original .NET Framework Locale Emulator).
    [Theory]
    [InlineData("zh-CN", 0x0804, 936)]
    [InlineData("zh-TW", 0x0404, 950)]
    [InlineData("zh-HK", 0x0C04, 950)]
    [InlineData("ja-JP", 0x0411, 932)]
    [InlineData("ko-KR", 0x0412, 949)]
    [InlineData("en-US", 0x0409, 1252)]
    public void SupportedCultures_IncludeCommonRegionsWithRealLcid(string name, int lcid, int ansiCodePage)
    {
        var culture = RegionSupport.GetSupportedCultures().SingleOrDefault(c => c.Name == name);

        Assert.NotNull(culture);
        Assert.Equal(lcid, culture.LCID);
        Assert.Equal(ansiCodePage, culture.TextInfo.ANSICodePage);
    }

    [Fact]
    public void SupportedCultures_OnlyContainEmulatableCultures()
    {
        var cultures = RegionSupport.GetSupportedCultures();

        Assert.NotEmpty(cultures);
        Assert.All(cultures, c => Assert.True(RegionSupport.IsSupported(c), c.Name));
    }

    [Theory]
    [InlineData("ja-JP")]
    [InlineData("zh-CN")]
    [InlineData("zh-Hans")]   // neutral culture: has a real LCID (0x0004) and code page
    public void IsSupported_AcceptsCulturesWithLcidAndAnsiCodePage(string name)
    {
        Assert.True(RegionSupport.IsSupported(CultureInfo.GetCultureInfo(name)));
    }

    [Theory]
    [InlineData("zh-Hans-HK")]   // LCID 0x1000 (LOCALE_CUSTOM_UNSPECIFIED)
    [InlineData("kok-IN")]       // Unicode-only locale: ANSI code page 0
    public void IsSupported_RejectsCulturesCoreCannotEmulate(string name)
    {
        Assert.False(RegionSupport.IsSupported(CultureInfo.GetCultureInfo(name)));
    }

    [Theory]
    [InlineData("zh-Hans-HK")]
    [InlineData("not-a-culture")]
    [InlineData("")]
    public void TryGetSupportedCulture_RejectsUnsupportedOrUnknownNames(string name)
    {
        Assert.False(RegionSupport.TryGetSupportedCulture(name, out _));
    }

    // Profiles saved while LEGUI ran under ICU contain names such as zh-Hans-CN.
    // Under NLS these resolve to the Windows locale, so those profiles keep working.
    [Theory]
    [InlineData("zh-Hans-CN", "zh-CN", 0x0804)]
    [InlineData("zh-Hant-TW", "zh-TW", 0x0404)]
    [InlineData("zh-Hant-HK", "zh-HK", 0x0C04)]
    public void TryGetSupportedCulture_ResolvesIcuStyleNamesSavedByOlderBuilds(string saved, string expectedName, int expectedLcid)
    {
        Assert.True(RegionSupport.TryGetSupportedCulture(saved, out var culture));
        Assert.Equal(expectedName, culture.Name);
        Assert.Equal(expectedLcid, culture.TextInfo.LCID);
    }

    [Fact]
    public void TryGetSupportedCulture_ReturnsCultureForSupportedName()
    {
        Assert.True(RegionSupport.TryGetSupportedCulture("ja-JP", out var culture));
        Assert.Equal(0x0411, culture.LCID);
    }
}
