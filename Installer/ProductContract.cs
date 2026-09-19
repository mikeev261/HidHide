using System;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Xml.Linq;

namespace HidHide.Installer;

// Shared by installer production code and its tests. Cross-family migration is
// deliberately separate from same-family version ordering.
public static class ProductContract
{
    public const string Name = "HidHide Profiles";
    public const string Publisher = "HidHide Profiles";
    public static readonly Guid BundleUpgradeCode = new("C62D8280-B0B1-42FD-8969-084CC64F9D5B");
    public static readonly Guid MsiUpgradeCode = new("A7F7B763-29B4-47FB-9B00-DB18AFA5EB32");
    public static readonly Guid UpstreamUpgradeCode = new("8822CC70-E2A5-4CB7-8F14-E27101150A1D");
    public static readonly Guid CompanionUpgradeCode = new("7078E839-3A07-4FA9-BC3A-7677356C88CF");
    public static readonly Guid UpstreamProductCode = new("01E0AB21-D1CC-42B4-9DFF-84FFE4F26DAF");
    public enum Operation { FreshInstall, Repair, Upgrade, MigrateUpstream, MigrateCompanion, RejectDowngrade, RejectUnknown }

    public static Guid UnifiedProductCode(Version version)
    {
        var canonical = ParseVersion($"{version.Major}.{version.Minor}.{version.Build}.0");
        using var hash = System.Security.Cryptography.SHA256.Create();
        var bytes = hash.ComputeHash(System.Text.Encoding.UTF8.GetBytes(MsiUpgradeCode.ToString("D") + ":" + canonical.ToString(4)));
        return new Guid(bytes.Take(16).ToArray());
    }

    public static Version ParseVersion(string text)
    {
        if (text is null || !Regex.IsMatch(text, @"\A(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.0\z") ||
            !Version.TryParse(text, out var version) || version.Major > 255 || version.Minor > 255 || version.Build > 65535)
            throw new ArgumentException("Product version must be major.minor.build.0 within MSI limits; no fallback is permitted.");
        return version;
    }

    public static Version ReadVersion(string path)
    {
        var values = XDocument.Load(path).Descendants("HidHideProductVersion").ToArray();
        if (values.Length != 1) throw new InvalidDataException("Expected one explicit HidHideProductVersion in ProductVersion.props.");
        return ParseVersion(values[0].Value);
    }

    public static Operation Select(Guid family, Guid product, Version installed, Version target)
    {
        if (family == MsiUpgradeCode)
        {
            if (installed.Revision > 0) return Operation.RejectUnknown;
            if (new Version(installed.Major, installed.Minor, installed.Build) >= new Version(2, 1, 0) && product != UnifiedProductCode(installed)) return Operation.RejectUnknown;
            var current = new Version(installed.Major, installed.Minor, installed.Build);
            var next = new Version(target.Major, target.Minor, target.Build);
            return current > next ? Operation.RejectDowngrade : current == next ? Operation.Repair : Operation.Upgrade;
        }
        if (family == UpstreamUpgradeCode)
            return product == UpstreamProductCode && (installed == new Version(1,5,230) || installed == new Version(1,5,230,0))
                ? Operation.MigrateUpstream : Operation.RejectUnknown;
        if (family == CompanionUpgradeCode)
        {
            // Only packages with inspected metadata are currently recognized.
            if (product == new Guid("B7E9D4A2-6F31-4E88-9C0D-1A2B6C4D5E70") && installed == new Version(99,0,0,0) ||
                product == new Guid("B7E9D4A2-6F31-4E88-9C0D-1A2B4C4D5E70") && installed == new Version(1,0,0,0))
                return Operation.MigrateCompanion;
        }
        return Operation.RejectUnknown;
    }
}
