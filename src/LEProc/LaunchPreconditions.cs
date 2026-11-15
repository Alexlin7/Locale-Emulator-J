#nullable disable

using System.Reflection.PortableExecutable;
using LECommonLibrary;

namespace LEProc;

/// <summary>
///     Checks done before launching a target. Kept free of UI so they can be unit tested;
///     <see cref="Program" /> turns the results into message boxes.
/// </summary>
internal static class LaunchPreconditions
{
    internal enum AppProfileStatus
    {
        /// <summary>The .le.config file does not exist.</summary>
        NoConfig,

        /// <summary>The .le.config file exists and has at least one profile.</summary>
        Loaded,

        /// <summary>The .le.config file exists but no profile could be read from it.</summary>
        Unreadable
    }

    /// <summary>
    ///     Returns null when the target file exists, otherwise the error message to show.
    ///     Without this check a mistyped path (e.g. a shortcut whose path repeats its own
    ///     prefix) ends up in LEGUI, which reports the non-existent directory as
    ///     "not writable" after a UAC prompt.
    /// </summary>
    internal static string GetMissingTargetMessage(string path)
    {
        if (!string.IsNullOrEmpty(path) && File.Exists(path))
            return null;

        return "The target application was not found:\r\n" +
               $"{path}\r\n" +
               "\r\n" +
               "Please check the path (for example, the target of your shortcut).";
    }

    /// <summary>
    ///     Loads the first profile of an application's .le.config.
    /// </summary>
    internal static AppProfileStatus LoadAppProfile(string configPath, out LEProfile profile)
    {
        profile = default;

        if (!File.Exists(configPath))
            return AppProfileStatus.NoConfig;

        var profiles = LEConfig.GetProfiles(configPath);
        if (profiles.Length == 0)
            return AppProfileStatus.Unreadable;

        profile = profiles[0];
        return AppProfileStatus.Loaded;
    }

    internal enum TargetArchitecture
    {
        /// <summary>Not a readable PE image; launching is left to the existing error handling.</summary>
        Unknown,

        /// <summary>Native x86, or a managed image with 32BITREQUIRED (including "prefer 32-bit").</summary>
        X86,

        /// <summary>Managed IL-only image without 32BITREQUIRED: runs as 64-bit on 64-bit Windows.</summary>
        AnyCpu,

        X64,
        Arm64,

        /// <summary>Any other machine type (e.g. 32-bit ARM, Itanium).</summary>
        Other
    }

    /// <summary>
    ///     Reads the target's architecture from its PE headers. Only x86 targets can be emulated:
    ///     LEProc and the Core it calls are 32-bit (see docs/roadmap.md, x64 is on hold).
    /// </summary>
    internal static TargetArchitecture GetTargetArchitecture(string path)
    {
        try
        {
            using var stream = File.OpenRead(path);
            using var reader = new PEReader(stream);
            var headers = reader.PEHeaders;

            switch (headers.CoffHeader.Machine)
            {
                case Machine.I386:
                    var cor = headers.CorHeader;
                    if (cor != null &&
                        (cor.Flags & CorFlags.ILOnly) != 0 &&
                        (cor.Flags & CorFlags.Requires32Bit) == 0)
                        return TargetArchitecture.AnyCpu;
                    return TargetArchitecture.X86;

                case Machine.Amd64:
                    return TargetArchitecture.X64;

                case Machine.Arm64:
                    return TargetArchitecture.Arm64;

                default:
                    return TargetArchitecture.Other;
            }
        }
        catch (Exception e) when (e is IOException or BadImageFormatException or UnauthorizedAccessException)
        {
            return TargetArchitecture.Unknown;
        }
    }

    /// <summary>
    ///     Returns null when the target can be emulated, otherwise the message to show.
    ///     Unknown is let through so that files LEProc cannot read keep the existing behavior.
    /// </summary>
    internal static string GetUnsupportedArchitectureMessage(string path, TargetArchitecture architecture,
        bool is64BitOperatingSystem)
    {
        var supported = architecture switch
        {
            TargetArchitecture.Unknown or TargetArchitecture.X86 => true,
            TargetArchitecture.AnyCpu => !is64BitOperatingSystem,
            _ => false
        };
        if (supported)
            return null;

        return "This application runs as a 64-bit program:\r\n" +
               $"{path}\r\n" +
               "\r\n" +
               "Locale Emulator currently supports 32-bit (x86) applications only, " +
               "so it cannot apply locale emulation to this application.";
    }

    internal static string GetUnreadableConfigMessage(string configPath)
    {
        return "The profile file of this application could not be read:\r\n" +
               $"{configPath}\r\n" +
               "\r\n" +
               "It may be damaged or written by an incompatible version of Locale Emulator.\r\n" +
               "Open the profile editor to set it up again?";
    }
}
