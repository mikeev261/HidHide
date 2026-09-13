using HidHide.DriverSetup;
using HidHide.Installer;
using Microsoft.Win32;
using System.Security.Cryptography;

namespace HidHide.Setup;

public static class MsiRollbackRecovery
{
    static readonly string[] Files = { "HidHideCLI.exe", "HidHideClient.exe", "mfc140u.dll", "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll", "Driver/HidHide.inf", "Driver/HidHide.sys", "Driver/hidhide.cat", "Driver/LICENSE.rtf", "shortcut" };
    public static void ValidateFiles(Dictionary<string, string>? files)
    {
        if (files == null || files.Count != Files.Length || Files.Any(x => !files.ContainsKey(x)) || files.Values.Any(x => x != "absent" && !ValidHash(x)))
            throw new InvalidDataException("Invalid pre-MSI file evidence.");
    }
    static bool ValidHash(string value) => value != null && System.Text.RegularExpressions.Regex.IsMatch(value, "\\A[A-F0-9]{64}\\z");
    static string HashFile(string path)
    {
        ProtectedJournal.RejectReparsePath(path);
        using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (file.Length > 256 * 1024 * 1024) throw new InvalidDataException("Oversized installer recovery evidence.");
        using var hash = SHA256.Create();
        return BitConverter.ToString(hash.ComputeHash(file)).Replace("-", "");
    }
    public static Dictionary<string, string> CaptureFiles()
    {
        var files = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (string name in Files)
        {
            string path = name == "shortcut" ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonPrograms), "HidHide", ProductContract.Name + ".lnk") :
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "HidHide", name.Replace('/', Path.DirectorySeparatorChar));
            // Validate every existing parent even when the target is absent.
            string parent = path; while (!File.Exists(parent) && !Directory.Exists(parent)) parent = Path.GetDirectoryName(parent) ?? throw new InvalidDataException("Invalid owned file path.");
            ProtectedJournal.RejectReparsePath(parent);
            files.Add(name, File.Exists(path) ? HashFile(path) : "absent");
        }
        return files;
    }
    public static Dictionary<string, string> CaptureBundles()
    {
        var result = new Dictionary<string, string>(StringComparer.Ordinal);
        string cacheRoot = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "Package Cache") + Path.DirectorySeparatorChar;
        foreach (var view in new[] { RegistryView.Registry32, RegistryView.Registry64 })
        {
            using var machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view);
            using var root = machine.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall");
            if (root == null) continue;
            var names = root.GetSubKeyNames(); if (names.Length > 16384) throw new InvalidDataException("Oversized installer inventory.");
            foreach (string name in names)
            {
                using var key = root.OpenSubKey(name) ?? throw new InvalidDataException("Installer inventory changed.");
                if (!(key.GetValue("BundleUpgradeCode") is string[] families) || !families.Any(x => Guid.TryParse(x, out var id) && id == ProductContract.BundleUpgradeCode)) continue;
                if (!Guid.TryParseExact(name, "B", out var bundle) || result.Count >= 8) throw new InvalidDataException("Ambiguous related bundle inventory.");
                string version = key.GetValue("BundleVersion") as string ?? throw new InvalidDataException("Related bundle version missing.");
                string path = key.GetValue("BundleCachePath") as string ?? throw new InvalidDataException("Related bundle cache missing.");
                if (!Path.IsPathRooted(path) || !Path.GetFullPath(path).StartsWith(cacheRoot, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Unexpected bundle cache location.");
                result.Add(view + ":" + bundle.ToString("D"), version + ":" + HashFile(path));
            }
        }
        return result;
    }
    public static bool Same(Dictionary<string, string> expected, Dictionary<string, string> actual) => expected.Count == actual.Count && expected.All(x => actual.TryGetValue(x.Key, out var value) && value == x.Value);
    public static void VerifyNative(SetupRecord setup, TransactionRecord driver, DriverState actual)
    {
        VerifyIdentity(setup, driver);
        bool untouched = driver.Status == JournalStatus.Prepared && driver.Steps.Count == 0 && driver.Failure == "";
        bool rolledBack = driver.Status == JournalStatus.RolledBack && driver.Steps.All(x => x.Completed && x.Undone);
        if (driver.Reboot || !(untouched || rolledBack) ||
            !ProtectedJournal.StateBytes(actual).SequenceEqual(ProtectedJournal.StateBytes(driver.Before)))
            throw new InvalidOperationException("Native rollback is not proven complete; recovery evidence retained.");
    }
    public static void VerifyIdentity(SetupRecord setup, TransactionRecord driver)
    {
        ProtectedJournal.Validate(driver);
        var operation = setup.Operation == Operation.Uninstall ? Operation.Uninstall : driver.Before.CanInstall ? Operation.Install : setup.Operation == Operation.Upgrade ? Operation.Upgrade : Operation.Repair;
        if (driver.Id != setup.Id || driver.InitiatingSid != setup.Sid || driver.Operation != operation)
            throw new InvalidOperationException("Native rollback identity does not match setup.");
    }
}
