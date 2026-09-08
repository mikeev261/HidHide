using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using Microsoft.Win32.SafeHandles;

namespace HidHide.DriverSetup;

internal static class OwnedDriverFile
{
    // Windows may retain a TrustedInstaller-owned copy after package removal.
    // Use a scoped restore privilege, never take ownership or weaken its ACL.
    public static void Remove(string path)
    {
        if (!OpenProcessToken(new IntPtr(-1), 0x28, out var token)) throw new Win32Exception(Marshal.GetLastWin32Error());
        using (token)
        {
            if (!LookupPrivilegeValueW(null, "SeRestorePrivilege", out var luid)) throw new Win32Exception(Marshal.GetLastWin32Error());
            var requested = new Privileges { Count = 1, Luid = luid, Attributes = 2 };
            if (!AdjustTokenPrivileges(token, false, ref requested, 16, out var previous, out _)) throw new Win32Exception(Marshal.GetLastWin32Error());
            int error = Marshal.GetLastWin32Error();
            try
            {
                if (error != 0) throw new Win32Exception(error);
                using var file = CreateFileW(path, 0x80010000, 1, IntPtr.Zero, 3, 0x02200000, IntPtr.Zero);
                if (file.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
                using var stream = new FileStream(file, FileAccess.Read);
                using var sha = SHA256.Create();
                if (stream.Length > 16 * 1024 * 1024 || BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "") != Payload.SysHash)
                    throw new InvalidDataException("Opened driver file identity changed; removal refused.");
                byte delete = 1;
                if (!SetFileInformationByHandle(file, 4, ref delete, 1)) throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            finally
            {
                if (!AdjustTokenPrivileges(token, false, ref previous, 16, out _, out _) || Marshal.GetLastWin32Error() != 0)
                    throw new InvalidOperationException("Could not restore maintenance token privileges.");
            }
        }
    }
    [StructLayout(LayoutKind.Sequential)] struct Luid { public uint Low; public int High; }
    [StructLayout(LayoutKind.Sequential)] struct Privileges { public uint Count; public Luid Luid; public uint Attributes; }
    [DllImport("advapi32.dll", SetLastError = true)] static extern bool OpenProcessToken(IntPtr process, uint access, out SafeAccessTokenHandle token);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)] static extern bool LookupPrivilegeValueW(string? system, string name, out Luid luid);
    [DllImport("advapi32.dll", SetLastError = true)] static extern bool AdjustTokenPrivileges(SafeAccessTokenHandle token, bool disableAll, ref Privileges value, int size, out Privileges previous, out int used);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)] static extern SafeFileHandle CreateFileW(string path, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool SetFileInformationByHandle(SafeFileHandle file, int kind, ref byte info, int size);
}
