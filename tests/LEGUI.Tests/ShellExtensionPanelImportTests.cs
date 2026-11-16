using LECommonLibrary;
using NSubstitute;
using Xunit;

namespace LEGUI.Tests;

/// <summary>
///     "Clean up old registration" can first import the profiles of the original LE 2.x.
///     The import must run before the cleanup: the old folder is only known from the
///     registration that the cleanup deletes.
/// </summary>
[Collection(WpfUiCollection.Name)]
public class ShellExtensionPanelImportTests
{
    private const string OldConfig = @"D:\OldLE\LEConfig.xml";

    private static (ShellExtensionPanel panel, IShellExtensionCommand command, List<string> calls, List<string> messages)
        CreatePanel(string? oldConfig, LEConfig.ImportResult importResult = default, Exception? importError = null)
    {
        var calls = new List<string>();
        var messages = new List<string>();
        var panel = new ShellExtensionPanel();
        panel.ShowMessage = messages.Add;
        panel.FormatMessage = (key, args) => key + ":" + string.Join("|", args);
        panel.FindOldConfig = () => oldConfig;
        panel.ImportProfiles = path =>
        {
            calls.Add("import " + path);
            if (importError != null)
                throw importError;
            return importResult;
        };

        var command = Substitute.For<IShellExtensionCommand>();
        command.IsInstalled(Arg.Any<ShellExtensionRegistrar.InstallMode>()).Returns(false);
        command.HasOldRegistration().Returns(true);
        command.When(c => c.CleanupOldRegistration()).Do(_ => calls.Add("cleanup"));

        panel.SetCommand(command, dllPath: string.Empty, isAdmin: true);
        panel.RefreshStatus();
        return (panel, command, calls, messages);
    }

    private static void ClickCleanup(ShellExtensionPanel panel) =>
        panel.bCleanupOld.RaiseEvent(
            new System.Windows.RoutedEventArgs(System.Windows.Controls.Primitives.ButtonBase.ClickEvent));

    [WpfFact]
    public void OldConfigFound_ShowsCheckedImportOption_WithPath()
    {
        var (panel, _, _, _) = CreatePanel(OldConfig);

        Assert.Equal(System.Windows.Visibility.Visible, panel.importOldSection.Visibility);
        Assert.True(panel.cbImportOld.IsChecked);
        Assert.Equal(OldConfig, panel.tOldConfigPath.Text);
    }

    [WpfFact]
    public void NoOldConfig_HidesImportOption()
    {
        var (panel, _, _, _) = CreatePanel(oldConfig: null);

        Assert.Equal(System.Windows.Visibility.Collapsed, panel.importOldSection.Visibility);
    }

    [WpfFact]
    public void Cleanup_WithImportChecked_ImportsBeforeCleanup_AndReportsResult()
    {
        var (panel, _, calls, messages) = CreatePanel(OldConfig, new LEConfig.ImportResult(3, 1));
        var raised = 0;
        panel.ProfilesImported += (_, _) => raised++;

        ClickCleanup(panel);

        Assert.Equal(new[] { "import " + OldConfig, "cleanup" }, calls);
        Assert.Equal(1, raised);
        Assert.Equal(new[] { "ImportProfilesResult:3|1" }, messages);
    }

    [WpfFact]
    public void Cleanup_WithImportUnchecked_OnlyCleansUp()
    {
        var (panel, _, calls, _) = CreatePanel(OldConfig);
        panel.cbImportOld.IsChecked = false;

        ClickCleanup(panel);

        Assert.Equal(new[] { "cleanup" }, calls);
    }

    [WpfFact]
    public void Cleanup_ImportFindsNoProfiles_SaysSo_AndStillCleansUp()
    {
        var (panel, _, calls, messages) = CreatePanel(OldConfig, new LEConfig.ImportResult(0, 0));
        var raised = 0;
        panel.ProfilesImported += (_, _) => raised++;

        ClickCleanup(panel);

        Assert.Equal(new[] { "import " + OldConfig, "cleanup" }, calls);
        Assert.Equal(0, raised);
        Assert.Equal(new[] { "ImportProfilesNone:" + OldConfig }, messages);
    }

    [WpfFact]
    public void Cleanup_ImportCannotBeSaved_KeepsOldRegistration_AndSaysWhy()
    {
        // The registration is the only record of where the old LEConfig.xml is.
        var (panel, _, calls, messages) =
            CreatePanel(OldConfig, importError: new UnauthorizedAccessException("access denied"));
        var raised = 0;
        panel.ProfilesImported += (_, _) => raised++;

        ClickCleanup(panel);

        Assert.Equal(new[] { "import " + OldConfig }, calls);
        Assert.Equal(0, raised);
        Assert.Equal(new[] { "ShellExtImportFailedOldKept:access denied" }, messages);
        Assert.Equal(System.Windows.Visibility.Visible, panel.cleanupSection.Visibility);
    }
}
