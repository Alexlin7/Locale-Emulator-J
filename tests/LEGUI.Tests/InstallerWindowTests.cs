using Xunit;

namespace LEGUI.Tests;

/// <summary>
///     The installer window (LEGUI.exe --installer) always offers "Import ..." for an old
///     LEConfig.xml, not only when the original LE's context menu registration is still there.
/// </summary>
[Collection(WpfUiCollection.Name)]
public class InstallerWindowTests
{
    [WpfFact]
    public void HasImportButton_VisibleAndEnabled()
    {
        var window = new InstallerWindow();
        try
        {
            Assert.Equal(System.Windows.Visibility.Visible, window.bImportProfiles.Visibility);
            Assert.True(window.bImportProfiles.IsEnabled);
        }
        finally
        {
            window.Close();
        }
    }
}
