using Xunit;

namespace LEGUI.Tests;

public class ShellExtCliCommandTests
{
    [Theory]
    [InlineData(new[] { "--shell-ext", "install", "current-user" },
                ShellExtCliCommand.Verb.Install,
                ShellExtensionRegistrar.InstallMode.CurrentUser)]
    [InlineData(new[] { "--shell-ext", "install", "all-users" },
                ShellExtCliCommand.Verb.Install,
                ShellExtensionRegistrar.InstallMode.AllUsers)]
    [InlineData(new[] { "--shell-ext", "uninstall", "current-user" },
                ShellExtCliCommand.Verb.Uninstall,
                ShellExtensionRegistrar.InstallMode.CurrentUser)]
    public void Parse_ValidArgs_ReturnsParsedCommand(
        string[] args, ShellExtCliCommand.Verb verb, ShellExtensionRegistrar.InstallMode mode)
    {
        var cmd = ShellExtCliCommand.Parse(args);
        Assert.NotNull(cmd);
        Assert.Equal(verb, cmd.ActionVerb);
        Assert.Equal(mode, cmd.Mode);
    }

    [Fact]
    public void Parse_CleanupOld_NoModeRequired()
    {
        var cmd = ShellExtCliCommand.Parse(new[] { "--shell-ext", "cleanup-old" });
        Assert.NotNull(cmd);
        Assert.Equal(ShellExtCliCommand.Verb.CleanupOld, cmd.ActionVerb);
    }

    [Fact]
    public void IsInstallerMode_OnlyForTheInstallerSwitchAlone()
    {
        Assert.True(ShellExtCliCommand.IsInstallerMode(new[] { "--installer" }));

        Assert.False(ShellExtCliCommand.IsInstallerMode(Array.Empty<string>()));
        Assert.False(ShellExtCliCommand.IsInstallerMode(new[] { "game.exe" }));
        Assert.False(ShellExtCliCommand.IsInstallerMode(new[] { "--installer", "extra" }));
        Assert.False(ShellExtCliCommand.IsInstallerMode(new[] { "--shell-ext", "install", "current-user" }));
    }

    [Fact]
    public void Parse_NonShellExtArgs_ReturnsNull()
    {
        Assert.Null(ShellExtCliCommand.Parse(new[] { "game.exe" }));
        Assert.Null(ShellExtCliCommand.Parse(Array.Empty<string>()));
    }

    [Fact]
    public void Parse_InvalidVerb_ReturnsNull()
    {
        Assert.Null(ShellExtCliCommand.Parse(new[] { "--shell-ext", "xyz", "current-user" }));
    }
}
