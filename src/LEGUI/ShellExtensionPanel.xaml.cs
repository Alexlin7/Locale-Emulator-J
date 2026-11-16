#nullable disable

using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using LECommonLibrary;

namespace LEGUI;

public partial class ShellExtensionPanel : UserControl
{
    private IShellExtensionCommand _command;
    private string _dllPath;
    private bool _isAdmin;

    /// <summary>
    /// Replaceable message shower — defaults to MessageBox.Show.
    /// Tests can swap this out to avoid blocking on a modal dialog.
    /// </summary>
    internal Action<string> ShowMessage = text =>
        MessageBox.Show(text, I18n.AppName);

    /// <summary>
    /// Finds the original LE 2.x LEConfig.xml (null when there is none). Replaceable in tests.
    /// </summary>
    internal Func<string> FindOldConfig = () => null;

    /// <summary>
    /// Formats a localized message (key, arguments). Replaceable in tests, which have no
    /// WPF Application and therefore no language dictionary.
    /// </summary>
    internal Func<string, object[], string> FormatMessage = I18n.Format;

    /// <summary>
    /// Imports a config file into this installation's global profiles, asking about profiles
    /// whose name is already used (see <see cref="ProfileImportDialog.ImportIntoGlobalConfig" />).
    /// Throws when the import could not be saved. Replaceable in tests.
    /// </summary>
    internal Func<string, LEConfig.ImportResult> ImportProfiles;

    /// <summary>
    /// Removes the Mark of the Web from this LE folder after a successful install
    /// (see <see cref="DownloadMark"/>). No-op until InitializeForThisInstallation, so tests
    /// never touch real files. Replaceable in tests.
    /// </summary>
    internal Action RemoveDownloadMark = () => { };

    /// <summary>
    /// Raised after profiles were imported, so a window showing the profile list can reload it.
    /// </summary>
    public event EventHandler ProfilesImported;

    private string _oldConfigPath;

    public ShellExtensionPanel()
    {
        InitializeComponent();
        ImportProfiles = path => ProfileImportDialog.ImportIntoGlobalConfig(path, Window.GetWindow(this));
    }

    /// <summary>Wire up install/uninstall actions. AllUsers mode when !isAdmin is routed
    /// through a UAC-elevated sub-process via RunElevatedAsync.</summary>
    public void SetCommand(IShellExtensionCommand command, string dllPath, bool isAdmin)
    {
        _command = command;
        _dllPath = dllPath;
        _isAdmin = isAdmin;
    }

    /// <summary>
    /// Wires the panel to the real registry and the ShellExtension DLL of this LE folder
    /// (shared by the global config window and the installer window).
    /// </summary>
    public void InitializeForThisInstallation()
    {
        var installDir = ShellExtensionRegistrar.GetInstallDirectory();
        var dllPath = ShellExtensionRegistrar.GetShellExtensionDllPath(installDir);
        var registrar = new ShellExtensionRegistrar(
            new RegistryOperations(),
            ShellExtensionConstants.NewClsid);

        RemoveDownloadMark = () => DownloadMark.RemoveFromProductFiles(installDir);
        FindOldConfig = () => LegacyInstallation.FindOldGlobalConfig(new RegistryOperations(), File.Exists);
        SetCommand(registrar, dllPath, SystemHelper.IsAdministrator());
        RefreshStatus();
    }

    public void RefreshStatus()
    {
        if (_command == null) return;

        ShowModeStatus(ShellExtensionRegistrar.InstallMode.CurrentUser,
                       tStatusCurrentUser, tPathCurrentUser, bInstallCurrentUser, bUninstallCurrentUser);
        ShowModeStatus(ShellExtensionRegistrar.InstallMode.AllUsers,
                       tStatusAllUsers, tPathAllUsers, bInstallAllUsers, bUninstallAllUsers);

        var hasOld = _command.HasOldRegistration();
        cleanupSection.Visibility = hasOld ? Visibility.Visible : Visibility.Collapsed;

        _oldConfigPath = hasOld ? FindOldConfig() : null;
        importOldSection.Visibility = _oldConfigPath != null ? Visibility.Visible : Visibility.Collapsed;
        tOldConfigPath.Text = _oldConfigPath ?? string.Empty;
    }

