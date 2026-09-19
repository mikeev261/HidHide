using System.Diagnostics;
using System.Security.Principal;
using HidHide.Installer;
using WixSharp;
using WixToolset.Dtf.WindowsInstaller;

internal static class Program
{
    static int Main(string[] args)
    {
        if (args.Length == 3 && args[0] == "child")
        {
            using var identity = WindowsIdentity.GetCurrent();
            return identity.User?.Value == args[1] && Process.GetCurrentProcess().SessionId.ToString() == args[2]
                && MaintenanceUser.TokenNumber(identity.AccessToken, 20) == 0 ? 0 : 1;
        }
        if (args.Length == 2 && args[0] == "failure")
        {
            bool reported = false;
            Installer.SetInternalUI(InstallUIOptions.Silent);
            Installer.SetExternalUI((type, message, buttons, icon, defaultButton) =>
            {
                if (type == InstallMessage.Error && message.Contains("Regression probe maintenance failure")) reported = true;
                return MessageResult.OK;
            }, InstallLogModes.Error);
            try { Installer.InstallProduct(args[1], "FAILPROBE=1 REBOOT=ReallySuppress"); }
            catch (InstallerException error) when (error.ErrorCode == 1603)
            { Console.WriteLine("Visible MSI error callback: " + reported); return reported ? 0 : 1; }
            return 1;
        }
        if (args.Length != 1 || args[0] != "build") return 2;
        string output = Path.GetFullPath("artifacts/installer-fix-validation/msi-probe");
        Directory.CreateDirectory(output);
        string fixture = Path.Combine(output, "fixture.txt");
        System.IO.File.WriteAllText(fixture, "Disposable driver-free Windows Installer regression fixture.");
        var prepare = new ManagedAction(new Id("ProbeUser"), ProbeActions.CheckUser, Return.check,
            When.Before, Step.InstallInitialize, new Condition("NOT AFTERREBOOT AND NOT SKIPPROBE")) { Impersonate = true };
        var operation = new ManagedAction(new Id("ProbeOperation"), ProbeActions.Operation, Return.check,
            When.Before, Step.CostFinalize, new Condition("NOT SKIPPROBE"));
        var elevated = new ElevatedManagedAction(new Id("ProbeElevated"), ProbeActions.Elevated, Return.check,
            When.Before, Step.ForceReboot, new Condition("NOT SKIPPROBE"));
        var verify = new ElevatedManagedAction(new Id("VerifyResume"), ProbeActions.VerifyResume, Return.check,
            When.After, Step.ForceReboot, new Condition("HIDHIDE_RECOVERY AND NOT SKIPPROBE"));
        foreach (var action in new ManagedAction[] { prepare, operation, verify, elevated })
        {
            action.RefAssemblies = new[] { typeof(DirectMsiActions).Assembly.Location, typeof(HidHide.DriverSetup.WindowsDriverBackend).Assembly.Location };
            action.UsesProperties = "HIDHIDE_UNINSTALL,HIDHIDE_RECOVERY";
        }
        var project = new ManagedProject("HidHide MSI Regression Probe",
            new Dir(@"%ProgramFiles64Folder%\HidHide MSI Regression Probe", new WixSharp.File(fixture)))
        {
            GUID = new Guid("943247B8-13A9-4A1C-AF2B-0367BDCF9913"),
            ProductId = new Guid("27E26E54-51C8-407E-B931-1208622128AC"),
            Version = new Version(1,0,0), Platform = Platform.x64, Scope = InstallScope.perMachine,
            OutDir = output, OutFileName = "HidHide.MsiProbe", UI = WUI.WixUI_Minimal,
            Actions = new WixSharp.Action[] { operation, prepare, elevated, verify },
            Properties = new[] { new Property("ARPSYSTEMCOMPONENT", "1"), new Property("MSIFASTINSTALL", "7"), new Property("SKIPPROBE", "") { Secure = true },
                new Property("HIDHIDE_UNINSTALL", "") { Secure = true, IsDeferred = true }, new Property("HIDHIDE_RECOVERY", "") { Secure = true, IsDeferred = true },
                new Property("FAILPROBE", "") { Secure = true } },
            ForceReboot = new ForceReboot { Step = Step.InstallFiles, When = When.After, Condition = new Condition("HIDHIDE_UNINSTALL AND NOT HIDHIDE_RECOVERY AND NOT AFTERREBOOT AND NOT SKIPPROBE") }
        };
        project.BuildMsi();
        return 0;
    }
}

public static class ProbeActions
{
    const string Marker = @"SOFTWARE\HidHideMsiRegressionProbe";
    [CustomAction] public static ActionResult Operation(Session session)
    {
        using var key = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(Marker);
        bool pending = key != null;
        var plan = DirectMsiPolicy.RemovalPlan(session["REMOVE"] == "ALL", pending);
        session["HIDHIDE_UNINSTALL"] = plan.Uninstall ? "1" : "";
        session["HIDHIDE_RECOVERY"] = pending ? "1" : "";
        if (plan.Uninstall) { session["REMOVE"] = plan.RemoveApplications ? "ALL" : ""; session["REINSTALL"] = ""; session["ADDLOCAL"] = ""; }
        session.Log("REMOVAL_PLAN uninstall=" + plan.Uninstall + "; removeApps=" + plan.RemoveApplications);
        return ActionResult.Success;
    }
    [CustomAction] public static ActionResult Elevated(Session session)
    {
        if (session.CustomActionData["HIDHIDE_UNINSTALL"] == "1" && session.CustomActionData["HIDHIDE_RECOVERY"] != "1")
        { using var key = Microsoft.Win32.Registry.LocalMachine.CreateSubKey(Marker); key.SetValue("Pending", 1); }
        return ActionResult.Success;
    }
    [CustomAction] public static ActionResult CheckUser(Session session)
    {
        if (session["FAILPROBE"] == "1")
            return (ActionResult)typeof(DirectMsiActions).GetMethod("ReportFailure", System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.NonPublic)!
                .Invoke(null, new object[] { session, "prepare maintenance", new InvalidOperationException("Regression probe maintenance failure") });
        try
        {
            using var effective = WindowsIdentity.GetCurrent();
            session.Log("TOKEN_PROBE effective=" + effective.User?.Value + "; elevated=" + MaintenanceUser.TokenNumber(effective.AccessToken, 20) + "; session=" + MaintenanceUser.TokenNumber(effective.AccessToken, 12) + "; client=" + session["CLIENTPROCESSID"] + "; UserSID=" + session["UserSID"]);
            using var user = new MaintenanceUser(session["UserSID"], int.Parse(session["CLIENTPROCESSID"]));
            string executable = @"C:\Code\HidHide\tools\Installer.MsiProbe\bin\Release\net48\Installer.MsiProbe.exe";
            using var child = user.Start(executable, "child " + user.Sid + " " + user.SessionId);
            if (!child.WaitForExit(15000) || child.ExitCode != 0) throw new Exception("Ordinary child probe failed.");
            session.Log("TOKEN_PROBE passed: same ordinary user and session.");
            return ActionResult.Success;
        }
        catch (Exception error) { session.Log("TOKEN_PROBE FAILED: " + error); return ActionResult.Failure; }
    }
    [CustomAction] public static ActionResult VerifyResume(Session session)
    {
        bool exists = System.IO.File.Exists(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "HidHide MSI Regression Probe", "fixture.txt"));
        session.Log("REMOVAL_RESUME fileExists=" + exists);
        if (exists) return ActionResult.Failure;
        Microsoft.Win32.Registry.LocalMachine.DeleteSubKey(Marker);
        return ActionResult.Success;
    }
}
