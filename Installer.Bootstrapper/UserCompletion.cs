using System.Diagnostics;
using Microsoft.Win32;

namespace HidHide.Bootstrapper;
internal static class UserCompletion
{
    // Runs only in the original ordinary-user BA, after all marker/barrier handles
    // are released. Existing absence means disabled; unknown commands are preserved.
    public static void Apply(bool uninstall)
    {
        using var key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Run", true);
        const string name = "HidHide Profiles", legacyName = "HidHide App Profiles";
        if (key == null) return;
        string current = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "HidHide", "HidHideClient.exe");
        string legacy = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "HidHide App Profiles", "HidHideClient.exe");
        string Command(string path) => "\"" + path + "\" --background";
        bool Owned(object? value) => value is string command &&
            (string.Equals(command, Command(current), StringComparison.OrdinalIgnoreCase) ||
             string.Equals(command, Command(legacy), StringComparison.OrdinalIgnoreCase));
        bool currentOwned = Owned(key.GetValue(name)), legacyOwned = Owned(key.GetValue(legacyName));
        if (!currentOwned && !legacyOwned) return;
        if (uninstall)
        {
            if (currentOwned) key.DeleteValue(name, false);
            if (legacyOwned) key.DeleteValue(legacyName, false);
            return;
        }
        if (!File.Exists(current)) throw new FileNotFoundException("Verified installed manager disappeared.");
        if (key.GetValue(name) != null && !currentOwned)
            throw new InvalidOperationException("The HidHide Profiles startup entry is owned by another command and was preserved.");
        key.SetValue(name, Command(current), RegistryValueKind.String);
        if (legacyOwned) key.DeleteValue(legacyName, false);
        using var process = Process.Start(new ProcessStartInfo(current, "--background") { UseShellExecute = false, CreateNoWindow = true });
    }
}
