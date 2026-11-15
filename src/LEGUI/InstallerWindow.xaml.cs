#nullable disable

using System.Windows;

namespace LEGUI;

/// <summary>
///     Window for <c>LEGUI.exe --installer</c>: installs / uninstalls the context menu
///     without the profile editor, and imports an old LEConfig.xml ("Import ..."). It only
///     touches LEConfig.xml when importing (ProfileImportDialog).
/// </summary>
public partial class InstallerWindow : Window
{
    public InstallerWindow()
    {
        InitializeComponent();

        // Built from existing strings so no new keys are needed in the 22 language files.
        Title = $"{I18n.AppName} - {I18n.GetString("InstallShellExtTitle")}";

        shellExtPanel.InitializeForThisInstallation();
    }

    private void bImportProfiles_Click(object sender, RoutedEventArgs e)
    {
        ProfileImportDialog.Run(this);
    }
}
