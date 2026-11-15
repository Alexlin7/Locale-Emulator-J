using Xunit;

namespace LEGUI.Tests;

public class InputBoxTests
{
    [WpfFact]
    public void Ok_IsDefaultButton_And_Cancel_IsCancelButton()
    {
        // Enter confirms and Esc cancels through the standard WPF button roles.
        var box = new InputBox();

        Assert.True(box.bOk.IsDefault);
        Assert.True(box.bCancel.IsCancel);
    }

    [WpfFact]
    public void Label_TargetsTextBox()
    {
        var box = new InputBox();

        Assert.Same(box.tbText, box.lText.Target);
    }

    [WpfTheory]
    [InlineData("")]
    [InlineData("   ")]
    public void Ok_Disabled_WhenTextIsBlank(string text)
    {
        var box = new InputBox { Text = text };

        Assert.False(box.bOk.IsEnabled);
    }

    [WpfFact]
    public void Ok_Enabled_WhenTextIsPresent()
    {
        var box = new InputBox { Text = "Run in Chinese" };

        Assert.True(box.bOk.IsEnabled);
    }

    [WpfFact]
    public void Ok_TracksTyping()
    {
        var box = new InputBox();
        Assert.False(box.bOk.IsEnabled);

        box.tbText.Text = "A";
        Assert.True(box.bOk.IsEnabled);

        box.tbText.Text = "";
        Assert.False(box.bOk.IsEnabled);
    }

    [WpfFact]
    public void Text_IsTrimmed()
    {
        var box = new InputBox { Text = "  Run in Chinese  " };

        Assert.Equal("Run in Chinese", box.Text);
    }
}
