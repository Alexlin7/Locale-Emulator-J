using Microsoft.Win32;
using NSubstitute;
using Xunit;

namespace LEGUI.Tests;

/// <summary>
///     Finding the original LE 2.x folder from its context menu registration: the old
///     LEInstaller registered LEContextMenuHandler.dll with RegAsm /codebase, which writes
///     CodeBase = file:///&lt;folder&gt;/LEContextMenuHandler.DLL under InprocServer32
///     (and under a version subkey). LEConfig.xml lives in that same folder.
/// </summary>
public class LegacyInstallationTests
{
    private const string InprocKey = @"Software\Classes\CLSID\" + ShellExtensionRegistrar.OldClsid + @"\InprocServer32";

    [Theory]
    [InlineData("file:///D:/Tools/LE/LEContextMenuHandler.DLL", @"D:\Tools\LE")]
    [InlineData("file:///C:/Program Files/Locale Emulator/LEContextMenuHandler.DLL", @"C:\Program Files\Locale Emulator")]
    [InlineData("FILE:///E:/遊戲工具/LE/LEContextMenuHandler.dll", @"E:\遊戲工具\LE")]
    [InlineData("file://server/share/LE/LEContextMenuHandler.DLL", @"\\server\share\LE")]
    public void CodeBaseToDirectory_ParsesRegAsmCodeBase(string codeBase, string expected)
    {
        Assert.Equal(expected, LegacyInstallation.CodeBaseToDirectory(codeBase));
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("mscoree.dll")]
    [InlineData("http://example.com/LEContextMenuHandler.DLL")]
    public void CodeBaseToDirectory_NotAFileCodeBase_ReturnsNull(string? codeBase)
    {
        Assert.Null(LegacyInstallation.CodeBaseToDirectory(codeBase));
    }

    [Fact]
    public void FindOldGlobalConfig_CodeBaseOnInprocServer32()
    {
        var registry = Substitute.For<IRegistryOperations>();
        registry.GetStringValue(RegistryHive.LocalMachine, InprocKey, RegistryView.Registry64, "CodeBase")
                .Returns("file:///D:/OldLE/LEContextMenuHandler.DLL");
        registry.GetSubKeyNames(Arg.Any<RegistryHive>(), Arg.Any<string>(), Arg.Any<RegistryView>())
                .Returns([]);

        var found = LegacyInstallation.FindOldGlobalConfig(registry, path => path == @"D:\OldLE\LEConfig.xml");

        Assert.Equal(@"D:\OldLE\LEConfig.xml", found);
    }

    [Fact]
    public void FindOldGlobalConfig_CodeBaseOnVersionSubkey_CurrentUser32BitView()
    {
        var registry = Substitute.For<IRegistryOperations>();
        registry.GetSubKeyNames(RegistryHive.CurrentUser, InprocKey, RegistryView.Registry32)
                .Returns(["2.5.0.0"]);
        registry.GetStringValue(RegistryHive.CurrentUser, InprocKey + @"\2.5.0.0", RegistryView.Registry32, "CodeBase")
                .Returns("file:///E:/LE/LEContextMenuHandler.DLL");

        var found = LegacyInstallation.FindOldGlobalConfig(registry, path => path == @"E:\LE\LEConfig.xml");

        Assert.Equal(@"E:\LE\LEConfig.xml", found);
    }

    [Fact]
    public void FindOldGlobalConfig_FolderWithoutLEConfig_ReturnsNull()
    {
        var registry = Substitute.For<IRegistryOperations>();
        registry.GetStringValue(RegistryHive.LocalMachine, InprocKey, RegistryView.Registry64, "CodeBase")
                .Returns("file:///D:/OldLE/LEContextMenuHandler.DLL");
        registry.GetSubKeyNames(Arg.Any<RegistryHive>(), Arg.Any<string>(), Arg.Any<RegistryView>())
                .Returns([]);

        Assert.Null(LegacyInstallation.FindOldGlobalConfig(registry, _ => false));
    }

    [Fact]
    public void FindOldGlobalConfig_NoRegistration_ReturnsNull()
    {
        var registry = Substitute.For<IRegistryOperations>();
        registry.GetSubKeyNames(Arg.Any<RegistryHive>(), Arg.Any<string>(), Arg.Any<RegistryView>())
                .Returns([]);

        Assert.Null(LegacyInstallation.FindOldGlobalConfig(registry, _ => true));
    }
}
