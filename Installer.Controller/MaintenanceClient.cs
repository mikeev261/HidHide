using System.Diagnostics;
using System.Security.Principal;

namespace HidHide.Setup;

public sealed class MaintenanceClient : IDisposable
{
    readonly Process child;
    readonly Task drain;
    bool disposed;
    public string Ready { get; }
    public MaintenanceClient(string cli)
    {
        using (var current = WindowsIdentity.GetCurrent())
            if (new WindowsPrincipal(current).IsInRole(WindowsBuiltInRole.Administrator)) throw new UnauthorizedAccessException("Start setup as the ordinary configuration user, without Run as administrator.");
        child = Process.Start(new ProcessStartInfo(cli, "--maintenance-session") { UseShellExecute = false, CreateNoWindow = true, RedirectStandardInput = true, RedirectStandardOutput = true, RedirectStandardError = true })!;
        // Drain without accumulating or logging sensitive child output.
        drain = Task.Run(() => { var chars = new char[1024]; while (child.StandardError.Read(chars, 0, chars.Length) > 0) { } });
        try { Ready = ReadLine(); _ = ReadySnapshot.Parse(Ready, WindowsIdentity.GetCurrent().User!.Value); }
        catch { Dispose(); throw; }
    }
    string ReadLine()
    {
        var task = Task.Run(() => { var result = new System.Text.StringBuilder(); for (;;) { int c = child.StandardOutput.Read(); if (c < 0) throw new EndOfStreamException("Maintenance session exited before confirmation."); if (c == '\n') return result.ToString().TrimEnd('\r'); if (result.Length >= ReadySnapshot.Limit * 2 + 6) throw new InvalidDataException("Oversized maintenance response."); result.Append((char)c); } });
        if (!task.Wait(15000)) throw new TimeoutException("Maintenance preparation timed out.");
        return task.GetAwaiter().GetResult();
    }
    public void Handoff() { child.StandardInput.WriteLine("handoff"); child.StandardInput.Flush(); if (ReadLine() != "HANDED_OFF") throw new InvalidDataException("Maintenance handoff failed."); }
    public void Dispose()
    {
        if (disposed) return; disposed = true;
        try { child.StandardInput.Close(); if (!child.WaitForExit(7000)) child.Kill(); } catch (InvalidOperationException) { }
        child.Dispose();
    }
}
