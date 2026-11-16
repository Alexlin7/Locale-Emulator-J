using System.IO;
using System.Runtime.InteropServices;

namespace LEGUI;

/// <summary>
///     Removes the Mark of the Web (the <c>Zone.Identifier</c> alternate data stream) from
///     LE's own files. Extracting the downloaded zip copies the mark onto every file;
///     ShellExecute then shows "Open File - Security Warning" each time the context menu
///     starts LEProc.exe (the binaries are not code-signed), and Defender runs a cloud scan.
///     Called after the user installs the Shell Extension, i.e. after they chose to trust
///     LE. Same effect as <c>Unblock-File</c> on those files.
///     <para>
///         Only LE's files are touched (<see cref="IsProductFile" />): the zip may have been
///         extracted into a folder that holds other downloads (e.g. Downloads), whose marks
///         must stay.
///     </para>
/// </summary>
public static class DownloadMark
{
    public const string StreamSuffix = ":Zone.Identifier";

    private const string LangFolder = "Lang";

    // Native files of the single product folder (docs/superpowers/specs/2026-11-16-flat-product-layout.md).
    private static readonly HashSet<string> NativeFiles = new(StringComparer.OrdinalIgnoreCase)
    {
        "LoaderDll.dll", "LocaleEmulator.dll", "LoaderDll64.dll", "LocaleEmulator64.dll",
        "ShellExtension.dll", "ShellExtension64.dll"
    };

    // LE's programs and libraries with their .NET side files (.dll, .runtimeconfig.json, .deps.json, .pdb ...).
    private static readonly string[] ProgramPrefixes = ["LEInstaller.", "LEProc.", "LEGUI.", "LECommonLibrary."];

    private static readonly string[] LangExtensions = [".xaml", ".xml"];

    /// <summary>
    ///     Whether a file belongs to LE: a known file directly in the product folder, or a
    ///     language file (.xaml / .xml) directly in its Lang folder.
    /// </summary>
    /// <param name="relativePath">Path relative to the product folder.</param>
    public static bool IsProductFile(string relativePath)
    {
        var folder = Path.GetDirectoryName(relativePath) ?? string.Empty;
        var name = Path.GetFileName(relativePath);

        if (folder.Length == 0)
            return NativeFiles.Contains(name) ||
                   ProgramPrefixes.Any(prefix => name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase));

        return string.Equals(folder, LangFolder, StringComparison.OrdinalIgnoreCase) &&
               LangExtensions.Contains(Path.GetExtension(name), StringComparer.OrdinalIgnoreCase);
    }

    /// <summary>
    ///     Removes the mark from LE's files in <paramref name="directory"/> (see <see cref="IsProductFile" />).
    ///     Never throws: this only improves the experience and must not fail an install.
    /// </summary>
    /// <returns>The number of files whose mark was removed.</returns>
    public static int RemoveFromProductFiles(string directory)
    {
        List<string> files;
        try
        {
            var root = Directory.EnumerateFiles(directory);
            var langDir = Path.Combine(directory, LangFolder);
            var lang = Directory.Exists(langDir) ? Directory.EnumerateFiles(langDir) : Enumerable.Empty<string>();
            files = root.Concat(lang)
                        .Where(file => IsProductFile(Path.GetRelativePath(directory, file)))
                        .ToList();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException)
        {
            return 0;
        }

        var removed = 0;
        foreach (var file in files)
        {
            // Fails with ERROR_FILE_NOT_FOUND when the file has no mark; other failures
            // (access denied, sharing violation) are skipped as well.
            if (DeleteFile(file + StreamSuffix))
                removed++;
        }
        return removed;
    }

    [DllImport("kernel32.dll", EntryPoint = "DeleteFileW", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DeleteFile(string lpFileName);
}
