#nullable disable

using System.IO;
using System.Windows;
using LECommonLibrary;

namespace LEGUI;

/// <summary>
///     "Import ...": pick another LEConfig.xml (e.g. of the original LE 2.x) and merge its
///     profiles into this installation's global profiles (<see cref="LEConfig.ImportGlobalProfiles" />).
///     Shared by the global config window and the installer window.
/// </summary>
internal static class ProfileImportDialog
{
    /// <summary>Returns true when at least one profile was imported.</summary>
    internal static bool Run(Window owner)
    {
        var dialog = new Microsoft.Win32.OpenFileDialog
        {
            Title = I18n.GetString("ImportProfiles").TrimEnd('.', '…', ' '),
            Filter = "LEConfig.xml|LEConfig.xml|XML (*.xml)|*.xml",
            CheckFileExists = true
        };
        if (dialog.ShowDialog(owner) != true)
            return false;

        var dir = Path.GetDirectoryName(LEConfig.GlobalConfigPath);
        if (!SystemHelper.CheckPermission(dir))
        {
            MessageBox.Show(owner, I18n.Format("ErrorInstallDirNotWritable", dir), I18n.AppName,
                            MessageBoxButton.OK, MessageBoxImage.Error);
            return false;
        }

        // The installer window never creates LEConfig.xml. Create it with the default
        // profiles first, or the import would leave only the imported ones.
        LEConfig.CheckGlobalConfigFile(true);

        var result = LEConfig.ImportGlobalProfiles(dialog.FileName);
        MessageBox.Show(owner,
                        result.Imported + result.Skipped > 0
                            ? I18n.Format("ImportProfilesResult", result.Imported, result.Skipped)
                            : I18n.Format("ImportProfilesNone", dialog.FileName),
                        I18n.AppName);
        return result.Imported > 0;
    }
}
