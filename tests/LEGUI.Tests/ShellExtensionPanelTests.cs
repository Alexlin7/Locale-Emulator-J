using NSubstitute;
using Xunit;

namespace LEGUI.Tests;

[Collection(WpfUiCollection.Name)]
public class ShellExtensionPanelTests
{
    [WpfFact]
    public void RefreshStatus_WhenCurrentUserInstalled_DisablesInstallButton()
    {
        var panel = new ShellExtensionPanel();
        var command = Substitute.For<IShellExtensionCommand>();
        command.IsInstalled(ShellExtensionRegistrar.InstallMode.CurrentUser).Returns(true);
        command.IsInstalled(ShellExtensionRegistrar.InstallMode.AllUsers).Returns(false);
        command.HasOldRegistration().Returns(false);

        panel.SetCommand(command, dllPath: string.Empty, isAdmin: true);
        panel.RefreshStatus();

        Assert.False(panel.bInstallCurrentUser.IsEnabled);
        Assert.True(panel.bUninstallCurrentUser.IsEnabled);
        Assert.True(panel.bInstallAllUsers.IsEnabled);
        Assert.False(panel.bUninstallAllUsers.IsEnabled);
    }

    [WpfFact]
    public void RefreshStatus_HasOldRegistration_ShowsCleanupSection()
    {
        var panel = new ShellExtensionPanel();
        var command = Substitute.For<IShellExtensionCommand>();
        command.IsInstalled(Arg.Any<ShellExtensionRegistrar.InstallMode>()).Returns(false);
        command.HasOldRegistration().Returns(true);

        panel.SetCommand(command, dllPath: string.Empty, isAdmin: true);
        panel.RefreshStatus();

        Assert.Equal(System.Windows.Visibility.Visible, panel.cleanupSection.Visibility);
    }

    [WpfFact]
    public void InstallCurrentUser_CallsRegisterWithCurrentUserMode()
    {
        var panel = new ShellExtensionPanel();
        panel.ShowMessage = _ => { }; // suppress dialog in tests
        var command = Substitute.For<IShellExtensionCommand>();
        command.IsInstalled(Arg.Any<ShellExtensionRegistrar.InstallMode>()).Returns(false);
        command.HasOldRegistration().Returns(false);

        panel.SetCommand(command, dllPath: @"C:\fake\ShellExtension.dll", isAdmin: true);

        panel.bInstallCurrentUser.RaiseEvent(
            new System.Windows.RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));

        command.Received().Register(
            ShellExtensionRegistrar.InstallMode.CurrentUser,
            @"C:\fake\ShellExtension.dll");
    }

    [WpfFact]
    public void Install_Succeeded_RemovesDownloadMark()
    {
        var panel = new ShellExtensionPanel();
        panel.ShowMessage = _ => { };
        var removed = 0;
        panel.RemoveDownloadMark = () => removed++;
        var command = Substitute.For<IShellExtensionCommand>();
        command.IsInstalled(Arg.Any<ShellExtensionRegistrar.InstallMode>()).Returns(false);

        panel.SetCommand(command, dllPath: @"C:\fake\ShellExtension.dll", isAdmin: true);
        panel.bInstallAllUsers.RaiseEvent(
            new System.Windows.RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));

        Assert.Equal(1, removed);
    }

    [WpfFact]
    public void Install_Failed_KeepsDownloadMark()
    {
        var panel = new ShellExtensionPanel();
        panel.ShowMessage = _ => { };
        var removed = 0;
        panel.RemoveDownloadMark = () => removed++;
        var command = Substitute.For<IShellExtensionCommand>();
        command.IsInstalled(Arg.Any<ShellExtensionRegistrar.InstallMode>()).Returns(false);
        command.When(c => c.Register(Arg.Any<ShellExtensionRegistrar.InstallMode>(), Arg.Any<string>()))
               .Do(_ => throw new UnauthorizedAccessException());

        panel.SetCommand(command, dllPath: @"C:\fake\ShellExtension.dll", isAdmin: true);
        panel.bInstallCurrentUser.RaiseEvent(
            new System.Windows.RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));

        Assert.Equal(0, removed);
    }

    private const string ThisDll = @"D:\LE-new\ShellExtension64.dll";

    private static (ShellExtensionPanel panel, IShellExtensionCommand command) CreateInstalledPanel(string? registeredPath)
    {
        var panel = new ShellExtensionPanel();
        panel.ShowMessage = _ => { };
        var command = Substitute.For<IShellExtensionCommand>();
        command.IsInstalled(ShellExtensionRegistrar.InstallMode.CurrentUser).Returns(true);
        command.IsInstalled(ShellExtensionRegistrar.InstallMode.AllUsers).Returns(false);
        command.GetRegisteredDllPath(ShellExtensionRegistrar.InstallMode.CurrentUser).Returns(registeredPath);
        command.HasOldRegistration().Returns(false);

        panel.SetCommand(command, dllPath: ThisDll, isAdmin: true);
        panel.RefreshStatus();
        return (panel, command);
    }

    [WpfFact]
    public void RefreshStatus_RegisteredToThisFolder_IsInstalled_WithoutPath()
    {
        var (panel, _) = CreateInstalledPanel(@"d:\le-new\.\SHELLEXTENSION64.dll");

        Assert.False(panel.bInstallCurrentUser.IsEnabled);
        Assert.True(panel.bUninstallCurrentUser.IsEnabled);
        Assert.Equal(System.Windows.Visibility.Collapsed, panel.tPathCurrentUser.Visibility);
    }

    [WpfFact]
    public void RefreshStatus_RegisteredToAnotherFolder_ShowsThatPath_AndOffersUpdate()
    {
        // A newer version extracted to another folder: the menu still runs the old copy.
        var (panel, _) = CreateInstalledPanel(@"D:\LE-old\ShellExtension64.dll");

        Assert.True(panel.bInstallCurrentUser.IsEnabled);
        Assert.True(panel.bUninstallCurrentUser.IsEnabled);
        Assert.Equal(System.Windows.Visibility.Visible, panel.tPathCurrentUser.Visibility);
        Assert.Equal(@"D:\LE-old\ShellExtension64.dll", panel.tPathCurrentUser.Text);
        Assert.Equal(System.Windows.Visibility.Collapsed, panel.tPathAllUsers.Visibility);
    }

    [WpfFact]
    public void UpdateToThisFolder_RegistersThisFoldersDll()
    {
        var (panel, command) = CreateInstalledPanel(@"D:\LE-old\ShellExtension64.dll");

        panel.bInstallCurrentUser.RaiseEvent(
            new System.Windows.RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));

        command.Received().Register(ShellExtensionRegistrar.InstallMode.CurrentUser, ThisDll);
    }

    [Theory]
    [InlineData(@"C:\LE\ShellExtension.dll", @"c:\le\shellextension.dll", true)]
    [InlineData(@"C:\LE\ShellExtension.dll", @"C:\LE\sub\..\ShellExtension.dll", true)]
    [InlineData(@"C:\LE\ShellExtension.dll", @"C:\LE2\ShellExtension.dll", false)]
    [InlineData(@"C:\LE\ShellExtension.dll", "", false)]
    public void IsSamePath(string a, string b, bool expected)
    {
        Assert.Equal(expected, ShellExtensionPanel.IsSamePath(a, b));
    }
}
