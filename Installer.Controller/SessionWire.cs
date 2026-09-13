using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;

namespace HidHide.Setup;

// Length-prefixed, bounded UTF-8 records. Only fixed control words and the bounded
// ordinary-user snapshot cross this pipe. Never accept command lines or paths.
public sealed class SessionWire : IDisposable
{
    public const string RestartRequiredMessage = "The previous HidHide uninstall requires a Windows restart. Restart Windows, then run setup again.";
    readonly Stream stream;
    public SessionWire(Stream stream) { this.stream = stream; }
    public string Read(int timeoutMs = 30000)
    {
        var size = ReadBytes(4, timeoutMs); int n = BitConverter.ToInt32(size, 0);
        if (n < 0 || n > ReadySnapshot.Limit * 2 + 6) throw new InvalidDataException("Oversized setup message.");
        string value = new UTF8Encoding(false, true).GetString(ReadBytes(n, timeoutMs));
        var diagnostic = PublicDiagnostic.Decode(value);
        if (diagnostic != null) throw diagnostic;
        return value;
    }
    byte[] ReadBytes(int count, int timeout)
    {
        var bytes = new byte[count]; int offset = 0; var deadline = DateTime.UtcNow.AddMilliseconds(timeout);
        while (offset < count)
        {
            var read = stream.ReadAsync(bytes, offset, count - offset);
            int remaining = (int)Math.Max(0, (deadline - DateTime.UtcNow).TotalMilliseconds);
            if (!read.Wait(remaining)) { stream.Dispose(); throw new TimeoutException("Setup peer stopped responding."); }
            int n = read.GetAwaiter().GetResult(); if (n == 0) throw new EndOfStreamException("Setup peer disconnected."); offset += n;
        }
        return bytes;
    }
    public void Write(string value)
    {
        byte[] bytes = new UTF8Encoding(false, true).GetBytes(value);
        if (bytes.Length > ReadySnapshot.Limit * 2 + 6) throw new InvalidDataException("Oversized setup message.");
        byte[] message = BitConverter.GetBytes(bytes.Length).Concat(bytes).ToArray();
        var write = stream.WriteAsync(message, 0, message.Length);
        if (!write.Wait(5000)) { stream.Dispose(); throw new TimeoutException("Setup peer stopped reading."); }
        write.GetAwaiter().GetResult();
    }
    public void Expect(string value) { if (Read() != value) throw new InvalidDataException("Unexpected setup control."); }
    public void Dispose() => stream.Dispose();
    public static string Name(Guid id) => "HidHide.Setup." + id.ToString("D");
    public static uint ServerPid(NamedPipeClientStream pipe)
    { if (!GetNamedPipeServerProcessId(pipe.SafePipeHandle, out uint pid)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error()); return pid; }
    public static uint ClientPid(NamedPipeServerStream pipe)
    { if (!GetNamedPipeClientProcessId(pipe.SafePipeHandle, out uint pid)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error()); return pid; }
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool GetNamedPipeServerProcessId(Microsoft.Win32.SafeHandles.SafePipeHandle handle, out uint pid);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool GetNamedPipeClientProcessId(Microsoft.Win32.SafeHandles.SafePipeHandle handle, out uint pid);
}
