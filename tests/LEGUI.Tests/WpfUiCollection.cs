using Xunit;

namespace LEGUI.Tests;

/// <summary>
///     Test classes that create WPF controls run one after another. xUnit runs classes in
///     parallel, each [WpfFact] on its own STA thread, and InitializeComponent
///     (Application.LoadComponent) is not safe to call from several threads at once:
///     it intermittently throws ArgumentOutOfRangeException in PackagePart.
/// </summary>
[CollectionDefinition(Name)]
public sealed class WpfUiCollection
{
    public const string Name = "WPF UI";
}
