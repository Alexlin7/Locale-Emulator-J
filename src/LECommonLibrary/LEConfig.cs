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
            var dict = XDocument.Load(configPath);

            var pros = from i in dict.Descendants("LEConfig").Elements("Profiles").Elements()
                       select i;

            var profiles =
                pros.Select(p => new LEProfile(p.Attribute("Name").Value,
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

            return profiles;
        }
        catch
        {
            return [];
        }
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

    /// <summary>Number of profiles imported and skipped (already existing).</summary>
    public readonly record struct ImportResult(int Imported, int Skipped);

    /// <summary>
    ///     Appends the incoming profiles to the existing ones. A profile is skipped when its Guid
    ///     (ignoring case) or Name is already present, so existing profiles always win and no
    ///     duplicate names appear in the context menu. Imported profiles keep their Guid, which
    ///     the context menu uses to find them (-runas {guid}); a missing Guid gets a new one.
    /// </summary>
    public static LEProfile[] MergeProfiles(LEProfile[] existing, LEProfile[] incoming, out ImportResult result)
    {
        var merged = existing.ToList();
        var guids = new HashSet<string>(existing.Select(p => p.Guid ?? string.Empty), StringComparer.OrdinalIgnoreCase);
        var names = new HashSet<string>(existing.Select(p => p.Name ?? string.Empty), StringComparer.Ordinal);
        int imported = 0, skipped = 0;

        foreach (var profile in incoming)
        {
            var p = profile;
            if (string.IsNullOrEmpty(p.Guid))
                p.Guid = Guid.NewGuid().ToString();

            if (guids.Contains(p.Guid) || names.Contains(p.Name ?? string.Empty))
            {
                skipped++;
                continue;
            }

            merged.Add(p);
            guids.Add(p.Guid);
            names.Add(p.Name ?? string.Empty);
            imported++;
        }

        result = new ImportResult(imported, skipped);
        return merged.ToArray();
    }

    /// <summary>
    ///     Imports the profiles of another config file (e.g. LEConfig.xml of the original LE 2.x)
    ///     into targetConfigPath with <see cref="MergeProfiles" />. The target is only rewritten
    ///     when something was imported. An unreadable source, or the target itself, imports nothing.
    /// </summary>
    public static ImportResult ImportProfiles(string sourceConfigPath, string targetConfigPath)
    {
        if (string.Equals(Path.GetFullPath(sourceConfigPath), Path.GetFullPath(targetConfigPath),
                          StringComparison.OrdinalIgnoreCase))
            return new ImportResult(0, 0);

        var incoming = GetProfiles(sourceConfigPath);
        if (incoming.Length == 0)
            return new ImportResult(0, 0);

        var merged = MergeProfiles(GetProfiles(targetConfigPath), incoming, out var result);
        if (result.Imported > 0)
            WriteConfig(targetConfigPath, merged);
        return result;
    }

    /// <summary>Imports into this installation's global LEConfig.xml.</summary>
    public static ImportResult ImportGlobalProfiles(string sourceConfigPath)
    {
        return ImportProfiles(sourceConfigPath, GlobalConfigPath);
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

        var tree = new XElement("LEConfig", baseNode);

        try
        {
            tree.Save(writeTo);
        }
        catch
        {
        }
    }
}
