using Xunit;

namespace LECommonLibrary.Tests;

/// <summary>
///     Importing global profiles from another LEConfig.xml (e.g. the original LE 2.x folder):
///     profiles keep their Guid, and one whose Guid or Name already exists is skipped.
/// </summary>
public class LEConfigImportTests : IDisposable
{
    private readonly string _tempDir;

    public LEConfigImportTests()
    {
        _tempDir = Path.Combine(Path.GetTempPath(), "LEConfigImportTests_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_tempDir);
    }

    public void Dispose()
    {
        if (Directory.Exists(_tempDir))
            Directory.Delete(_tempDir, true);
    }

    private static LEProfile Profile(string name, string guid, string region = "ja-JP") =>
        new(name, guid, false, string.Empty, region, "Tokyo Standard Time", false, true, false, false);

    private string Write(string fileName, params LEProfile[] profiles)
    {
        var path = Path.Combine(_tempDir, fileName);
        WriteMany(path, profiles);
        return path;
    }

    // Writes the LEConfig schema directly (SaveGlobalConfigFile always targets the shared
    // static GlobalConfigPath, which tests running in parallel must not change).
    private static void WriteMany(string path, LEProfile[] profiles)
    {
        var doc = new System.Xml.Linq.XElement("LEConfig",
            new System.Xml.Linq.XElement("Profiles",
                profiles.Select(p => new System.Xml.Linq.XElement("Profile",
                    new System.Xml.Linq.XAttribute("Name", p.Name),
                    new System.Xml.Linq.XAttribute("Guid", p.Guid),
                    new System.Xml.Linq.XAttribute("MainMenu", p.ShowInMainMenu),
                    new System.Xml.Linq.XElement("Parameter", p.Parameter),
                    new System.Xml.Linq.XElement("Region", p.Region),
                    new System.Xml.Linq.XElement("Timezone", p.Timezone),
                    new System.Xml.Linq.XElement("RunAsAdmin", p.RunAsAdmin),
                    new System.Xml.Linq.XElement("RedirectRegistry", p.RedirectRegistry),
                    new System.Xml.Linq.XElement("IsAdvancedRedirection", p.IsAdvancedRedirection),
                    new System.Xml.Linq.XElement("RunWithSuspend", p.RunWithSuspend)))));
        doc.Save(path);
    }

    // ---- MergeProfiles ----

    [Fact]
    public void Merge_NewProfiles_AreAppendedWithTheirGuid()
    {
        var existing = new[] { Profile("Run in Japanese", "g1") };
        var incoming = new[] { Profile("Run in Chinese", "g2", "zh-TW") };

        var merged = LEConfig.MergeProfiles(existing, incoming, out var result);

        Assert.Equal(new[] { "Run in Japanese", "Run in Chinese" }, merged.Select(p => p.Name));
        Assert.Equal("g2", merged[1].Guid);
        Assert.Equal("zh-TW", merged[1].Region);
        Assert.Equal(new LEConfig.ImportResult(1, 0), result);
    }

    [Fact]
    public void Merge_SameGuid_IsSkipped()
    {
        var existing = new[] { Profile("Run in Japanese", "g1") };
        var incoming = new[] { Profile("Renamed", "G1") };  // Guid compare ignores case

        var merged = LEConfig.MergeProfiles(existing, incoming, out var result);

        Assert.Single(merged);
        Assert.Equal(new LEConfig.ImportResult(0, 1), result);
    }

    [Fact]
    public void Merge_SameName_IsSkipped_ExistingWins()
    {
        var existing = new[] { Profile("Run in Japanese", "g1", "ja-JP") };
        var incoming = new[] { Profile("Run in Japanese", "g2", "ko-KR") };

        var merged = LEConfig.MergeProfiles(existing, incoming, out var result);

        Assert.Single(merged);
        Assert.Equal("ja-JP", merged[0].Region);
        Assert.Equal(new LEConfig.ImportResult(0, 1), result);
    }

    [Fact]
    public void Merge_DuplicatesInsideIncoming_OnlyFirstIsImported()
    {
        var incoming = new[] { Profile("A", "g1"), Profile("A", "g2"), Profile("B", "g1") };

        var merged = LEConfig.MergeProfiles([], incoming, out var result);

        Assert.Single(merged);
        Assert.Equal(new LEConfig.ImportResult(1, 2), result);
    }

    [Fact]
    public void Merge_ProfileWithoutGuid_GetsANewOne()
    {
        var merged = LEConfig.MergeProfiles([], new[] { Profile("A", "") }, out var result);

        Assert.True(Guid.TryParse(merged[0].Guid, out _));
        Assert.Equal(new LEConfig.ImportResult(1, 0), result);
    }

    // ---- ImportProfiles (files) ----

    [Fact]
    public void Import_WritesMergedProfilesToTarget()
    {
        var target = Write("new.xml", Profile("Run in Japanese", "g1"), Profile("Run in Japanese (Admin)", "g2"));
        var source = Write("old.xml", Profile("Run in Japanese", "g1"), Profile("Run in Korean", "g3", "ko-KR"));

        var result = LEConfig.ImportProfiles(source, target);

        Assert.Equal(new LEConfig.ImportResult(1, 1), result);
        Assert.Equal(new[] { "Run in Japanese", "Run in Japanese (Admin)", "Run in Korean" },
                     LEConfig.GetProfiles(target).Select(p => p.Name));
    }

    [Fact]
    public void Import_LegacyLocationElement_IsReadAndSavedAsRegion()
    {
        var target = Write("new.xml", Profile("Run in Japanese", "g1"));
        var source = Path.Combine(_tempDir, "old-2x.xml");
        File.WriteAllText(source, """
            <?xml version="1.0" encoding="utf-8"?>
            <LEConfig>
              <Profiles>
                <Profile Name="Run in Taiwan" Guid="f9b6ad4a-0000-4000-8000-000000000001" MainMenu="true">
                  <Parameter />
                  <Location>zh-TW</Location>
                  <Timezone>Taipei Standard Time</Timezone>
                  <RunAsAdmin>false</RunAsAdmin>
                  <RedirectRegistry>true</RedirectRegistry>
                  <IsAdvancedRedirection>false</IsAdvancedRedirection>
                  <RunWithSuspend>false</RunWithSuspend>
                </Profile>
              </Profiles>
            </LEConfig>
            """);

        var result = LEConfig.ImportProfiles(source, target);

        Assert.Equal(new LEConfig.ImportResult(1, 0), result);
        var imported = LEConfig.GetProfiles(target).Single(p => p.Name == "Run in Taiwan");
        Assert.Equal("zh-TW", imported.Region);
        Assert.Equal("f9b6ad4a-0000-4000-8000-000000000001", imported.Guid);
        Assert.True(imported.ShowInMainMenu);
        Assert.Contains("<Region>zh-TW</Region>", File.ReadAllText(target));
    }

    [Fact]
    public void Import_NothingNew_LeavesTargetUntouched()
    {
        var target = Write("new.xml", Profile("Run in Japanese", "g1"));
        var source = Write("old.xml", Profile("Run in Japanese", "g1"));
        var before = File.GetLastWriteTimeUtc(target);
        File.SetLastWriteTimeUtc(target, before.AddMinutes(-5));
        var stamp = File.GetLastWriteTimeUtc(target);

        var result = LEConfig.ImportProfiles(source, target);

        Assert.Equal(new LEConfig.ImportResult(0, 1), result);
        Assert.Equal(stamp, File.GetLastWriteTimeUtc(target));
    }

    [Theory]
    [InlineData("missing.xml")]
    [InlineData("broken.xml")]
    public void Import_UnreadableSource_ImportsNothing(string fileName)
    {
        var target = Write("new.xml", Profile("Run in Japanese", "g1"));
        var source = Path.Combine(_tempDir, fileName);
        if (fileName == "broken.xml")
            File.WriteAllText(source, "not xml");

        var result = LEConfig.ImportProfiles(source, target);

        Assert.Equal(new LEConfig.ImportResult(0, 0), result);
        Assert.Single(LEConfig.GetProfiles(target));
    }

    [Fact]
    public void Import_SameFile_ImportsNothing()
    {
        var target = Write("new.xml", Profile("Run in Japanese", "g1"));

        var result = LEConfig.ImportProfiles(target, Path.Combine(_tempDir, ".", "new.xml"));

        Assert.Equal(new LEConfig.ImportResult(0, 0), result);
    }
}