    /// <summary>
    /// Installed here, not installed, or installed for another folder (LE moved, or a newer
    /// version extracted elsewhere): then the old path is shown and the install button
    /// becomes "Update to this folder", which registers this folder's DLL over it.
    /// </summary>
    private void ShowModeStatus(ShellExtensionRegistrar.InstallMode mode,
                                System.Windows.Documents.Run status,
                                TextBlock path,
                                Button install,
                                Button uninstall)
    {
        var installed = _command.IsInstalled(mode);
        var registered = installed ? _command.GetRegisteredDllPath(mode) : null;
        var elsewhere = registered != null && !IsSamePath(registered, _dllPath);

        string statusKey;
        if (!installed)
            statusKey = "NotInstalled";
        else if (elsewhere)
            statusKey = "InstalledElsewhere";
        else
            statusKey = "Installed";
        status.Text = " " + I18n.GetString(statusKey);

        path.Text = elsewhere ? registered : string.Empty;
        path.Visibility = elsewhere ? Visibility.Visible : Visibility.Collapsed;

        var installKey = mode == ShellExtensionRegistrar.InstallMode.AllUsers ? "InstallAllUsers" : "InstallCurrentUser";
        install.SetResourceReference(ContentControl.ContentProperty, elsewhere ? "UpdateToThisFolder" : installKey);
        install.IsEnabled = !installed || elsewhere;
        uninstall.IsEnabled = installed;
    }

