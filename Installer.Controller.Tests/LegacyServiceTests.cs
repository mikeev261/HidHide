using HidHide.Setup;

static class LegacyServiceTests
{
    public static void Run()
    {
        int checks = 0;
        void Reject(Action action, string name) { try { action(); } catch { checks++; return; } throw new Exception(name); }
        LegacyServiceObservation Valid() => new() { Type = 16, StartMode = 2, ErrorControl = 1, Account = "LocalSystem", DisplayName = "HidHide Watchdog", BinaryPath = "\"" + LegacyServices.ImagePath + "\"", ImageHash = LegacyServices.ImageHash, State = 4, ProcessPath = LegacyServices.ImagePath };
        LegacyServices.Validate(new LegacyServiceEvidence { Exists = true, Running = true, StartMode = 2 }); checks++;
        LegacyServices.ValidateObservation(Valid()); checks++;
        var stopped = Valid(); stopped.State = 1; stopped.ProcessPath = ""; stopped.StartMode = 4;
        LegacyServices.ValidateObservation(stopped); checks++;
        stopped.ImageHash = "absent";
        LegacyServices.ValidateObservation(stopped, true); checks++;
        Reject(() => LegacyServices.ValidateObservation(stopped), "Missing image is never valid for capture/restore/verify");
        var runningMissing = Valid(); runningMissing.ImageHash = "absent";
        Reject(() => LegacyServices.ValidateObservation(runningMissing, true), "Running service without pinned image cannot be quiesced by inferred ownership");
        stopped.ImageHash = new string('0', 64);
        Reject(() => LegacyServices.ValidateObservation(stopped, true), "Stopped service with foreign existing image is preserved");
        stopped.ImageHash = "absent"; stopped.BinaryPath = @"C:\foreign\HidHideWatchdog.exe";
        Reject(() => LegacyServices.ValidateObservation(stopped, true), "Missing image does not weaken canonical service metadata proof");
        Reject(() => LegacyServices.Validate(new LegacyServiceEvidence()), "Absent original service must block restoration");
        Reject(() => LegacyServices.Validate(new LegacyServiceEvidence { Exists = true, StartMode = 3 }), "Modified original startup has no proven restoration source");
        foreach (Action<LegacyServiceObservation> change in new Action<LegacyServiceObservation>[] {
            x => x.Type = 32, x => x.StartMode = 0, x => x.ErrorControl = 0, x => x.Tag = 1, x => x.DelayedAutoStart = true, x => x.DisplayName = "Foreign",
            x => x.Account = @"NT AUTHORITY\LocalService", x => x.Group = "foreign", x => x.Dependencies = true,
            x => x.BinaryPath += " --foreign", x => x.BinaryPath = @"C:\foreign\HidHideWatchdog.exe",
            x => x.ImageHash = new string('0', 64), x => x.State = 2, x => x.State = 3,
            x => x.State = 7, x => x.ProcessPath = @"C:\foreign\HidHideWatchdog.exe", x => x.ProcessPath = "" })
        {
            var value = Valid(); change(value);
            Reject(() => LegacyServices.ValidateObservation(value), "Unknown service state/identity must be preserved");
        }
        Console.WriteLine($"{checks} legacy service ownership checks passed; no service mutations.");
    }
}
