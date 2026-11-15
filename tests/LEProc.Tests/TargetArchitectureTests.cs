using Xunit;
using static LEProc.LaunchPreconditions;

namespace LEProc.Tests;

/// <summary>
///     LEProc only injects into 32-bit (x86) targets. These tests cover how the target's
///     architecture is read from its PE headers and when the "64-bit not supported" message is shown.
/// </summary>
public class TargetArchitectureTests : IDisposable
{
    private const ushort MachineI386 = 0x014c;
    private const ushort MachineAmd64 = 0x8664;
    private const ushort MachineArm64 = 0xaa64;
    private const ushort MachineArmNt = 0x01c4;

    private readonly string _tempDir;

    public TargetArchitectureTests()
    {
        _tempDir = Path.Combine(Path.GetTempPath(), "TargetArchitectureTests_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_tempDir);
    }

    public void Dispose()
    {
        if (Directory.Exists(_tempDir))
            Directory.Delete(_tempDir, true);
    }

    /// <summary>
    ///     Writes a minimal native PE image (headers only, no sections) with the given machine type.
    /// </summary>
    private string CreateNativePe(string name, ushort machine, bool pe32Plus)
    {
        const int peOffset = 0x40;
        var optionalHeaderSize = (pe32Plus ? 112 : 96) + 16 * 8;
        var bytes = new byte[0x200];

        bytes[0] = (byte)'M';
        bytes[1] = (byte)'Z';
        BitConverter.GetBytes(peOffset).CopyTo(bytes, 0x3C);

        var p = peOffset;
        bytes[p] = (byte)'P';
        bytes[p + 1] = (byte)'E';
        p += 4;

        // COFF file header
        BitConverter.GetBytes(machine).CopyTo(bytes, p);
        BitConverter.GetBytes((ushort)0).CopyTo(bytes, p + 2); // NumberOfSections
        BitConverter.GetBytes((ushort)optionalHeaderSize).CopyTo(bytes, p + 16);
        BitConverter.GetBytes((ushort)0x0102).CopyTo(bytes, p + 18); // EXECUTABLE_IMAGE | 32BIT_MACHINE
        p += 20;

        // Optional header
        BitConverter.GetBytes((ushort)(pe32Plus ? 0x20b : 0x10b)).CopyTo(bytes, p);
        BitConverter.GetBytes(0x200).CopyTo(bytes, p + 32); // SectionAlignment
        BitConverter.GetBytes(0x200).CopyTo(bytes, p + 36); // FileAlignment
        BitConverter.GetBytes(0x200).CopyTo(bytes, p + 56); // SizeOfImage
        BitConverter.GetBytes(0x200).CopyTo(bytes, p + 60); // SizeOfHeaders
        BitConverter.GetBytes(16).CopyTo(bytes, p + (pe32Plus ? 108 : 92)); // NumberOfRvaAndSizes

        var path = Path.Combine(_tempDir, name);
        File.WriteAllBytes(path, bytes);
        return path;
    }

    // ---- GetTargetArchitecture ----

    [Fact]
    public void GetTargetArchitecture_NativeX86_ReturnsX86()
    {
        var exe = CreateNativePe("x86.exe", MachineI386, pe32Plus: false);

        Assert.Equal(TargetArchitecture.X86, GetTargetArchitecture(exe));
    }

    [Fact]
    public void GetTargetArchitecture_NativeX64_ReturnsX64()
    {
        var exe = CreateNativePe("x64.exe", MachineAmd64, pe32Plus: true);

        Assert.Equal(TargetArchitecture.X64, GetTargetArchitecture(exe));
    }

    [Fact]
    public void GetTargetArchitecture_NativeArm64_ReturnsArm64()
    {
        var exe = CreateNativePe("arm64.exe", MachineArm64, pe32Plus: true);

        Assert.Equal(TargetArchitecture.Arm64, GetTargetArchitecture(exe));
    }

    [Fact]
    public void GetTargetArchitecture_OtherMachine_ReturnsOther()
    {
        var exe = CreateNativePe("arm.exe", MachineArmNt, pe32Plus: false);

        Assert.Equal(TargetArchitecture.Other, GetTargetArchitecture(exe));
    }

    [Fact]
    public void GetTargetArchitecture_ManagedRequires32Bit_ReturnsX86()
    {
        // LEProc.dll is built with PlatformTarget=x86, so its CLR header has 32BITREQUIRED set.
        var dll = typeof(LaunchPreconditions).Assembly.Location;

        Assert.Equal(TargetArchitecture.X86, GetTargetArchitecture(dll));
    }

    [Fact]
    public void GetTargetArchitecture_ManagedAnyCpu_ReturnsAnyCpu()
    {
        // LECommonLibrary.dll is AnyCPU: machine i386 + IL only, without 32BITREQUIRED.
        // A .NET Framework exe built like this runs as 64-bit on 64-bit Windows.
        // (Not the test assembly itself: `dotnet test --arch x86` builds it as x86.)
        var dll = typeof(LECommonLibrary.LEConfig).Assembly.Location;

        Assert.Equal(TargetArchitecture.AnyCpu, GetTargetArchitecture(dll));
    }

    [Theory]
    [InlineData("")]
    [InlineData("not a PE file")]
    [InlineData("MZ but nothing else")]
    public void GetTargetArchitecture_NotAPeFile_ReturnsUnknown(string content)
    {
        var path = Path.Combine(_tempDir, "broken.exe");
        File.WriteAllText(path, content);

        Assert.Equal(TargetArchitecture.Unknown, GetTargetArchitecture(path));
    }

    [Fact]
    public void GetTargetArchitecture_MissingFile_ReturnsUnknown()
    {
        Assert.Equal(TargetArchitecture.Unknown, GetTargetArchitecture(Path.Combine(_tempDir, "missing.exe")));
    }

    // ---- GetUnsupportedArchitectureMessage ----

    // TargetArchitecture is internal, so theories take its name (a public test method
    // cannot have an internal parameter type).

    [Theory]
    [InlineData(nameof(TargetArchitecture.X86), true)]
    [InlineData(nameof(TargetArchitecture.X86), false)]
    [InlineData(nameof(TargetArchitecture.AnyCpu), false)]
    [InlineData(nameof(TargetArchitecture.Unknown), true)]
    public void GetUnsupportedArchitectureMessage_Supported_ReturnsNull(string arch, bool is64BitOs)
    {
        Assert.Null(GetUnsupportedArchitectureMessage(@"C:\game\game.exe",
            Enum.Parse<TargetArchitecture>(arch), is64BitOs));
    }

    [Theory]
    [InlineData(nameof(TargetArchitecture.X64))]
    [InlineData(nameof(TargetArchitecture.Arm64))]
    [InlineData(nameof(TargetArchitecture.AnyCpu))]
    [InlineData(nameof(TargetArchitecture.Other))]
    public void GetUnsupportedArchitectureMessage_Unsupported_ReturnsMessageWithPath(string arch)
    {
        const string path = @"C:\game\game.exe";

        var message = GetUnsupportedArchitectureMessage(path, Enum.Parse<TargetArchitecture>(arch),
            is64BitOperatingSystem: true);

        Assert.NotNull(message);
        Assert.Contains(path, message);
        Assert.Contains("32-bit", message);
    }
}