    internal static bool IsSamePath(string a, string b)
    {
        if (string.IsNullOrEmpty(a) || string.IsNullOrEmpty(b))
            return string.IsNullOrEmpty(a) && string.IsNullOrEmpty(b);
        try
        {
            return string.Equals(Path.GetFullPath(a), Path.GetFullPath(b), StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return string.Equals(a, b, StringComparison.OrdinalIgnoreCase);
        }
    }

    private async void bInstallCurrentUser_Click(object sender, RoutedEventArgs e)
        => await HandleInstallAsync(ShellExtensionRegistrar.InstallMode.CurrentUser);

    private async void bInstallAllUsers_Click(object sender, RoutedEventArgs e)
        => await HandleInstallAsync(ShellExtensionRegistrar.InstallMode.AllUsers);

    private async void bUninstallCurrentUser_Click(object sender, RoutedEventArgs e)
        => await HandleUninstallAsync(ShellExtensionRegistrar.InstallMode.CurrentUser);

    private async void bUninstallAllUsers_Click(object sender, RoutedEventArgs e)
        => await HandleUninstallAsync(ShellExtensionRegistrar.InstallMode.AllUsers);

    private async void bCleanupOld_Click(object sender, RoutedEventArgs e)
    {
        // Import first: the cleanup deletes the registration that tells where the old folder is,
        // so it only runs once the import was saved. The import runs in this (non-elevated)
        // process because it writes this installation's LEConfig.xml.
        string importMessage = null;
        if (_oldConfigPath != null && cbImportOld.IsChecked == true)
        {
            LEConfig.ImportResult result;
            try
            {
                result = ImportProfiles(_oldConfigPath);
            }
            catch (Exception ex)
            {
                // Not saved: keep the old registration, the only record of where the old folder is.
                ShowMessage(FormatMessage("ShellExtImportFailedOldKept", [ex.Message]));
                return;
            }
            if (result.Imported > 0)
                ProfilesImported?.Invoke(this, EventArgs.Empty);
            importMessage = result.Imported + result.Skipped > 0
                ? FormatMessage("ImportProfilesResult", [result.Imported, result.Skipped])
                : FormatMessage("ImportProfilesNone", [_oldConfigPath]);
        }

        if (_isAdmin)
        {
            TryInProcess(() => _command?.CleanupOldRegistration());
        }
        else
        {
            await RunElevatedAsync(ShellExtCliCommand.VerbCleanupOld, null);
        }
        RefreshStatus();

        if (importMessage != null)
            ShowMessage(importMessage);
    }

    private async Task HandleInstallAsync(ShellExtensionRegistrar.InstallMode mode)
    {
        if (mode == ShellExtensionRegistrar.InstallMode.AllUsers && !_isAdmin)
        {
            if (!await RunElevatedAsync(ShellExtCliCommand.VerbInstall, ShellExtCliCommand.ScopeAllUsers))
            {
                RefreshStatus();
                return;
            }
        }
        else
        {
            if (!TryInProcess(() => _command?.Register(mode, _dllPath)))
            {
                RefreshStatus();
                return;
            }
        }
        // The user chose to trust this folder: stop the "Open File - Security Warning" dialog
        // that ShellExecute shows for every downloaded, unsigned LEProc.exe launch.
        RemoveDownloadMark();
        RefreshStatus();
        if (_command != null)
            ShowMessage(I18n.GetString("InstallSuccess"));
    }

    private async Task HandleUninstallAsync(ShellExtensionRegistrar.InstallMode mode)
    {
        if (mode == ShellExtensionRegistrar.InstallMode.AllUsers && !_isAdmin)
        {
            if (!await RunElevatedAsync(ShellExtCliCommand.VerbUninstall, ShellExtCliCommand.ScopeAllUsers))
            {
                RefreshStatus();
                return;
            }
        }
        else
        {
            if (!TryInProcess(() => _command?.Unregister(mode)))
            {
                RefreshStatus();
                return;
            }
        }
        RefreshStatus();
        if (_command != null)
            ShowMessage(I18n.GetString("UninstallSuccess"));
    }

    /// <summary>Run an in-process registry operation, showing a localized error and returning false on failure.</summary>
    private bool TryInProcess(Action op)
    {
        try
        {
            op();
            return true;
        }
        catch (Exception ex)
        {
            ShowMessage(string.Format(I18n.GetString("ShellExtOperationFailed"), ex.Message));
            return false;
        }
    }

    /// <summary>
    /// Spawn LEGUI.exe with --shell-ext CLI and "runas" verb to trigger UAC.
    /// Returns true on success (exit code 0), false on cancellation or failure.
    /// </summary>
    private async Task<bool> RunElevatedAsync(string verb, string scope)
    {
        SetAllButtonsEnabled(false);
        try
        {
            var args = scope != null
                ? $"{ShellExtCliCommand.Prefix} {verb} {scope}"
                : $"{ShellExtCliCommand.Prefix} {verb}";
            var psi = new ProcessStartInfo
            {
                FileName = Environment.ProcessPath,
                Arguments = args,
                Verb = "runas",
                UseShellExecute = true
            };

            using var p = Process.Start(psi);
            if (p == null) return false;

            await p.WaitForExitAsync();

            if (p.ExitCode != 0)
            {
                ShowMessage(string.Format(I18n.GetString("ShellExtOperationFailed"), $"exit code {p.ExitCode}"));
                return false;
            }
            return true;
        }
        catch (Win32Exception ex) when (ex.NativeErrorCode == 1223)
        {
            // User cancelled UAC prompt — silently return, do not show error.
            return false;
        }
        finally
        {
            SetAllButtonsEnabled(true);
        }
    }

    private void SetAllButtonsEnabled(bool enabled)
    {
        bInstallCurrentUser.IsEnabled = enabled;
        bUninstallCurrentUser.IsEnabled = enabled;
        bInstallAllUsers.IsEnabled = enabled;
        bUninstallAllUsers.IsEnabled = enabled;
        bCleanupOld.IsEnabled = enabled;
    }
}
