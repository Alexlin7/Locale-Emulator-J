using Xunit;

namespace LECommonLibrary.Tests;

/// <summary>
///     Importing global profiles from another LEConfig.xml (e.g. the original LE 2.x folder):
///     profiles keep their Guid; an identical one is skipped, and a different one with an
///     existing name is replaced, saved under a new name or skipped, as the resolver decides.
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
        if (!Directory.Exists(_tempDir))
            return;
        foreach (var file in Directory.GetFiles(_tempDir))
            File.SetAttributes(file, FileAttributes.Normal);
        Directory.Delete(_tempDir, true);
    }

    // Fails the test when a conflict is asked although none was expected.
    private static readonly LEConfig.ImportConflictResolver NoQuestion =
        (_, incoming, _) => throw new Xunit.Sdk.XunitException("Unexpected conflict question for " + incoming.Name);

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
    public void Merge_SameNameDifferentSettings_WithoutResolver_IsSkipped_ExistingWins()
    {
        var existing = new[] { Profile("Run in Japanese", "g1", "ja-JP") };
        var incoming = new[] { Profile("Run in Japanese", "g2", "ko-KR") };

        var merged = LEConfig.MergeProfiles(existing, incoming, out var result);

        Assert.Single(merged);
        Assert.Equal("ja-JP", merged[0].Region);
        Assert.Equal(new LEConfig.ImportResult(0, 1), result);
    }

    [Fact]
    public void Merge_SameNameSameSettings_IsSkipped_WithoutAsking()
    {
        // e.g. the untouched default "Run in Japanese" of the old version (other Guid).
        var existing = new[] { Profile("Run in Japanese", "g1") };
        var incoming = new[] { Profile("Run in Japanese", "g2") };

        var merged = LEConfig.MergeProfiles(existing, incoming, NoQuestion, out var result);

        Assert.Single(merged);
        Assert.Equal("g1", merged[0].Guid);
        Assert.Equal(new LEConfig.ImportResult(0, 1), result);
    }

    [Fact]
    public void Merge_SameNameDifferentSettings_AsksWithBothProfiles()
    {
        var existing = new[] { Profile("Run in Japanese", "g1", "ja-JP") };
        var incoming = new[] { Profile("Run in Japanese", "g2", "ko-KR") };
        LEProfile? askedExisting = null, askedIncoming = null;

        LEConfig.MergeProfiles(existing, incoming, (e, i, _) =>
        {
            askedExisting = e;
            askedIncoming = i;
            return LEConfig.ImportConflictDecision.Skip;
        }, out _);

        Assert.Equal("ja-JP", askedExisting?.Region);
        Assert.Equal("ko-KR", askedIncoming?.Region);
    }

    [Fact]
    public void Merge_Replace_TakesIncomingSettings_KeepsExistingNameGuidAndPosition()
    {
        var existing = new[] { Profile("Run in Japanese", "g1", "ja-JP"), Profile("Other", "g9") };
        var incoming = new[] { Profile("Run in Japanese", "g2", "ko-KR") };

        var merged = LEConfig.MergeProfiles(existing, incoming,
                                            (_, _, _) => LEConfig.ImportConflictDecision.Replace, out var result);

        Assert.Equal(new[] { "Run in Japanese", "Other" }, merged.Select(p => p.Name));
        Assert.Equal("g1", merged[0].Guid);
        Assert.Equal("ko-KR", merged[0].Region);
        Assert.Equal(new LEConfig.ImportResult(1, 0), result);
    }

    [Fact]
    public void Merge_SaveAs_AddsUnderTheNewName_KeepingTheIncomingGuid()
    {
        var existing = new[] { Profile("Run in Japanese", "g1", "ja-JP") };
        var incoming = new[] { Profile("Run in Japanese", "g2", "ko-KR") };

        var merged = LEConfig.MergeProfiles(existing, incoming,
                                            (_, _, _) => LEConfig.ImportConflictDecision.SaveAs(" Old Japanese "),
                                            out var result);

        Assert.Equal(new[] { "Run in Japanese", "Old Japanese" }, merged.Select(p => p.Name));
        Assert.Equal("ja-JP", merged[0].Region);
        Assert.Equal(("g2", "ko-KR"), (merged[1].Guid, merged[1].Region));
        Assert.Equal(new LEConfig.ImportResult(1, 0), result);
    }

    [Fact]
    public void Merge_SaveAs_GetsANewGuid_WhenItsGuidIsTaken()
    {
        var existing = new[] { Profile("Run in Japanese", "g1", "ja-JP") };
        var incoming = new[] { Profile("Run in Japanese", "g1", "ko-KR") };

        var merged = LEConfig.MergeProfiles(existing, incoming,
                                            (_, _, _) => LEConfig.ImportConflictDecision.SaveAs("Old"), out _);

        Assert.True(Guid.TryParse(merged[1].Guid, out _));
    }

    [Fact]
    public void Merge_Resolver_CanCheckWhichNamesAreTaken()
    {
        var existing = new[] { Profile("Run in Japanese", "g1", "ja-JP"), Profile("Old", "g9") };
        var incoming = new[] { Profile("Run in Japanese", "g2", "ko-KR") };
        bool? oldTaken = null, freeTaken = null;

        LEConfig.MergeProfiles(existing, incoming, (_, _, isNameTaken) =>
        {
            oldTaken = isNameTaken("Old");
            freeTaken = isNameTaken("Free");
            return LEConfig.ImportConflictDecision.Skip;
        }, out _);

        Assert.True(oldTaken);
        Assert.False(freeTaken);
    }

    [Theory]
    [InlineData("Old")]
    [InlineData("  ")]
    public void Merge_SaveAs_WithATakenOrEmptyName_IsSkipped(string newName)
    {
        var existing = new[] { Profile("Run in Japanese", "g1", "ja-JP"), Profile("Old", "g9") };
        var incoming = new[] { Profile("Run in Japanese", "g2", "ko-KR") };

        var merged = LEConfig.MergeProfiles(existing, incoming,
                                            (_, _, _) => LEConfig.ImportConflictDecision.SaveAs(newName),
                                            out var result);

        Assert.Equal(2, merged.Length);
        Assert.Equal(new LEConfig.ImportResult(0, 1), result);
    }

    [Fact]
    public void Merge_SameGuidOtherNameDifferentSettings_IsAddedWithANewGuid()
    {
        var existing = new[] { Profile("Run in Japanese", "g1", "ja-JP") };
        var incoming = new[] { Profile("Run in Korean", "g1", "ko-KR") };

        var merged = LEConfig.MergeProfiles(existing, incoming, NoQuestion, out var result);

        Assert.Equal(new[] { "Run in Japanese", "Run in Korean" }, merged.Select(p => p.Name));
        Assert.NotEqual("g1", merged[1].Guid);
        Assert.Equal(new LEConfig.ImportResult(1, 0), result);
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
    public void Import_ModifiedDefaultOfOldVersion_Replace_IsSaved()
    {
        // The new version's defaults vs. an old "Run in Japanese" changed to run as administrator.
        var target = Write("new.xml", Profile("Run in Japanese", "g1"), Profile("Run in Japanese (Admin)", "g2"));
        var changed = Profile("Run in Japanese", "old1");
        changed.RunAsAdmin = true;
        var source = Write("old.xml", changed);

        var result = LEConfig.ImportProfiles(source, target, (_, _, _) => LEConfig.ImportConflictDecision.Replace);

        Assert.Equal(new LEConfig.ImportResult(1, 0), result);
        var saved = LEConfig.GetProfiles(target);
        Assert.Equal(2, saved.Length);
        Assert.True(saved[0].RunAsAdmin);
        Assert.Equal("g1", saved[0].Guid);
    }

    [Fact]
    public void Import_TargetMissing_WritesOnlyTheImportedProfiles_WithoutAsking()
    {
        // First migration: no default profiles that could win over the old ones.
        var target = Path.Combine(_tempDir, "LEConfig.xml");
        var changed = Profile("Run in Japanese", "old1");
        changed.RunAsAdmin = true;
        var source = Write("old.xml", changed, Profile("Run in Korean", "old2", "ko-KR"));

        var result = LEConfig.ImportProfiles(source, target, NoQuestion);

        Assert.Equal(new LEConfig.ImportResult(2, 0), result);
        var saved = LEConfig.GetProfiles(target);
        Assert.Equal(new[] { "Run in Japanese", "Run in Korean" }, saved.Select(p => p.Name));
        Assert.True(saved[0].RunAsAdmin);
    }

    [Fact]
    public void Import_TargetMissing_SourceWithoutProfiles_CreatesNothing()
    {
        var target = Path.Combine(_tempDir, "LEConfig.xml");
        var source = Write("old.xml");

        var result = LEConfig.ImportProfiles(source, target);

        Assert.Equal(new LEConfig.ImportResult(0, 0), result);
        Assert.False(File.Exists(target));
    }

    [Fact]
    public void Import_UnreadableTarget_Throws_AndLeavesItUntouched()
    {
        // Overwriting it with only the imported profiles would lose the existing ones.
        var target = Path.Combine(_tempDir, "new.xml");
        File.WriteAllText(target, "not xml");
        var source = Write("old.xml", Profile("Run in Korean", "g3", "ko-KR"));

        Assert.ThrowsAny<System.Xml.XmlException>(() => LEConfig.ImportProfiles(source, target));
        Assert.Equal("not xml", File.ReadAllText(target));
    }

    [Fact]
    public void Import_TargetNotWritable_Throws_InsteadOfReportingSuccess()
    {
        var target = Write("new.xml", Profile("Run in Japanese", "g1"));
        File.SetAttributes(target, FileAttributes.ReadOnly);
        var source = Write("old.xml", Profile("Run in Korean", "g3", "ko-KR"));

        Assert.ThrowsAny<UnauthorizedAccessException>(() => LEConfig.ImportProfiles(source, target));
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
