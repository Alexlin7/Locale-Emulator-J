using System.Globalization;

namespace LECommonLibrary;

/// <summary>
/// Decides which regions (cultures) Locale Emulator can emulate.
/// </summary>
/// <remarks>
/// Core needs a real Windows LCID and an ANSI code page with an NLS table. Many
/// cultures have neither: locales without an LCID report LOCALE_CUSTOM_UNSPECIFIED
/// (0x1000), and Unicode-only locales report ANSI code page 0. Starting a process
/// with such a region fails silently, so they are hidden from the region list and
/// rejected by LEProc.
///
/// The region list relies on System.Globalization.UseNls (Directory.Build.props):
/// under .NET's default ICU globalization, CultureInfo.GetCultures lists e.g.
/// zh-Hans-CN / zh-Hant-TW (LCID 0x1000) instead of zh-CN / zh-TW.
/// </remarks>
public static class RegionSupport
{
    private const int LOCALE_CUSTOM_DEFAULT = 0x0C00;
    private const int LOCALE_CUSTOM_UNSPECIFIED = 0x1000;
    private const int LOCALE_CUSTOM_UI_DEFAULT = 0x1400;

    public static bool IsSupported(CultureInfo culture) =>
        culture.LCID is not (LOCALE_CUSTOM_DEFAULT or LOCALE_CUSTOM_UNSPECIFIED or LOCALE_CUSTOM_UI_DEFAULT)
        && culture.TextInfo.ANSICodePage != 0
        && culture.TextInfo.OEMCodePage != 0;

    /// <summary>All cultures Locale Emulator can emulate, sorted by display name.</summary>
    public static List<CultureInfo> GetSupportedCultures() =>
        CultureInfo.GetCultures(CultureTypes.AllCultures)
            .Where(c => !string.IsNullOrEmpty(c.Name) && IsSupported(c))
            .OrderBy(c => c.DisplayName)
            .ToList();

    public static bool TryGetSupportedCulture(string name, out CultureInfo culture)
    {
        culture = null!;

        if (string.IsNullOrEmpty(name))
            return false;

        try
        {
            var candidate = CultureInfo.GetCultureInfo(name);
            if (!IsSupported(candidate))
                return false;

            culture = candidate;
            return true;
        }
        catch (CultureNotFoundException)
        {
            return false;
        }
    }
}
