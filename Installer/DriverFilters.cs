using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Microsoft.Win32;

namespace HidHide.Installer;

public static class DriverFilters
{
    public static readonly Guid[] Classes = {
        new("745A17A0-74D3-11D0-B6FE-00A0C90F57DA"),
        new("D61CA365-5AF4-4486-998B-9DB4734C6CA3"),
        new("05F5CFE2-4733-4950-A6BB-07AAD01A3A84")
    };

    public static string[] Change(IEnumerable<string> existing, bool attach)
    {
        var original = existing.ToArray();
        if (original.Length > 4096 || original.Any(x => string.IsNullOrEmpty(x) || x.IndexOf('\0') >= 0))
            throw new InvalidDataException("Invalid class filter list; preserve it for explicit recovery.");
        if (attach && original.Any(IsHidHide)) return original;
        return attach ? original.Concat(new[] { "HidHide" }).ToArray() : original.Where(x => !IsHidHide(x)).ToArray();
    }

    public static bool IsHidHide(string value) => string.Equals(value, "HidHide", StringComparison.OrdinalIgnoreCase);

    public static string[] DecodeRegistryEntries(string[] entries)
    {
        // Windows/.NET can expose an empty REG_MULTI_SZ as one empty string
        // after upstream uninstall. This is an empty list, not a filter name.
        if (entries.Length == 1 && entries[0] == "") return Array.Empty<string>();
        Change(entries, false);
        return entries;
    }

    public static string[] Read(Guid deviceClass)
    {
        if (!Classes.Contains(deviceClass)) throw new ArgumentException("Class is outside the HidHide contract.");
        using var machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64);
        using var key = machine.OpenSubKey(@"SYSTEM\CurrentControlSet\Control\Class\" + deviceClass.ToString("B"));
        if (key == null) return Array.Empty<string>();
        var value = key.GetValue("UpperFilters", null, RegistryValueOptions.DoNotExpandEnvironmentNames);
        if (value == null) return Array.Empty<string>();
        if (key.GetValueKind("UpperFilters") != RegistryValueKind.MultiString || value is not string[] entries)
            throw new InvalidDataException("Unexpected UpperFilters value type on " + deviceClass);
        // Validate without normalizing another vendor's data.
        return DecodeRegistryEntries(entries);
    }
}
