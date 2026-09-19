using System.Diagnostics;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.Principal;
using HidHide.DriverSetup;
using HidHide.Installer;
using Microsoft.Win32.SafeHandles;

namespace HidHide.Setup;

public static class Program
{
    public static int Main(string[] args)
    {
        try
        {
            if (args.Length == 1 && args[0] == "--inspect")
            {
                var version = typeof(Program).Assembly.GetName().Version!;
                foreach (var p in InstalledProducts.Detect(version)) Console.WriteLine(p.Product.ToString("B") + " | " + p.Version + " | " + p.Operation);
                return 0;
            }
            if (args.Length != 3 || args[0] != "--session" || !Guid.TryParseExact(args[1], "D", out var id) || id == Guid.Empty || !int.TryParse(args[2], out int parent) || parent <= 0)
                throw new ArgumentException("Use the unified Burn setup, or --inspect for read-only detection.");
            ProtectedJournal.RequireAdministrator();
            using var deadline = new NativeDeadline();
            using var pipe = new NamedPipeClientStream(".", SessionWire.Name(id), PipeDirection.InOut, PipeOptions.Asynchronous, TokenImpersonationLevel.Identification);
            pipe.Connect(30000);
            if (SessionWire.ServerPid(pipe) != parent) throw new UnauthorizedAccessException("Setup pipe server changed.");
            using var process = Process.GetProcessById(parent);
            if (!OpenProcessToken(process.Handle, 8, out var token)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            string sid;
            using (token) using (var identity = new WindowsIdentity(token.DangerousGetHandle())) sid = identity.User!.Value;
            // The elevated token can belong to a different administrator when a
            // standard user enters credentials. Configuration and recovery remain
            // bound to the authenticated ordinary-user server token, never admin HKCU.
            using var wire = new SessionWire(pipe);
            string request = wire.Read();
            bool restoreLegacy = request == "restore-legacy";
            bool recovery = request == "resume" || restoreLegacy;
            bool validateOnly = request == "validate";
            Operation operation = request == "install" || validateOnly ? Operation.Install : request == "repair" ? Operation.Repair : request == "uninstall" ? Operation.Uninstall : recovery ? Operation.Repair : throw new InvalidDataException("Unknown setup operation.");
            var store = new ControllerStore(id); SetupRecord record;
            SafeWaitHandle? retained = null;
            string stage = "handoff";
            try
            {
                if (!recovery)
                {
                    var ready = ReadySnapshot.Parse(wire.Read(), sid);
                    retained = OpenEventW(0x100000, false, @"Global\HidHide.AppProfiles.Maintenance.v1");
                    if (retained.IsInvalid) throw new InvalidOperationException("Maintenance preparation disappeared.");
                    wire.Write("handoff"); wire.Expect("handed-off");
                    using var preparationLease = new MaintenanceLease(id);
                    stage = "cache";
                    store.PrepareCache();
                    stage = "driver-inspection";
                    var state = new WindowsDriverBackend(Path.Combine(ProtectedJournal.Root, "payload")).Inspect();
                    if (ready.DriverPresent != state.ControlAvailable || ready.BaselineAvailable != (state.Settings != null) || ready.BaselineAvailable && (state.Settings == null || !state.Settings.Same(new DriverSettings { Active = ready.Active, Inverse = ready.Inverse, Blacklist = ReadySnapshot.MultiString(ready.Blacklist), Whitelist = ReadySnapshot.MultiString(ready.Whitelist) })))
                        throw new InvalidOperationException("Confirmed baseline changed before elevation handoff.");
                    var version = typeof(Program).Assembly.GetName().Version!;
                    stage = "product-policy";
                    var products = InstalledProducts.Detect(version);
                    operation = SetupPolicy.Select(operation, products, state);
                    record = new SetupRecord { Id = id, Sid = sid, Version = version.ToString(4), Operation = operation, Before = state, Boot = BootIdentity.Current(), Legacy = products.Where(x => x.Family != ProductContract.MsiUpgradeCode).Select(x => new LegacyProduct { Family = x.Family, Product = x.Product, Version = x.Version.ToString() }).ToList() };
                    store.PreparePackageRecord(record, products);
                    stage = "legacy-files";
                    if (record.Legacy.Count != 0) record.LegacyFiles = LegacyFileEvidence.Capture(products.Where(x => x.Family != ProductContract.MsiUpgradeCode).ToArray());
                    stage = "maintenance";
                    if (record.Legacy.Any(x => x.Family == ProductContract.UpstreamUpgradeCode)) record.LegacyService = LegacyServices.Capture();
                    store.Verify(record);
                    if (validateOnly) { wire.Write("validated"); return 0; }
                    stage = "journal";
                    store.Save(record);
                    new ProtectedJournal(id).Save(new TransactionRecord { Id = id, InitiatingSid = sid, Before = state, Operation = operation, BootId = record.Boot });
                }
                else { stage = "recovery-owner"; record = store.Load(); if (record.Sid != sid) throw new SetupPublicException("other-user"); }
                using var host = new WindowsSetupHost(id, Version.Parse(record.Version), recovery);
                stage = "maintenance";
                if (retained == null)
                {
                    retained = OpenEventW(0x100000, false, @"Global\HidHide.AppProfiles.Maintenance.v1");
                    if (retained.IsInvalid) throw new InvalidOperationException("Recovery exclusion disappeared.");
                }
                host.MarkPending(record);
                if (restoreLegacy)
                {
                    stage = "legacy-restoration";
                    var recoveryRecord = store.LoadOrCreateRecovery(host.Boot);
                    bool complete = new LegacyRecovery(host, record, recoveryRecord).Advance();
                    wire.Write(complete ? "complete:legacy" : "reboot");
                    return complete ? 0 : 3010;
                }
                var transaction = new SetupTransaction(host, record); SetupPhase phase = transaction.Advance();
                if (phase == SetupPhase.WaitingForReboot) host.MarkPending(record);
                if (phase == SetupPhase.MsiPending)
                {
                    host.ReleaseForMsi(); wire.Write("apply:" + record.Operation.ToString().ToLowerInvariant());
                    deadline.WaitForMsi();
                    string result = wire.Read(30 * 60 * 1000);
                    deadline.NativeWork();
                    if (result == "cancel:before-apply")
                    {
                        host.Reacquire(id);
                        transaction.CancelBeforeApply();
                        wire.Write("cancelled:resumable");
                        return 1602;
                    }
                    if (result != "result:0" && result != "result:3010" && result != "result:1") throw new InvalidDataException("Invalid MSI result.");
                    host.Reacquire(id); phase = transaction.MsiCompleted(int.Parse(result.Substring(7)));
                    if (phase == SetupPhase.WaitingForReboot) host.MarkPending(record);
                }
                wire.Write(phase == SetupPhase.Complete ? record.Operation == Operation.Uninstall ? "complete:uninstall" : "complete:install" : phase == SetupPhase.WaitingForReboot ? "reboot" : "recovery");
                return phase == SetupPhase.Complete ? 0 : phase == SetupPhase.WaitingForReboot ? 3010 : 1;
            }
            catch (Exception error)
            {
                // Send only fixed public errors, never baseline values or exception
                // text from protected paths. Keep the authenticated pipe alive here.
                try { wire.Write(PublicDiagnostic.Encode(error is DriverRestartRequiredException ? "restart-required" : error is SetupPublicException known ? known.Code : stage == "driver-inspection" && error is InvalidDataException ? "driver-ownership" : stage == "legacy-files" ? "legacy-files" : "failure", stage, error)); }
                catch (Exception) { /* The original failure remains authoritative. */ }
                throw;
            }
            finally { retained?.Dispose(); }
        }
        catch (Exception error) { Console.Error.WriteLine(error.Message); return 1; }
    }
    [DllImport("advapi32.dll", SetLastError = true)] static extern bool OpenProcessToken(IntPtr process, uint access, out SafeAccessTokenHandle token);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)] static extern SafeWaitHandle OpenEventW(uint access, bool inherit, string name);
    sealed class NativeDeadline : IDisposable
    {
        readonly Timer timer = new(_ => Process.GetCurrentProcess().Kill(), null, 180000, Timeout.Infinite);
        public void WaitForMsi() => timer.Change(31 * 60 * 1000, Timeout.Infinite);
        public void NativeWork() => timer.Change(180000, Timeout.Infinite);
        public void Dispose() => timer.Dispose();
    }
}
