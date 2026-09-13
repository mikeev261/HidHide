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
        const string name = "HidHide App Profiles";
        if (key == null || key.GetValue(name) is not string existing) return;
        string current = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "HidHide", "HidHideClient.exe");
        string legacy = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "HidHide App Profiles", "HidHideClient.exe");
        string Command(string path) => "\"" + path + "\" --background";
        if (!string.Equals(existing, Command(current), StringComparison.OrdinalIgnoreCase) && !string.Equals(existing, Command(legacy), StringComparison.OrdinalIgnoreCase)) return;
        if (uninstall) { key.DeleteValue(name, false); return; }
        if (!File.Exists(current)) throw new FileNotFoundException("Verified installed manager disappeared.");
        key.SetValue(name, Command(current), RegistryValueKind.String);
        using var process = Process.Start(new ProcessStartInfo(current, "--background") { UseShellExecute = false, CreateNoWindow = true });
    }
}
