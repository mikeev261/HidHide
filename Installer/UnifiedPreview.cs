using HidHide.DriverSetup;
using System.Xml.Linq;
using WixSharp;

namespace HidHide.Installer;

// Public Windows Installer package. Standard MSI UI owns install, repair,
// upgrade, uninstall, rollback and reboot continuation. Protected custom
// actions own only the unchanged signed driver's lifecycle.
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
        string editor = Path.Combine(Path.GetDirectoryName(application.Files[0].Name)!, "Editor");
        if (!System.IO.File.Exists(Path.Combine(editor, "HidHideProfiles.exe")) || !System.IO.File.Exists(Path.Combine(editor, "resources", "app.asar")))
            throw new InvalidDataException("The independently packaged profiles editor is missing.");
        application.Dirs = new[] { new Dir("Driver", new[] { "HidHide.inf", "HidHide.sys", "hidhide.cat", "LICENSE.rtf" }
            .Select(name => new WixSharp.File(Path.Combine(payload, name))).ToArray()), EditorDirectory(editor) };
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
        project.OutFileName = "HidHide.Profiles";
        project.UI = WUI.WixUI_Minimal;
        project.Include(WixExtension.Util);
        project.Properties = new[]
        {
            // QueryNativeMachine is a first-sequence action. Preserve its UI
            // result across the client/server elevation boundary; for a no-UI
            // sequence the action runs directly in the execute sequence.
            new Property("WIX_NATIVE_MACHINE", "") { Secure = true },
            new RegValueProperty("HIDHIDE_WINDOWS_BUILD", RegistryHive.LocalMachine,
                @"SOFTWARE\Microsoft\Windows NT\CurrentVersion", "CurrentBuildNumber", "0")
            { Win64 = true, Secure = true },
            // Always retain a verbose Windows Installer log in the initiating
            // user's temp directory. Runtime custom-action failures must not
            // collapse to an unactionable error 1603 again.
            new Property("MsiLogging", "voicewarmupx"),
            new Property("HIDHIDE_UNINSTALL", "") { Secure = true },
            new Property("HIDHIDE_TRANSACTION", "") { IsDeferred = true, Secure = true, Hidden = true },
            new Property("HIDHIDE_INITIATING_SID", "") { IsDeferred = true, Secure = true, Hidden = true },
            new Property("HIDHIDE_OPERATION", "") { IsDeferred = true, Secure = true, Hidden = true },
            new Property("HIDHIDE_HELPER_PID", "0") { IsDeferred = true, Secure = true, Hidden = true },
            new Property("HIDHIDE_RECOVERY", "") { IsDeferred = true, Secure = true, Hidden = true },
        };
        project.LaunchConditions = new List<LaunchCondition>
        {
            // VersionNT64 is a compatibility value and cannot reliably identify
            // Windows 11. Query the real build and native machine instead.
            new LaunchCondition("Installed OR (WIX_NATIVE_MACHINE = 34404 AND HIDHIDE_WINDOWS_BUILD >= 22000 AND MsiNTProductType = 1)",
                "HidHide Profiles requires Windows 11 on an x64 PC."),
            new LaunchCondition("UILevel >= 3 OR AFTERREBOOT OR UPGRADINGPRODUCTCODE", "HidHide Profiles setup requires interactive Windows Installer UI because driver maintenance may pause for a restart."),
        };
        project.WixSourceGenerated += document =>
        {
            var package = document.Descendants().Single(element => element.Name.LocalName == "Package");
            var buildProperty = package.Elements().Single(element =>
                element.Name.LocalName == "Property" && (string?)element.Attribute("Id") == "HIDHIDE_WINDOWS_BUILD");
            // RegValueProperty does not propagate Property.Secure in WixSharp
            // 2.13. Mark the emitted WiX property explicitly so UI and execute
            // sequences receive the same detected value.
            buildProperty.SetAttributeValue("Secure", "yes");
            package.Add(new XElement(XName.Get("QueryNativeMachine", WixExtension.UtilNamespace)));
        };

        var operation = new ManagedAction(new Id("PrepareHidHideOperation"), DirectMsiActions.PrepareOperation, Return.check,
            When.Before, WixSharp.Step.CostFinalize, new Condition("NOT UPGRADINGPRODUCTCODE"))
        { Execute = Execute.immediate, Impersonate = true };
        var prepare = new ManagedAction(new Id("PrepareHidHideUser"), DirectMsiActions.PrepareUser, Return.check,
            When.Before, WixSharp.Step.InstallInitialize, new Condition("NOT UPGRADINGPRODUCTCODE"))
        { Execute = Execute.immediate, Impersonate = true };
        var apply = new ElevatedManagedAction(new Id("ApplyHidHideDriver"), DirectMsiActions.ApplyDriver, Return.check,
            When.Before, WixSharp.Step.ForceReboot, new Condition("NOT UPGRADINGPRODUCTCODE"), DirectMsiActions.RollbackDirectMsiDriver);
        var finalize = new ElevatedManagedAction(new Id("FinalizeHidHideDriver"), DirectMsiActions.FinalizeDriver, Return.check,
            When.After, WixSharp.Step.ForceReboot, new Condition("NOT UPGRADINGPRODUCTCODE"));
        foreach (var action in new ManagedAction[] { operation, prepare, apply, finalize })
        {
            action.UsesProperties = "HIDHIDE_TRANSACTION,HIDHIDE_INITIATING_SID,HIDHIDE_OPERATION,HIDHIDE_HELPER_PID,HIDHIDE_RECOVERY";
            action.RefAssemblies = new[] { typeof(WindowsDriverBackend).Assembly.Location };
        }
        project.Actions = new WixSharp.Action[] { operation, prepare, apply, finalize };
        project.ForceReboot = new ForceReboot
        {
            Step = WixSharp.Step.InstallFiles,
            When = When.After,
            Condition = new Condition("NOT AFTERREBOOT AND NOT UPGRADINGPRODUCTCODE AND NOT HIDHIDE_RECOVERY AND NOT WIX_UPGRADE_DETECTED AND (NOT Installed OR REINSTALL OR HIDHIDE_UNINSTALL)"),
        };
    }

    static Dir EditorDirectory(string path) => new Dir(Path.GetFileName(path),
        Directory.GetFiles(path).OrderBy(file => file, StringComparer.Ordinal).Select(file => new WixSharp.File(file)).ToArray())
    {
        Dirs = Directory.GetDirectories(path).OrderBy(directory => directory, StringComparer.Ordinal).Select(EditorDirectory).ToArray()
    };
}
