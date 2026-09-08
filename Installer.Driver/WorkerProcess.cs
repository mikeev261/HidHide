using System.Diagnostics;

namespace HidHide.DriverSetup;

public static class WorkerProcess
{
    public static int Run(string verb, Guid transaction, Action<string> log, int timeoutMilliseconds = 120000)
    {
        if (verb != "--apply" && verb != "--rollback" && verb != "--resume") throw new ArgumentException("Invalid driver worker operation.");
        if (transaction == Guid.Empty || timeoutMilliseconds < 1 || timeoutMilliseconds > 120000) throw new ArgumentException("Invalid worker bounds.");
        var start = new ProcessStartInfo(typeof(WorkerProcess).Assembly.Location, verb + " " + transaction.ToString("D"))
        { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
        using var process = new Process { StartInfo = start };
        int remaining = 65536;
        object gate = new();
        void Output(object sender, DataReceivedEventArgs args)
        {
            if (args.Data == null) return;
            lock (gate) { if (args.Data.Length <= remaining) { remaining -= args.Data.Length; log(args.Data); } }
        }
        process.OutputDataReceived += Output; process.ErrorDataReceived += Output;
        process.Start(); process.BeginOutputReadLine(); process.BeginErrorReadLine();
        if (!process.WaitForExit(timeoutMilliseconds))
        {
            // Killing a stuck SetupAPI caller is not rollback. Durable intent and
            // exclusion remain; MSI must fail and the controller must re-detect.
            try { process.Kill(); } catch (InvalidOperationException) { }
            if (!process.WaitForExit(5000)) throw new TimeoutException("Driver worker remains active; no further maintenance may run.");
            throw new TimeoutException("Driver worker timed out; outcome is unknown and recovery is required.");
        }
        return process.ExitCode;
    }
}
