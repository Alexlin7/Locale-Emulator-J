using System.IO;
using Xunit;

namespace LEGUI.Tests;

/// <summary>
///     Files extracted from a downloaded zip carry the Mark of the Web (a Zone.Identifier
///     alternate data stream). Launching LEProc.exe through ShellExecute then shows the
///     "Open File - Security Warning" dialog every time, and Defender runs a cloud scan.
///     Installing the Shell Extension removes the mark from LE's own files only: the zip may
///     have been extracted into a folder with other downloads, whose marks must stay.
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
    public void RemoveFromProductFiles_RemovesMarkFromProgramsAndLang_KeepsContent()
    {
        var exe = CreateFile("LEProc.exe", marked: true);
        var lang = CreateFile(Path.Combine("Lang", "zh-TW.xaml"), marked: true);
        Assert.True(HasMark(exe));

        var removed = DownloadMark.RemoveFromProductFiles(_dir);

        Assert.Equal(2, removed);
        Assert.False(HasMark(exe));
        Assert.False(HasMark(lang));
        Assert.Equal("content of LEProc.exe", File.ReadAllText(exe));
    }

    [Fact]
    public void RemoveFromProductFiles_IgnoresUnmarkedFiles()
    {
        CreateFile("LEGUI.exe", marked: false);
        var marked = CreateFile("LoaderDll.dll", marked: true);

        Assert.Equal(1, DownloadMark.RemoveFromProductFiles(_dir));
        Assert.False(HasMark(marked));
    }

    [Fact]
    public void RemoveFromProductFiles_ContinuesWhenOneFileFails()
    {
        var locked = CreateFile("LEProc.exe", marked: true);
        var other = CreateFile("LEGUI.exe", marked: true);

        // An open handle without delete sharing makes deleting this stream fail.
        using (new FileStream(locked + DownloadMark.StreamSuffix, FileMode.Open, FileAccess.Read, FileShare.None))
        {
            Assert.Equal(1, DownloadMark.RemoveFromProductFiles(_dir));
        }

        Assert.True(HasMark(locked));
        Assert.False(HasMark(other));
    }

    [Fact]
    public void RemoveFromProductFiles_MissingDirectory_ReturnsZero()
    {
        Assert.Equal(0, DownloadMark.RemoveFromProductFiles(Path.Combine(_dir, "missing")));
    }

    [Fact]
    public void RemoveFromProductFiles_KeepsMarksOfOtherFiles()
    {
        // e.g. the zip was extracted straight into the Downloads folder.
        var ours = new[]
        {
            CreateFile("LEInstaller.exe", marked: true),
            CreateFile("LEGUI.dll", marked: true),
            CreateFile("LEProc.runtimeconfig.json", marked: true),
            CreateFile("LECommonLibrary.dll", marked: true),
            CreateFile("LocaleEmulator.dll", marked: true),
            CreateFile("ShellExtension64.dll", marked: true),
            CreateFile(Path.Combine("Lang", "DefaultLanguage.xml"), marked: true)
        };
        var others = new[]
        {
            CreateFile("setup.exe", marked: true),
            CreateFile("LEGO.exe", marked: true),
            CreateFile("readme.txt", marked: true),
            CreateFile(Path.Combine("Lang", "notes.txt"), marked: true),
            CreateFile(Path.Combine("SomeGame", "game.exe"), marked: true),
            CreateFile(Path.Combine("SomeGame", "LEProc.exe"), marked: true)
        };

        Assert.Equal(ours.Length, DownloadMark.RemoveFromProductFiles(_dir));

        Assert.All(ours, file => Assert.False(HasMark(file), file));
        Assert.All(others, file => Assert.True(HasMark(file), file));
    }

    [Theory]
    [InlineData("LEProc.exe", true)]
    [InlineData("leproc.exe", true)]
    [InlineData("LoaderDll.dll", true)]
    [InlineData(@"Lang\ja.xaml", true)]
    [InlineData(@"Lang\ja.xml", true)]
    [InlineData("LEGO.exe", false)]
    [InlineData("LEProcessor.exe", false)]
    [InlineData(@"Lang\run.exe", false)]
    [InlineData(@"Other\LEProc.exe", false)]
    [InlineData(@"Lang\Sub\ja.xaml", false)]
    public void IsProductFile(string relativePath, bool expected)
    {
        Assert.Equal(expected, DownloadMark.IsProductFile(relativePath));
    }
}
