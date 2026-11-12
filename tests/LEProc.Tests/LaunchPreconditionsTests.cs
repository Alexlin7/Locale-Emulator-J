using Xunit;

namespace LEProc.Tests;

public class LaunchPreconditionsTests : IDisposable
{
    private readonly string _tempDir;

    public LaunchPreconditionsTests()
    {
        _tempDir = Path.Combine(Path.GetTempPath(), "LaunchPreconditionsTests_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_tempDir);
    }

    public void Dispose()
    {
        if (Directory.Exists(_tempDir))
            Directory.Delete(_tempDir, true);
    }

    private string CreateFile(string name, string content = "")
    {
        var path = Path.Combine(_tempDir, name);
        File.WriteAllText(path, content);
        return path;
    }

    // ---- GetMissingTargetMessage ----

    [Fact]
    public void GetMissingTargetMessage_ExistingFile_ReturnsNull()
    {
        var exe = CreateFile("game.exe");

        Assert.Null(LaunchPreconditions.GetMissingTargetMessage(exe));
    }

    [Fact]
    public void GetMissingTargetMessage_MissingFile_ReturnsMessageWithPath()
    {
        var exe = Path.Combine(_tempDir, "missing.exe");

        var message = LaunchPreconditions.GetMissingTargetMessage(exe);

        Assert.NotNull(message);
        Assert.Contains(exe, message);
    }

    [Fact]
    public void GetMissingTargetMessage_RepeatedPathPrefix_ReturnsMessage()
    {
        // The case from docs/roadmap.md: the path prefix repeated, e.g. X:\dir\X:\dir\game.exe.
        CreateFile("game.exe");
        var broken = _tempDir + Path.DirectorySeparatorChar + _tempDir + Path.DirectorySeparatorChar + "game.exe";

        Assert.NotNull(LaunchPreconditions.GetMissingTargetMessage(broken));
    }

    [Fact]
    public void GetMissingTargetMessage_Directory_ReturnsMessage()
    {
        Assert.NotNull(LaunchPreconditions.GetMissingTargetMessage(_tempDir));
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    public void GetMissingTargetMessage_NullOrEmpty_ReturnsMessage(string? path)
    {
        Assert.NotNull(LaunchPreconditions.GetMissingTargetMessage(path));
    }

    // ---- LoadAppProfile ----

    [Fact]
    public void LoadAppProfile_NoConfigFile_ReturnsNoConfig()
    {
        var status = LaunchPreconditions.LoadAppProfile(Path.Combine(_tempDir, "game.exe.le.config"), out _);

        Assert.Equal(LaunchPreconditions.AppProfileStatus.NoConfig, status);
    }

    [Fact]
    public void LoadAppProfile_ValidConfig_ReturnsLoadedWithFirstProfile()
    {
        var conf = CreateFile("game.exe.le.config", """
            <?xml version="1.0" encoding="utf-8"?>
            <LEConfig>
              <Profiles>
                <Profile Name="game.exe" Guid="guid-1" MainMenu="false">
                  <Parameter></Parameter>
                  <Region>zh-TW</Region>
                  <Timezone>Taipei Standard Time</Timezone>
                </Profile>
              </Profiles>
            </LEConfig>
            """);

        var status = LaunchPreconditions.LoadAppProfile(conf, out var profile);

        Assert.Equal(LaunchPreconditions.AppProfileStatus.Loaded, status);
        Assert.Equal("zh-TW", profile.Region);
    }

    [Fact]
    public void LoadAppProfile_LegacyLocationConfig_ReturnsLoaded()
    {
        var conf = CreateFile("game.exe.le.config", """
            <?xml version="1.0" encoding="utf-8"?>
            <LEConfig>
              <Profiles>
                <Profile Name="game.exe" Guid="guid-1" MainMenu="false">
                  <Parameter></Parameter>
                  <Location>ja-JP</Location>
                  <Timezone>Tokyo Standard Time</Timezone>
                </Profile>
              </Profiles>
            </LEConfig>
            """);

        var status = LaunchPreconditions.LoadAppProfile(conf, out var profile);

        Assert.Equal(LaunchPreconditions.AppProfileStatus.Loaded, status);
        Assert.Equal("ja-JP", profile.Region);
    }

    [Theory]
    [InlineData("not xml at all")]
    [InlineData("<LEConfig><Profiles /></LEConfig>")]
    public void LoadAppProfile_UnreadableConfig_ReturnsUnreadable(string content)
    {
        var conf = CreateFile("game.exe.le.config", content);

        var status = LaunchPreconditions.LoadAppProfile(conf, out _);

        Assert.Equal(LaunchPreconditions.AppProfileStatus.Unreadable, status);
    }
}
