#nullable disable

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

    internal static string GetUnreadableConfigMessage(string configPath)
    {
        return "The profile file of this application could not be read:\r\n" +
               $"{configPath}\r\n" +
               "\r\n" +
               "It may be damaged or written by an incompatible version of Locale Emulator.\r\n" +
               "Open the profile editor to set it up again?";
    }
}
