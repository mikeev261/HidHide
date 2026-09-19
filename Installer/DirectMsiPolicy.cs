namespace HidHide.Installer;

// Pure policy shared by MSI actions and contract tests. An upgrade cannot
// depend on a command that did not exist in the installed older CLI. The
// elevated phase accepts that helperless path only after it verifies the
// existing pinned driver is fully healthy, then creates its own protected
// maintenance barrier. Repair and uninstall use the current installed CLI.
public static class DirectMsiPolicy
{
    public static (bool Uninstall, bool RemoveApplications) RemovalPlan(bool removeRequested, bool pendingUninstall) =>
        (removeRequested || pendingUninstall, pendingUninstall);

    // Application-only upgrades retain the healthy pinned driver. Preserve any
    // restart actually requested by native maintenance; never suppress it.
    public static bool RequiresRestart(string operation, bool recovery, bool driverReboot)
    {
        ValidateOperation(operation);
        return driverReboot || !recovery && operation != "upgrade";
    }

    static void ValidateOperation(string operation)
    {
        if (operation != "install" && operation != "upgrade" && operation != "repair" && operation != "uninstall")
            throw new ArgumentException("Unknown MSI operation.", nameof(operation));
    }

    public static bool NeedsResidentHelper(string operation, bool recovery)
    {
        ValidateOperation(operation);
        return !recovery && (operation == "repair" || operation == "uninstall");
    }
}
