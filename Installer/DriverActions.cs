using HidHide.DriverSetup;
using WixToolset.Dtf.WindowsInstaller;

namespace HidHide.Installer;

public static class DriverActions
{
    [CustomAction] public static ActionResult InstallDriver(Session session) => Execute(session, false, false);
    [CustomAction] public static ActionResult RemoveDriver(Session session) => Execute(session, true, false);
    [CustomAction] public static ActionResult RollbackDriver(Session session) => Execute(session, false, true);
    static ActionResult Execute(Session session, bool uninstall, bool rollback)
    {
        try
        {
            var text = session.CustomActionData["HIDHIDE_TRANSACTION"];
            if (!Guid.TryParseExact(text, "D", out var id) || id == Guid.Empty) throw new InvalidDataException("Setup transaction ID is missing or invalid.");
            // Only a protected machine journal can authorize this transaction.
            // An MSI public property supplies an identifier, never file paths,
            // driver names, baseline data or commands.
            var record = new ProtectedJournal(id).Load();
            if (!rollback && ((record.Operation == DriverSetup.Operation.Uninstall) != uninstall))
                throw new InvalidDataException("MSI action disagrees with the prepared operation.");
            int result = WorkerProcess.Run(rollback ? "--rollback" : "--apply", id, message => session.Log(message));
            // Deferred actions cannot call MsiSetMode. The protected worker
            // journal records RebootRequired; the controller reads it after MSI
            // completes and returns 3010 from the bundle without auto-restarting.
            if (result == 3010) return ActionResult.Success;
            if (result != 0) throw new InvalidOperationException("Driver worker failed with exit code " + result + "; protected recovery state retained.");
            return ActionResult.Success;
        }
        catch (Exception error)
        {
            session.Log("HidHide driver maintenance failed: " + error.Message);
            return ActionResult.Failure;
        }
    }
}
