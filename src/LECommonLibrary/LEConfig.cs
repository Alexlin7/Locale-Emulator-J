#nullable disable

using System.Reflection;
using System.Xml.Linq;

namespace LECommonLibrary;

public static class LEConfig
{
    public static string GlobalConfigPath =
        Path.Combine(Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location),
                     "LEConfig.xml");

    public static LEProfile GetProfile(string name)
    {
        try
        {
            return GetProfiles(GlobalConfigPath).First(p => p.Name == name);
        }
        catch
        {
            return new LEProfile();
        }
    }

    public static LEProfile[] GetProfiles()
    {
        return GetProfiles(GlobalConfigPath);
    }

    public static LEProfile[] GetProfiles(string configPath)
    {
        try
        {
            return ReadProfiles(configPath);
        }
        catch
        {
            return [];
        }
    }

    /// <summary>Reads the profiles of a config file; throws when it is missing or malformed.</summary>
    private static LEProfile[] ReadProfiles(string configPath)
    {
        var dict = XDocument.Load(configPath);

        var pros = from i in dict.Descendants("LEConfig").Elements("Profiles").Elements()
                   select i;

        return pros.Select(p => new LEProfile(p.Attribute("Name").Value,
                                              p.Attribute("Guid").Value,
                                              bool.Parse(p.Attribute("MainMenu").Value),
                                              p.Element("Parameter").Value,
                                              GetRegion(p),
                                              p.Element("Timezone").Value,
                                              bool.Parse(p.TryGetValue("RunAsAdmin", "false")),
                                              bool.Parse(p.TryGetValue("RedirectRegistry", "true")),
                                              bool.Parse(p.TryGetValue("IsAdvancedRedirection", "false")),
                                              bool.Parse(p.TryGetValue("RunWithSuspend", "false"))
                                   )
                   ).ToArray();
    }

    public static bool CheckGlobalConfigFile(bool buildNewConfig)
    {
        if (buildNewConfig && !File.Exists(GlobalConfigPath))
            BuildGlobalConfigFile();

        return File.Exists(GlobalConfigPath);
    }

    public static void SaveGlobalConfigFile(params LEProfile[] profiles)
    {
        WriteConfig(GlobalConfigPath, profiles);
    }

    public static void SaveApplicationConfigFile(string path, LEProfile profile)
    {
        WriteConfig(path, profile);
    }

    /// <summary>
    ///     Number of profiles imported (added, replaced or saved under a new name) and skipped
    ///     (identical to an existing one, or the user chose to skip it).
    /// </summary>
    public readonly record struct ImportResult(int Imported, int Skipped);

    /// <summary>What to do with an incoming profile whose name is already used by a different one.</summary>
    public enum ImportConflictChoice
    {
        Skip,
        Replace,
        SaveAs
    }

    /// <summary>The answer to an import conflict; <see cref="NewName" /> is used with SaveAs.</summary>
    public readonly record struct ImportConflictDecision(ImportConflictChoice Choice, string NewName = null)
    {
        public static ImportConflictDecision Skip => new(ImportConflictChoice.Skip);
        public static ImportConflictDecision Replace => new(ImportConflictChoice.Replace);
        public static ImportConflictDecision SaveAs(string newName) => new(ImportConflictChoice.SaveAs, newName);
    }

    /// <summary>
    ///     Asked when an incoming profile has the name of an existing profile with different
    ///     settings. <paramref name="isNameTaken" /> tells whether a name for SaveAs is already used.
    /// </summary>
    public delegate ImportConflictDecision ImportConflictResolver(LEProfile existing,
                                                                  LEProfile incoming,
                                                                  Func<string, bool> isNameTaken);

    /// <summary>Same as <see cref="MergeProfiles(LEProfile[], LEProfile[], ImportConflictResolver, out ImportResult)" /> with every conflict skipped.</summary>
    public static LEProfile[] MergeProfiles(LEProfile[] existing, LEProfile[] incoming, out ImportResult result)
    {
        return MergeProfiles(existing, incoming, null, out result);
    }

    /// <summary>
    ///     Appends the incoming profiles to the existing ones. Imported profiles keep their Guid,
    ///     which the context menu uses to find them (-runas {guid}); a missing Guid gets a new one.
    ///     <list type="bullet">
    ///         <item>Same name, same settings: skipped.</item>
    ///         <item>
    ///             Same name, different settings: <paramref name="resolve" /> decides (null skips).
    ///             Replace keeps the existing name and Guid with the incoming settings; SaveAs adds
    ///             it under a name that is not used yet (otherwise it is skipped). Profile names
    ///             cannot be edited in LEGUI, so a generated name like "Name (2)" is never used.
    ///         </item>
    ///         <item>Same Guid, other name: skipped when the settings are the same, else added with a new Guid.</item>
    ///     </list>
    /// </summary>
    public static LEProfile[] MergeProfiles(LEProfile[] existing,
                                            LEProfile[] incoming,
                                            ImportConflictResolver resolve,
                                            out ImportResult result)
    {
        var merged = existing.ToList();
        int imported = 0, skipped = 0;

        bool IsNameTaken(string name) => merged.Any(p => string.Equals(p.Name ?? string.Empty, name ?? string.Empty, StringComparison.Ordinal));
        bool IsGuidTaken(string guid) => merged.Any(p => string.Equals(p.Guid, guid, StringComparison.OrdinalIgnoreCase));

        foreach (var profile in incoming)
        {
            var p = profile;
            if (string.IsNullOrEmpty(p.Guid))
                p.Guid = Guid.NewGuid().ToString();

            var sameName = merged.FindIndex(e => string.Equals(e.Name ?? string.Empty, p.Name ?? string.Empty, StringComparison.Ordinal));
            if (sameName >= 0)
            {
                var current = merged[sameName];
                var decision = HasSameSettings(current, p)
                                   ? ImportConflictDecision.Skip
                                   : resolve?.Invoke(current, p, IsNameTaken) ?? ImportConflictDecision.Skip;

                switch (decision.Choice)
                {
                    case ImportConflictChoice.Replace:
                        p.Name = current.Name;
                        p.Guid = current.Guid;
                        merged[sameName] = p;
                        imported++;
                        break;

                    case ImportConflictChoice.SaveAs when !string.IsNullOrWhiteSpace(decision.NewName) &&
                                                          !IsNameTaken(decision.NewName.Trim()):
                        p.Name = decision.NewName.Trim();
                        if (IsGuidTaken(p.Guid))
                            p.Guid = Guid.NewGuid().ToString();
                        merged.Add(p);
                        imported++;
                        break;

                    default:
                        skipped++;
                        break;
                }
                continue;
            }

            var sameGuid = merged.FindIndex(e => string.Equals(e.Guid, p.Guid, StringComparison.OrdinalIgnoreCase));
            if (sameGuid >= 0)
            {
                if (HasSameSettings(merged[sameGuid], p))
                {
                    skipped++;
                    continue;
                }
                p.Guid = Guid.NewGuid().ToString();
            }

            merged.Add(p);
            imported++;
        }

        result = new ImportResult(imported, skipped);
        return merged.ToArray();
    }

    /// <summary>Everything except Name and Guid is equal.</summary>
    public static bool HasSameSettings(LEProfile a, LEProfile b)
    {
        return a.ShowInMainMenu == b.ShowInMainMenu &&
               string.Equals(a.Parameter ?? string.Empty, b.Parameter ?? string.Empty, StringComparison.Ordinal) &&
               string.Equals(a.Region ?? string.Empty, b.Region ?? string.Empty, StringComparison.Ordinal) &&
               string.Equals(a.Timezone ?? string.Empty, b.Timezone ?? string.Empty, StringComparison.Ordinal) &&
               a.RunAsAdmin == b.RunAsAdmin &&
               a.RedirectRegistry == b.RedirectRegistry &&
               a.IsAdvancedRedirection == b.IsAdvancedRedirection &&
               a.RunWithSuspend == b.RunWithSuspend;
    }

    /// <summary>
    ///     Imports the profiles of another config file (e.g. LEConfig.xml of the original LE 2.x)
    ///     into targetConfigPath with <see cref="MergeProfiles(LEProfile[], LEProfile[], ImportConflictResolver, out ImportResult)" />.
    ///     <para>
    ///         When the target does not exist yet (first migration), it gets only the imported
    ///         profiles: no default profiles that would win over the old ones with the same name.
    ///         Callers create the default config afterwards when nothing was imported.
    ///     </para>
    ///     <para>
    ///         An unreadable source, or the target itself, imports nothing. The target is only
    ///         rewritten when something was imported. Unlike <see cref="SaveGlobalConfigFile" />,
    ///         failures throw (an existing target that cannot be read, a write that fails or
    ///         cannot be read back), so callers never report an import that was not saved.
    ///     </para>
    /// </summary>
    public static ImportResult ImportProfiles(string sourceConfigPath,
                                              string targetConfigPath,
                                              ImportConflictResolver resolve = null)
    {
        if (string.Equals(Path.GetFullPath(sourceConfigPath), Path.GetFullPath(targetConfigPath),
                          StringComparison.OrdinalIgnoreCase))
            return new ImportResult(0, 0);

        var incoming = GetProfiles(sourceConfigPath);
        if (incoming.Length == 0)
            return new ImportResult(0, 0);

        var existing = File.Exists(targetConfigPath) ? ReadProfiles(targetConfigPath) : Array.Empty<LEProfile>();
        var merged = MergeProfiles(existing, incoming, resolve, out var result);
        if (result.Imported > 0)
        {
            BuildConfig(merged).Save(targetConfigPath);
            VerifySaved(targetConfigPath, merged);
        }
        return result;
    }

    /// <summary>Imports into this installation's global LEConfig.xml.</summary>
    public static ImportResult ImportGlobalProfiles(string sourceConfigPath, ImportConflictResolver resolve = null)
    {
        return ImportProfiles(sourceConfigPath, GlobalConfigPath, resolve);
    }

    private static void VerifySaved(string path, LEProfile[] expected)
    {
        var saved = ReadProfiles(path);
        if (saved.Length != expected.Length ||
            !saved.Zip(expected).All(pair => pair.First.Name == pair.Second.Name &&
                                             string.Equals(pair.First.Guid, pair.Second.Guid, StringComparison.OrdinalIgnoreCase)))
            throw new IOException($"{path} does not contain the imported profiles after saving.");
    }

    private static void BuildGlobalConfigFile()
    {
        var defaultProfiles = new[]
                              {
                                  new LEProfile("Run in Japanese",
                                                Guid.NewGuid().ToString(),
                                                false,
                                                string.Empty,
                                                "ja-JP",
                                                "Tokyo Standard Time",
                                                false,
                                                true,
                                                false,
                                                false
                                      ),
                                  new LEProfile("Run in Japanese (Admin)",
                                                Guid.NewGuid().ToString(),
                                                false,
                                                string.Empty,
                                                "ja-JP",
                                                "Tokyo Standard Time",
                                                true,
                                                true,
                                                false,
                                                false
                                      )
                              };

        WriteConfig(GlobalConfigPath, defaultProfiles);
    }

    /// <summary>
    ///     Reads the profile's region. Old LE versions (upstream, and this fork before
    ///     f29f84f) wrote it as &lt;Location&gt;; &lt;Region&gt; wins when both exist.
    ///     Saving always writes &lt;Region&gt;, so re-saving a profile migrates it.
    ///     Keep in sync with ShellExtension's ConfigReader.
    /// </summary>
    private static string GetRegion(XElement profile)
    {
        return (profile.Element("Region") ?? profile.Element("Location")).Value;
    }

    private static string TryGetValue(this XElement element, string name, string defaultValue = null)
    {
        var found = element.Element(name);
        if (found != null)
        {
            return found.Value;
        }
        return defaultValue;
    }

    private static void WriteConfig(string writeTo, params LEProfile[] profiles)
    {
        try
        {
            BuildConfig(profiles).Save(writeTo);
        }
        catch
        {
            // Upstream behavior for saving from the editors; imports use BuildConfig directly
            // so that a failed write is reported.
        }
    }

    private static XElement BuildConfig(LEProfile[] profiles)
    {
        var baseNode = new XElement("Profiles");

        foreach (var pro in profiles)
        {
            baseNode.Add(new XElement("Profile",
                                      new XAttribute("Name", pro.Name),
                                      new XAttribute("Guid", pro.Guid),
                                      new XAttribute("MainMenu", pro.ShowInMainMenu),
                                      new XElement("Parameter", pro.Parameter),
                                      new XElement("Region", pro.Region),
                                      new XElement("Timezone", pro.Timezone),
                                      new XElement("RunAsAdmin", pro.RunAsAdmin),
                                      new XElement("RedirectRegistry", pro.RedirectRegistry),
                                      new XElement("IsAdvancedRedirection", pro.IsAdvancedRedirection),
                                      new XElement("RunWithSuspend", pro.RunWithSuspend)
                             )
                );
        }

        return new XElement("LEConfig", baseNode);
    }
}
