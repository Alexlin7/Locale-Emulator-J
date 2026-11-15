using System.IO;
using Microsoft.Win32;

namespace LEGUI;

/// <summary>
/// Locates the global LEConfig.xml of the original Locale Emulator 2.x, so its profiles can be
/// imported before its context menu registration is cleaned up.
///
/// The old LEInstaller registered LEContextMenuHandler.dll with
/// RegistrationServices.RegisterAssembly(..., SetCodeBase), which writes
/// CodeBase = "file:///&lt;folder&gt;/LEContextMenuHandler.DLL" under
/// CLSID\{old}\InprocServer32 and under its version subkey. All of LE 2.x, including
/// LEConfig.xml, lives in that one folder. The path is gone once the registration is removed.
/// </summary>
public static class LegacyInstallation
{
    private const string ConfigFileName = "LEConfig.xml";

    private static readonly string InprocServerKey =
        $@"Software\Classes\CLSID\{ShellExtensionRegistrar.OldClsid}\InprocServer32";

    /// <summary>
    /// Path of the old LEConfig.xml, or null when there is no old registration or the
    /// registered folder has no LEConfig.xml.
    /// </summary>
    public static string? FindOldGlobalConfig(IRegistryOperations registry, Func<string, bool> fileExists)
    {
        // The registration may be per user or per machine, in either registry view
        // (depending on the bitness of the old installer process).
        foreach (var hive in new[] { RegistryHive.CurrentUser, RegistryHive.LocalMachine })
        foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
        {
            var keys = new[] { InprocServerKey }
                .Concat(registry.GetSubKeyNames(hive, InprocServerKey, view)
                                .Select(version => $@"{InprocServerKey}\{version}"));

            foreach (var key in keys)
            {
                var dir = CodeBaseToDirectory(registry.GetStringValue(hive, key, view, "CodeBase"));
                if (dir == null)
                    continue;

                var config = Path.Combine(dir, ConfigFileName);
                if (fileExists(config))
                    return config;
            }
        }

        return null;
    }

    /// <summary>
    /// Folder of a RegAsm CodeBase such as "file:///D:/LE/LEContextMenuHandler.DLL" (spaces and
    /// non-ASCII characters are written as they are), or null when it is not a file URL.
    /// </summary>
    public static string? CodeBaseToDirectory(string? codeBase)
    {
        if (string.IsNullOrEmpty(codeBase))
            return null;

        string path;
        if (codeBase.StartsWith("file:///", StringComparison.OrdinalIgnoreCase))
            path = codeBase["file:///".Length..];
        else if (codeBase.StartsWith("file://", StringComparison.OrdinalIgnoreCase))
            path = @"\\" + codeBase["file://".Length..];  // UNC: file://server/share/...
        else
            return null;

        return Path.GetDirectoryName(path.Replace('/', '\\'));
    }
}
