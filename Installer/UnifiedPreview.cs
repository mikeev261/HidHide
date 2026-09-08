using HidHide.DriverSetup;
using WixSharp;

namespace HidHide.Installer;

// Development preview: Burn preparation and protected caching are implemented.
// Native lifecycle/reboot validation and related-bundle upgrade handling remain
// release gates. Missing protected controller authorization fails the MSI.
public static class UnifiedPreview
{
    public static void Configure(ManagedProject project, Dir installDir, string payload)
    {
        Payload.Verify(payload);
        var application = installDir;
        while (application.Dirs.Length == 1) application = application.Dirs[0];
        if (application.Name != "HidHide") throw new InvalidOperationException("Unexpected unified directory tree.");
        installDir.IsInstallDir = false;
        application.IsInstallDir = true;
        application.Files = application.Files.Concat(new[] { "mfc140u.dll", "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll" }
            .Select(name => new WixSharp.File(Path.Combine(Path.GetDirectoryName(application.Files[0].Name)!, name)))).ToArray();
        application.Dirs = new[] { new Dir("Driver", new[] { "HidHide.inf", "HidHide.sys", "hidhide.cat", "LICENSE.rtf" }
            .Select(name => new WixSharp.File(Path.Combine(payload, name))).ToArray()) };
        project.Name = ProductContract.Name;
        project.UpgradeCode = ProductContract.MsiUpgradeCode;
        // Remove the old applications inside the new MSI transaction so MSI
        // rollback restores them. Its UPGRADINGPRODUCTCODE removal condition
        // deliberately retains the unchanged signed driver.
        project.MajorUpgrade = new MajorUpgrade
        {
            Schedule = UpgradeSchedule.afterInstallInitialize,
            DowngradeErrorMessage = "A newer HidHide version is already installed.",
            AllowSameVersionUpgrades = false,
            IgnoreRemoveFailure = false
        };
        project.GUID = ProductContract.MsiUpgradeCode;
        project.ProductId = ProductContract.UnifiedProductCode(project.Version);
        project.OutFileName = "HidHide.Unified.Preview";
        project.Properties = new[] { new Property("ARPSYSTEMCOMPONENT", "1"), new Property("HIDHIDE_TRANSACTION", "") { IsDeferred = true, Secure = true, Hidden = true } };
        project.LaunchConditions = new List<LaunchCondition> { new LaunchCondition("HIDHIDE_TRANSACTION OR UPGRADINGPRODUCTCODE", "Run unified setup to prepare a protected maintenance transaction. This private MSI cannot install independently.") };
        var install = new ElevatedManagedAction(new Id("InstallHidHideDriver"), DriverActions.InstallDriver, Return.check, When.After, WixSharp.Step.InstallFiles,
            new Condition("NOT (REMOVE=\"ALL\")"), DriverActions.RollbackDriver);
        var remove = new ElevatedManagedAction(new Id("RemoveHidHideDriver"), DriverActions.RemoveDriver, Return.check, When.Before, WixSharp.Step.RemoveFiles,
            new Condition("REMOVE=\"ALL\" AND NOT UPGRADINGPRODUCTCODE"), DriverActions.RollbackDriver);
        foreach (var action in new[] { install, remove })
        {
            action.UsesProperties = "HIDHIDE_TRANSACTION";
            action.RefAssemblies = new[] { typeof(WindowsDriverBackend).Assembly.Location };
        }
        project.Actions = new WixSharp.Action[] { install, remove };
    }
}
