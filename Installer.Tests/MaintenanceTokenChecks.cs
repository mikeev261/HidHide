using HidHide.Installer;
using System.Diagnostics;
using System.Security.Principal;

internal static class MaintenanceTokenChecks
{
    public static void Run(int clientProcessId = 0)
    {
        using var identity = WindowsIdentity.GetCurrent();
        string sid = identity.User!.Value;
        bool rejected = false;
        try { using var wrong = new MaintenanceUser("S-1-5-18"); }
        catch (UnauthorizedAccessException) { rejected = true; }
        if (!rejected) throw new Exception("Different user token was accepted.");
        rejected = false;
        try { MaintenanceUser.Validate(identity.AccessToken, sid, 0); }
        catch (UnauthorizedAccessException) { rejected = true; }
        if (!rejected) throw new Exception("Session zero was accepted.");
        rejected = false;
        try { MaintenanceUser.Validate(identity.AccessToken, "S-1-5-18", Process.GetCurrentProcess().SessionId); }
        catch (UnauthorizedAccessException) { rejected = true; }
        if (!rejected) throw new Exception("Mismatched helper SID was accepted.");
        rejected = false;
        try { MaintenanceUser.Validate(identity.AccessToken, sid, Process.GetCurrentProcess().SessionId + 1); }
        catch (UnauthorizedAccessException) { rejected = true; }
        if (!rejected) throw new Exception("Mismatched helper session was accepted.");

        string evidence = Path.Combine(Path.GetTempPath(), "HidHide-token-test-" + Guid.NewGuid().ToString("N") + ".txt");
        string? previous = Environment.GetEnvironmentVariable("HIDHIDE_TEST_PARENT_ONLY");
        Environment.SetEnvironmentVariable("HIDHIDE_TEST_PARENT_ONLY", "must-not-leak");
        try
        {
            using var user = new MaintenanceUser(sid, clientProcessId);
            if (user.Sid != sid || user.SessionId != Process.GetCurrentProcess().SessionId) throw new Exception("Selected token identity/session changed.");
            using var child = user.Start(Path.Combine(AppContext.BaseDirectory, "Installer.Tests.exe"),
                $"--maintenance-user-child {sid} {user.SessionId} \"{evidence}\"");
            if (!child.WaitForExit(15000) || child.ExitCode != 0) throw new Exception("Ordinary child token probe failed.");
            string expected = "True|True|True|" + Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            if (File.ReadAllText(evidence) != expected) throw new Exception("Child identity, environment or profile directory differs.");
            Console.WriteLine("7 maintenance token checks passed (parent elevated=" + (MaintenanceUser.TokenNumber(identity.AccessToken, 20) != 0) + ").");
        }
        finally
        {
            Environment.SetEnvironmentVariable("HIDHIDE_TEST_PARENT_ONLY", previous);
            if (File.Exists(evidence)) File.Delete(evidence);
        }
    }
}
