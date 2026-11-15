using System.IO;
using Xunit;

namespace LEGUI.Tests;

/// <summary>
///     Files extracted from a downloaded zip carry the Mark of the Web (a Zone.Identifier
///     alternate data stream). Launching LEProc.exe through ShellExecute then shows the
///     "Open File - Security Warning" dialog every time, and Defender runs a cloud scan.
///     Installing the Shell Extension removes the mark from the product folder.
/// </summary>
public sealed class DownloadMarkTests : IDisposable
{
    private const string MarkContent = "[ZoneTransfer]\r\nZoneId=3\r\n";

    private readonly string _dir =
        Path.Combine(Path.GetTempPath(), "LEGUI.Tests.DownloadMark." + Guid.NewGuid().ToString("N"));

    public DownloadMarkTests() => Directory.CreateDirectory(_dir);

    public void Dispose()
    {
        try { Directory.Delete(_dir, true); } catch (IOException) { }
    }

    private string CreateFile(string relativePath, bool marked)
    {
        var path = Path.Combine(_dir, relativePath);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, "content of " + relativePath);
        if (marked)
            File.WriteAllText(path + DownloadMark.StreamSuffix, MarkContent);
        return path;
    }

    private static bool HasMark(string path) => File.Exists(path + DownloadMark.StreamSuffix);

    [Fact]
    public void RemoveFromDirectory_RemovesMarkFromRootAndSubfolders_KeepsContent()
    {
        var exe = CreateFile("LEProc.exe", marked: true);
        var lang = CreateFile(Path.Combine("Lang", "zh-TW.xaml"), marked: true);
        Assert.True(HasMark(exe));

        var removed = DownloadMark.RemoveFromDirectory(_dir);

        Assert.Equal(2, removed);
        Assert.False(HasMark(exe));
        Assert.False(HasMark(lang));
        Assert.Equal("content of LEProc.exe", File.ReadAllText(exe));
    }

    [Fact]
    public void RemoveFromDirectory_IgnoresUnmarkedFiles()
    {
        CreateFile("LEGUI.exe", marked: false);
        var marked = CreateFile("LoaderDll.dll", marked: true);

        Assert.Equal(1, DownloadMark.RemoveFromDirectory(_dir));
        Assert.False(HasMark(marked));
    }

    [Fact]
    public void RemoveFromDirectory_ContinuesWhenOneFileFails()
    {
        var locked = CreateFile("A.dll", marked: true);
        var other = CreateFile("B.dll", marked: true);

        // An open handle without delete sharing makes deleting this stream fail.
        using (new FileStream(locked + DownloadMark.StreamSuffix, FileMode.Open, FileAccess.Read, FileShare.None))
        {
            Assert.Equal(1, DownloadMark.RemoveFromDirectory(_dir));
        }

        Assert.True(HasMark(locked));
        Assert.False(HasMark(other));
    }

    [Fact]
    public void RemoveFromDirectory_MissingDirectory_ReturnsZero()
    {
        Assert.Equal(0, DownloadMark.RemoveFromDirectory(Path.Combine(_dir, "missing")));
    }
}
