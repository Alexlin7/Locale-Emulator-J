using System.IO;
using System.Runtime.InteropServices;

namespace LEGUI;

/// <summary>
///     Removes the Mark of the Web (the <c>Zone.Identifier</c> alternate data stream) from
///     the product folder. Extracting the downloaded zip copies the mark onto every file;
///     ShellExecute then shows "Open File - Security Warning" each time the context menu
///     starts LEProc.exe (the binaries are not code-signed), and Defender runs a cloud scan.
///     Called after the user installs the Shell Extension, i.e. after they chose to trust
///     this folder. Same effect as <c>Unblock-File</c>.
/// </summary>
public static class DownloadMark
{
    public const string StreamSuffix = ":Zone.Identifier";

    /// <summary>
    ///     Removes the mark from every file under <paramref name="directory"/> (recursively).
    ///     Never throws: this only improves the experience and must not fail an install.
    /// </summary>
    /// <returns>The number of files whose mark was removed.</returns>
    public static int RemoveFromDirectory(string directory)
    {
        IEnumerable<string> files;
        try
        {
            files = Directory.EnumerateFiles(directory, "*", new EnumerationOptions
            {
                RecurseSubdirectories = true,
                IgnoreInaccessible = true,
                AttributesToSkip = FileAttributes.ReparsePoint
            }).ToList();
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
