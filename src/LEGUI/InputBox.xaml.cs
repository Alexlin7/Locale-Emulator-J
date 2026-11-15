#nullable disable

using System.Windows;
using System.Windows.Controls;

namespace LEGUI;

/// <summary>
///     Single-line text prompt (e.g. the name of a new global profile).
///     Enter confirms (<see cref="Button.IsDefault" />), Esc cancels (<see cref="Button.IsCancel" />),
///     and the confirm button stays disabled while the text is blank.
/// </summary>
public partial class InputBox : Window
{
    public InputBox()
    {
        InitializeComponent();

        // Set here rather than with an ElementName binding, which only resolves once shown.
        lText.Target = tbText;
    }

    public string Instruction
    {
        get { return lText.Content?.ToString(); }
        set { lText.Content = value; }
    }

    /// <summary>The entered text without leading or trailing white space.</summary>
    public string Text
    {
        get { return tbText.Text.Trim(); }
        set { tbText.Text = value; }
    }

    public string OkText
    {
        get { return bOk.Content.ToString(); }
        set { bOk.Content = value; }
    }

    public string CancelText
    {
        get { return bCancel.Content.ToString(); }
        set { bCancel.Content = value; }
    }

    private void Window_Loaded(object sender, RoutedEventArgs e)
    {
        tbText.Focus();
        tbText.SelectAll();
    }

    private void tbText_TextChanged(object sender, TextChangedEventArgs e)
    {
        bOk.IsEnabled = !string.IsNullOrWhiteSpace(tbText.Text);
    }

    private void bOk_Click(object sender, RoutedEventArgs e)
    {
        DialogResult = true;
    }
}
