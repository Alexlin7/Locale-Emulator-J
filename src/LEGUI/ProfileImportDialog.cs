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

        LEConfig.ImportResult result;
        try
        {
            result = ImportIntoGlobalConfig(dialog.FileName, owner);
        }
        catch (Exception ex)
        {
            MessageBox.Show(owner, I18n.Format("ImportProfilesFailed", ex.Message), I18n.AppName,
                            MessageBoxButton.OK, MessageBoxImage.Error);
            return false;
        }

        MessageBox.Show(owner,
                        result.Imported + result.Skipped > 0
                            ? I18n.Format("ImportProfilesResult", result.Imported, result.Skipped)
                            : I18n.Format("ImportProfilesNone", dialog.FileName),
                        I18n.AppName);
        return result.Imported > 0;
    }

    /// <summary>
    ///     Imports into the global LEConfig.xml, asking about profiles whose name is already used.
    ///     When LEConfig.xml does not exist yet (the installer window never creates it), the old
    ///     profiles are written as they are; the default profiles are only created afterwards
    ///     when nothing was imported. Throws when the import could not be saved.
    /// </summary>
    internal static LEConfig.ImportResult ImportIntoGlobalConfig(string sourcePath, Window owner)
    {
        var result = LEConfig.ImportGlobalProfiles(sourcePath, (_, incoming, isNameTaken) =>
                                                       AskConflict(owner, incoming, isNameTaken));
        LEConfig.CheckGlobalConfigFile(true);
        return result;
    }

    /// <summary>
    ///     An imported profile has the name of an existing one with different settings. Profile
    ///     names cannot be edited later, so the user picks the new name when saving it separately.
    /// </summary>
    private static LEConfig.ImportConflictDecision AskConflict(Window owner,
                                                               LEProfile incoming,
                                                               Func<string, bool> isNameTaken)
    {
        var answer = MessageBox.Show(owner,
                                     I18n.Format("ImportConflict", incoming.Name),
                                     I18n.AppName,
                                     MessageBoxButton.YesNoCancel,
                                     MessageBoxImage.Question,
                                     MessageBoxResult.Cancel);
        if (answer == MessageBoxResult.Yes)
            return LEConfig.ImportConflictDecision.Replace;
        if (answer != MessageBoxResult.No)
            return LEConfig.ImportConflictDecision.Skip;

        var name = incoming.Name;
        for (;;)
        {
            var ib = new InputBox
            {
                Owner = owner,
                Title = I18n.GetString("SaveAs").TrimEnd('.', '…', ' '),
                Instruction = I18n.GetString("SaveAsInstruction"),
                OkText = I18n.GetString("Save"),
                CancelText = I18n.GetString("Cancel"),
                Text = name
            };
            if (ib.ShowDialog() != true || string.IsNullOrEmpty(ib.Text))
                return LEConfig.ImportConflictDecision.Skip;

            name = ib.Text;
            if (!isNameTaken(name))
                return LEConfig.ImportConflictDecision.SaveAs(name);

            MessageBox.Show(owner, I18n.Format("ProfileNameExists", name), I18n.AppName,
                            MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }
}
