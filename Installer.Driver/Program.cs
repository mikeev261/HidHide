namespace HidHide.DriverSetup;
public static class Program
{
    public static int Main(string[] args)
    {
        try
        {
            if (args.Length == 2 && (args[0] == "--apply" || args[0] == "--rollback" || args[0] == "--resume") && Guid.TryParseExact(args[1], "D", out var id))
            {
                var journal = new ProtectedJournal(id);
                var record = journal.Load();
                using var lease = new MaintenanceLease(id, args[0] == "--resume");
                var backend = new WindowsDriverBackend(Path.Combine(ProtectedJournal.Root, "payload"), lease.AssertHeld);
                Payload.Verify(Path.Combine(ProtectedJournal.Root, "payload"));
                // No ordinary-user command can create this protected record or
                // approve resources. Setup must prepare it before invoking MSI.
                lease.MarkPending(args[0] == "--rollback");
                var transaction = new DriverTransaction(backend, journal, record);
                if (args[0] == "--apply" && string.IsNullOrEmpty(record.BootId)) { record.BootId = BootIdentity.Current(); journal.Save(record); }
                var outcome = args[0] == "--apply" ? transaction.Apply() : args[0] == "--resume" ? transaction.ResumeAfterReboot(BootIdentity.Current()) : transaction.Rollback();
                Console.WriteLine("Driver transaction: " + id + "; outcome: " + outcome);
                return outcome == JournalStatus.RebootRequired || outcome == JournalStatus.RollbackRebootRequired ? 3010 : 0;
            }
            if (args.Length != 1 || args[0] != "--inspect")
                throw new ArgumentException("Use --inspect, or a setup-authorized --apply/--rollback/--resume transaction GUID.");
            var state = new WindowsDriverBackend(AppContext.BaseDirectory).Inspect();
            Console.WriteLine("Service: " + state.ServiceExists + "; control: " + state.ControlAvailable + "; healthy: " + state.Healthy);
            Console.WriteLine("Binary SHA256: " + state.BinaryHash);
            foreach (var node in state.Nodes) Console.WriteLine("Node: " + node.Id + "; INF: " + node.Inf + "; service: " + node.Service + "; problem: " + node.Problem);
            foreach (var inf in state.Packages) Console.WriteLine("Package: " + inf);
            for (int i = 0; i < state.Filters.Length; i++) Console.WriteLine("Class " + HidHide.Installer.DriverFilters.Classes[i] + ": " + string.Join(", ", state.Filters[i].Entries));
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error.Message); return 1; }
    }
}
