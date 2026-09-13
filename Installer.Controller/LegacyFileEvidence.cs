using HidHide.DriverSetup;
using HidHide.Installer;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace HidHide.Setup;

// Fixed ownership maps inspected from the exact supported recovery packages.
// Journal keys never become paths; paths are resolved only from this map.
public static class LegacyFileEvidence
{
    sealed class OwnedFile
    {
        public string Relative = "", Hash = "", Component = "", KeyPath = "";
    }
    static readonly Guid Companion1 = new("B7E9D4A2-6F31-4E88-9C0D-1A2B4C4D5E70");
    static readonly Guid Companion99 = new("B7E9D4A2-6F31-4E88-9C0D-1A2B6C4D5E70");
    static OwnedFile F(string path, string hash, string component, string? key = null) => new() { Relative = path, Hash = hash, Component = component, KeyPath = key ?? path };
    static readonly OwnedFile[] Upstream =
    {
        F("HidHide.man", "B31B1A14CC5812492556B0CE84A2872C76D93AB0DD0778DFFA60FFB04A86B8D7", "F36C665F-23E4-45B4-B7C0-2589FB4266E5"),
        F("x64/HidHide.pdb", "456760F76B3C5B0EC71007B33529F77E0251FAE03A87335359DAC61B1F99688D", "5F3D6B3B-C035-4D48-AC00-756AE510FEF1"),
        F("x64/HidHideCLI.exe", "9DD283FEDFBD301E1A574A3D4B8663F6274CCB5C896F468ED55A6239F9ADE270", "FEEC20CC-7370-4347-9A41-8DFD19FAC53D"),
        F("x64/HidHideClient.exe", "4AF4752F0564E9C2A2B139B05DA4BBFCC4AB111A03033319EB38B7EC9B070A45", "C3EC16D6-35D4-4479-812E-9CEA0346E2D8"),
        F("x64/HidHideWatchdog.exe", "8B41058F5BBCDF85AF44489402B808F4E3A2B44B81AFC1C89169AC29501AAB93", "42C955B2-C626-49ED-B24F-8AB91AB09D83"),
        F("x64/install.cmd", "E67286E3A77FEF32F0B9500A68EF06737E40352167B74D8BF3E876CC70F84F48", "9D670EEF-F372-47BF-96BD-D1C5960200A4"),
        F("x64/uninstall.cmd", "41C117393B0C82625A4B05A7AE647E5BBCC350C791B2849FC25BF0BACD6F2D3B", "9D670EEF-F372-47BF-96BD-D1C5960200A4", "x64/install.cmd"),
        F("x64/nefarius_HidHide_Updater.exe", "128CEB8C6B5E9F32261B55BA904234ED52E26D03C9B92422CD2CF8B101BC1A88", "8723E8D2-15C4-4B5E-B2D6-3BBBB76B6A00"),
        F("x64/nefconw.exe", "1482FA240CAD984E02206427F1EB211E62C9A44B058484FC3E83CCB5B1A1FBCA", "F306D60E-E3CF-4C2D-B69C-F0AED9032A08"),
        F("x64/HidHide/HidHide.inf", Payload.InfHash, "35CB06F9-C51D-4996-BBC9-4F000A66462D", "x64/HidHide/hidhide.cat"),
        F("x64/HidHide/HidHide.sys", "E3BB8B6A4E4FF90290B34E710EDBEF88CF71CEC6BA0CADCB8E1685D5DD65DDB2", "AA6173B6-0815-4B0E-BBB1-CA1299A51ACA"),
        F("x64/HidHide/hidhide.cat", "0E4209963524163B3B75302C60F7B8A965FC349ADA5752B74452AD59D417E877", "35CB06F9-C51D-4996-BBC9-4F000A66462D"),
        F("x64/HidHide/LICENSE.rtf", "62356AD799305776DDD2FDF7B178EC9755E17ED980DA560E3887B883465825E0", "140B90CF-C350-460A-939A-7DA0B8A8377C")
    };
    static OwnedFile[] Files(Guid product)
    {
        if (product == ProductContract.UpstreamProductCode) return Upstream;
        if (product != Companion1 && product != Companion99) throw new InvalidDataException("Unsupported legacy file owner.");
        return new[] {
            F("HidHideClient.exe", product == Companion1 ? "0890CCCB254D66715151DCCCE4BBB4174B0E96AA27AE4996361782EAE805AF94" : "14EF5D40E494E45F1E88114114B5E75067A789F6877589A77CE524B1989ECFD6", "7078E839-3A07-4FA9-BC3A-767722617B28"),
            F("HidHideCLI.exe", product == Companion1 ? "3233FDC8C6756847564EABC6948F37C0EA82861630E5E18D9E8795BC5BD4F616" : "104EEF4CAA478C296379458E2DAC4347C8D930AA959A7DFFFA2151F4C8E2E89E", "7078E839-3A07-4FA9-BC3A-7677D8FAF02F")
        };
    }
    static string Root(Guid product) => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), product == ProductContract.UpstreamProductCode ? @"Nefarius Software Solutions\HidHide" : "HidHide App Profiles");
    static string Resolve(Guid product, string relative) => Path.Combine(Root(product), relative.Replace('/', Path.DirectorySeparatorChar));
    static string Key(Guid product, string name) => product.ToString("D") + "/" + name;
    static string[] Shortcuts(Guid product) => product == ProductContract.UpstreamProductCode ? new[] { "desktop", "start-menu" } : new[] { "programs" };
    static string ShortcutPath(Guid product, string name) => product == ProductContract.UpstreamProductCode
        ? Path.Combine(Environment.GetFolderPath(name == "desktop" ? Environment.SpecialFolder.CommonDesktopDirectory : Environment.SpecialFolder.CommonStartMenu), "HidHide Configuration Client.lnk")
        : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonPrograms), "HidHide App Profiles", "HidHide App Profiles.lnk");
    static void RequireCanonicalComponent(Guid product, OwnedFile file)
    {
        var path = new StringBuilder(1024); int length = path.Capacity;
        int state = MsiGetComponentPathExW(product.ToString("B"), new Guid(file.Component).ToString("B"), null, 4, path, ref length);
        if (state != 3 || !string.Equals(path.ToString(), Resolve(product, file.KeyPath), StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Legacy component is missing or uses an unsupported installation location; preserve it for explicit compatibility review.");
    }
    static string Hash(string path)
    {
        ProtectedJournal.RejectReparsePath(path);
        using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (file.Length > 256 * 1024 * 1024) throw new InvalidDataException("Oversized legacy file.");
        using var hash = SHA256.Create(); return BitConverter.ToString(hash.ComputeHash(file)).Replace("-", "");
    }
    static string InspectShortcut(Guid product, string name)
    {
        string target = Resolve(product, product == ProductContract.UpstreamProductCode ? "x64/HidHideClient.exe" : "HidHideClient.exe");
        return InspectShortcutAt(ShortcutPath(product, name), target);
    }
    static string InspectShortcutAt(string path, string target)
    {
        // Check existing parents even when the shortcut was not installed.
        string parent = path;
        while (!File.Exists(parent) && !Directory.Exists(parent)) parent = Path.GetDirectoryName(parent) ?? throw new InvalidDataException("Invalid shortcut path.");
        ProtectedJournal.RejectReparsePath(parent);
        if (!File.Exists(path)) return "absent";
        if (new FileInfo(path).Length > 1024 * 1024) throw new InvalidDataException("Oversized legacy shortcut.");
        var type = Type.GetTypeFromProgID("WScript.Shell") ?? throw new InvalidOperationException("Shortcut inspection is unavailable.");
        object shell = Activator.CreateInstance(type)!; object? shortcut = null;
        try
        {
            shortcut = type.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { path })!;
            string Read(string property) => (string)shortcut.GetType().InvokeMember(property, BindingFlags.GetProperty, null, shortcut, null)!;
            if (!string.Equals(Read("TargetPath"), target, StringComparison.OrdinalIgnoreCase) || Read("Arguments") != "" ||
                !string.Equals(Read("WorkingDirectory").TrimEnd('\\'), Path.GetDirectoryName(target), StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Legacy shortcut was customized; preserve it for explicit compatibility review.");
            return "canonical";
        }
        finally { if (shortcut != null) Marshal.FinalReleaseComObject(shortcut); Marshal.FinalReleaseComObject(shell); }
    }
    public static Dictionary<string, string> Capture(IReadOnlyList<InstalledProduct> products)
    {
        var result = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var product in products)
        {
            if (ProductContract.Select(product.Family, product.Product, product.Version, new Version(2, 1, 0, 0)) is not (ProductContract.Operation.MigrateUpstream or ProductContract.Operation.MigrateCompanion))
                throw new InvalidDataException("Unsupported legacy file inventory.");
            foreach (var file in Files(product.Product))
            {
                RequireCanonicalComponent(product.Product, file);
                string hash = Hash(Resolve(product.Product, file.Relative));
                if (hash != file.Hash) throw new InvalidOperationException("Legacy files differ from the verified recovery source; preserve the installation.");
                result.Add(Key(product.Product, file.Relative), hash);
            }
            foreach (string name in Shortcuts(product.Product)) result.Add(Key(product.Product, "shortcut/" + name), InspectShortcut(product.Product, name));
        }
        Validate(result); return result;
    }
    public static void Validate(Dictionary<string, string> evidence)
    {
        if (evidence == null || evidence.Count > 18) throw new InvalidDataException("Invalid legacy file evidence.");
        var products = new HashSet<Guid>();
        foreach (var key in evidence.Keys)
        {
            int separator = key.IndexOf('/');
            if (separator != 36 || !Guid.TryParseExact(key.Substring(0, separator), "D", out var product)) throw new InvalidDataException("Invalid legacy evidence key.");
            _ = Files(product); products.Add(product);
        }
        if (products.Contains(Companion1) && products.Contains(Companion99)) throw new InvalidDataException("Ambiguous legacy companion evidence.");
        int count = 0;
        foreach (var product in products)
        {
            foreach (var file in Files(product)) { count++; if (!evidence.TryGetValue(Key(product, file.Relative), out string hash) || hash != file.Hash) throw new InvalidDataException("Incomplete or untrusted legacy file evidence."); }
            foreach (var name in Shortcuts(product)) { count++; if (!evidence.TryGetValue(Key(product, "shortcut/" + name), out string value) || value != "canonical" && value != "absent") throw new InvalidDataException("Invalid legacy shortcut evidence."); }
        }
        if (count != evidence.Count) throw new InvalidDataException("Unexpected legacy evidence key.");
    }
    public static void VerifyRestored(IReadOnlyList<InstalledProduct> products, Dictionary<string, string> evidence)
    {
        Validate(evidence);
        var actual = Capture(products);
        if (actual.Count != evidence.Count || evidence.Any(x => !actual.TryGetValue(x.Key, out string value) || value != x.Value))
            throw new InvalidOperationException("Legacy files or shortcut preferences were not restored.");
    }
    public static void RestoreShortcutPreferences(IReadOnlyList<InstalledProduct> products, Dictionary<string, string> evidence)
    {
        // Caller retains the protected recovery lease. MSI may regenerate an
        // optional shortcut that was absent before migration. Remove only that
        // fixed mapped link, after validating all restored component/file bytes
        // and the link's canonical target. Never delete a customized shortcut.
        Validate(evidence);
        var actual = Capture(products);
        if (!new HashSet<string>(actual.Keys, StringComparer.Ordinal).SetEquals(evidence.Keys))
            throw new InvalidOperationException("Legacy shortcut ownership changed.");
        foreach (var product in products)
            foreach (string name in Shortcuts(product.Product))
                if (evidence[Key(product.Product, "shortcut/" + name)] == "absent" && actual[Key(product.Product, "shortcut/" + name)] == "canonical")
                {
                    string path = ShortcutPath(product.Product, name);
                    ProtectedJournal.RejectReparsePath(path);
                    File.Delete(path);
                }
        VerifyRestored(products, evidence);
    }
    public static void VerifySurvivingFiles(IReadOnlyList<InstalledProduct> originalProducts, Dictionary<string, string> evidence)
    {
        Validate(evidence);
        var keys = new HashSet<string>(StringComparer.Ordinal);
        foreach (var product in originalProducts)
        {
            if (ProductContract.Select(product.Family, product.Product, product.Version, new Version(2, 1, 0, 0)) is not (ProductContract.Operation.MigrateUpstream or ProductContract.Operation.MigrateCompanion))
                throw new InvalidDataException("Unsupported original legacy owner.");
            foreach (var file in Files(product.Product))
            {
                keys.Add(Key(product.Product, file.Relative));
                var componentPath = new StringBuilder(1024); int length = componentPath.Capacity;
                int state = MsiGetComponentPathExW(product.Product.ToString("B"), new Guid(file.Component).ToString("B"), null, 4, componentPath, ref length);
                if (state == 3 ? !string.Equals(componentPath.ToString(), Resolve(product.Product, file.KeyPath), StringComparison.OrdinalIgnoreCase) : state != 2 && state != -1)
                    throw new InvalidOperationException("Surviving legacy component has unexpected ownership or location.");
                string path = Resolve(product.Product, file.Relative), parent = path;
                while (!File.Exists(parent) && !Directory.Exists(parent)) parent = Path.GetDirectoryName(parent) ?? throw new InvalidDataException("Invalid legacy file path.");
                ProtectedJournal.RejectReparsePath(parent);
                if (Directory.Exists(path) || File.Exists(path) && Hash(path) != file.Hash)
                    throw new InvalidOperationException("Surviving legacy file differs from the verified recovery source; do not overwrite it.");
            }
            foreach (string name in Shortcuts(product.Product))
            {
                keys.Add(Key(product.Product, "shortcut/" + name));
                _ = InspectShortcut(product.Product, name);
            }
        }
        if (!keys.SetEquals(evidence.Keys)) throw new InvalidDataException("Original legacy ownership does not match its file evidence.");
    }
    [DllImport("msi.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] static extern int MsiGetComponentPathExW(string product, string component, string? sid, uint context, StringBuilder path, ref int length);
}
