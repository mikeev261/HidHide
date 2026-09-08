using HidHide.Setup;
using System.Reflection;
using System.Runtime.InteropServices;

internal static class LegacyFileEvidenceTests
{
    public static void Run()
    {
        const string id = "b7e9d4a2-6f31-4e88-9c0d-1a2b4c4d5e70/";
        // Values independently read from the inspected companion-1 cabinet.
        var source = new Dictionary<string, string> {
            [id + "HidHideCLI.exe"] = "3233FDC8C6756847564EABC6948F37C0EA82861630E5E18D9E8795BC5BD4F616",
            [id + "HidHideClient.exe"] = "0890CCCB254D66715151DCCCE4BBB4174B0E96AA27AE4996361782EAE805AF94",
            [id + "shortcut/programs"] = "canonical"
        };
        LegacyFileEvidence.Validate(source);
        void Reject(Dictionary<string, string> evidence)
        {
            try { LegacyFileEvidence.Validate(evidence); }
            catch (InvalidDataException) { return; }
            throw new Exception("Unsafe legacy evidence was accepted.");
        }
        var modified = new Dictionary<string, string>(source); modified[id + "HidHideCLI.exe"] = new string('0',64); Reject(modified);
        var incomplete = new Dictionary<string, string>(source); incomplete.Remove(id + "HidHideClient.exe"); Reject(incomplete);
        var traversal = new Dictionary<string, string>(source); traversal[id + "../other.exe"] = new string('0',64); Reject(traversal);
        var command = new Dictionary<string, string>(source); command[id + "shortcut/programs"] = "cmd.exe"; Reject(command);
        Reject(new Dictionary<string, string> { [Guid.Empty.ToString("D") + "/HidHideCLI.exe"] = new string('0',64) });
        source[id + "shortcut/programs"] = "absent"; LegacyFileEvidence.Validate(source);
        bool ownerRejected = false;
        try { LegacyFileEvidence.VerifySurvivingFiles(Array.Empty<HidHide.Installer.InstalledProduct>(), source); }
        catch (InvalidDataException) { ownerRejected = true; }
        if (!ownerRejected) throw new Exception("A missing original product owner was accepted.");
        VerifyRealShortcutReader();
        Console.WriteLine("10 legacy file evidence checks passed; actual legacy restore remains a VM gate.");
    }
    static void VerifyRealShortcutReader()
    {
        string directory = Path.Combine(Path.GetTempPath(), "HidHide-legacy-link-test-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        string path = Path.Combine(directory, "test.lnk"), target = Path.Combine(directory, "HidHideClient.exe");
        var type = Type.GetTypeFromProgID("WScript.Shell")!;
        object shell = Activator.CreateInstance(type)!; object? shortcut = null;
        try
        {
            shortcut = type.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { path })!;
            void Set(string key, string value) => shortcut.GetType().InvokeMember(key, BindingFlags.SetProperty, null, shortcut, new object[] { value });
            Set("TargetPath", target); Set("WorkingDirectory", directory); Set("Arguments", "");
            shortcut.GetType().InvokeMember("Save", BindingFlags.InvokeMethod, null, shortcut, null);
            var reader = typeof(LegacyFileEvidence).GetMethod("InspectShortcutAt", BindingFlags.NonPublic | BindingFlags.Static)!;
            if ((string)reader.Invoke(null, new object[] { path, target })! != "canonical") throw new Exception("Canonical shortcut was not recognized.");
            Set("Arguments", "--unexpected"); shortcut.GetType().InvokeMember("Save", BindingFlags.InvokeMethod, null, shortcut, null);
            try { reader.Invoke(null, new object[] { path, target }); }
            catch (TargetInvocationException error) when (error.InnerException is InvalidOperationException) { return; }
            throw new Exception("Customized shortcut was not rejected.");
        }
        finally
        {
            if (shortcut != null) Marshal.FinalReleaseComObject(shortcut); Marshal.FinalReleaseComObject(shell);
            if (File.Exists(path)) File.Delete(path);
            Directory.Delete(directory);
        }
    }
}
