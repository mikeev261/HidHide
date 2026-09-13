using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Text;
using HidHide.DriverSetup;
using Microsoft.Win32.SafeHandles;

namespace HidHide.Setup;

[DataContract]
public sealed class LegacyServiceEvidence
{
    [DataMember] public bool Exists { get; set; }
    [DataMember] public bool Running { get; set; }
    [DataMember] public int StartMode { get; set; }
}

public sealed class LegacyServiceObservation
{
    public uint Type, StartMode, ErrorControl, State, Tag;
    public string BinaryPath = "", Account = "", Group = "", ProcessPath = "", ImageHash = "", DisplayName = "";
    public bool Dependencies, DelayedAutoStart;
}

// All service identifiers and paths are fixed here. Evidence never supplies a
// service name, executable, command line, account, or filesystem destination.
public static class LegacyServices
{
    const string Name = "HidHideWatchdog.exe";
    public const string ImageHash = "8B41058F5BBCDF85AF44489402B808F4E3A2B44B81AFC1C89169AC29501AAB93";
    public static string ImagePath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), @"Nefarius Software Solutions\HidHide\x64\HidHideWatchdog.exe");
    const uint Query = 1 | 4, Change = 2, Start = 16, Stop = 32;
    public static void Validate(LegacyServiceEvidence evidence)
    {
        if (evidence == null || !evidence.Exists || evidence.StartMode != 2)
            throw new InvalidDataException("Unsupported original watchdog service evidence.");
    }
    public static void ValidateObservation(LegacyServiceObservation value, bool allowStoppedMissingImage = false)
    {
        if (value.Type != 16 || value.StartMode < 2 || value.StartMode > 4 || value.ErrorControl != 1 ||
            value.Account != "LocalSystem" || value.Group != "" || value.Dependencies ||
            value.Tag != 0 || value.DelayedAutoStart || value.DisplayName != "HidHide Watchdog" ||
            !(string.Equals(value.BinaryPath, ImagePath, StringComparison.OrdinalIgnoreCase) || string.Equals(value.BinaryPath, "\"" + ImagePath + "\"", StringComparison.OrdinalIgnoreCase)) ||
            (value.ImageHash != ImageHash && !(allowStoppedMissingImage && value.State == 1 && value.ImageHash == "absent")) || value.State != 1 && value.State != 4 ||
            value.State == 4 && !string.Equals(value.ProcessPath, ImagePath, StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("Watchdog metadata, image or process state is not the supported owned service.");
    }
    public static LegacyServiceEvidence Capture()
    {
        using var manager = Manager(); using var service = Open(manager, Query, false)!;
        var value = Read(service); ValidateObservation(value);
        if (value.StartMode != 2) throw new InvalidOperationException("Original watchdog startup mode differs from the supported MSI.");
        var evidence = new LegacyServiceEvidence { Exists = true, Running = value.State == 4, StartMode = checked((int)value.StartMode) };
        Validate(evidence); return evidence;
    }
    public static void StopVerified(LegacyServiceEvidence evidence)
    {
        Validate(evidence); ProtectedJournal.RequireAdministrator();
        using var manager = Manager(); using var service = Open(manager, Query | Change | Stop, true);
        if (service == null) return; // The exact legacy MSI may have removed it.
        var value = Read(service, true); ValidateObservation(value, true); ValidateRecoveryMode(evidence, value);
        // Prevent automatic startup on the next recovery reboot. Original startup
        // and running state are restored only after file and baseline proof.
        if (value.StartMode != 4) SetStart(service, 4);
        if (value.State == 4)
        {
            if (!ControlService(service, 1, out _)) throw Error("Stop owned watchdog");
            Wait(service, 1);
        }
        value = Read(service, true); ValidateObservation(value, true);
        if (value.StartMode != 4 || value.State != 1) throw new InvalidOperationException("Watchdog quiescence did not verify.");
    }
    static void ValidateRecoveryMode(LegacyServiceEvidence evidence, LegacyServiceObservation value)
    {
        // A reinstall authors Automatic (2); our quiescence authors Disabled (4).
        // Other changes must match the exact original snapshot.
        if (value.StartMode != evidence.StartMode && value.StartMode != 2 && value.StartMode != 4)
            throw new InvalidOperationException("Watchdog startup mode changed outside recovery.");
    }
    public static void Restore(LegacyServiceEvidence evidence)
    {
        Validate(evidence); ProtectedJournal.RequireAdministrator();
        using var manager = Manager(); using var service = Open(manager, Query | Change | Start | Stop, false)!;
        var value = Read(service); ValidateObservation(value); ValidateRecoveryMode(evidence, value);
        if (!evidence.Running && value.State == 4)
        {
            if (!ControlService(service, 1, out _)) throw Error("Restore stopped watchdog state");
            Wait(service, 1);
        }
        if (value.StartMode != evidence.StartMode) SetStart(service, checked((uint)evidence.StartMode));
        if (evidence.Running && value.State == 1)
        {
            if (!StartServiceW(service, 0, IntPtr.Zero)) throw Error("Restore running watchdog state");
            Wait(service, 4);
        }
        Verify(evidence);
    }
    public static void Verify(LegacyServiceEvidence evidence)
    {
        Validate(evidence);
        using var manager = Manager(); using var service = Open(manager, Query, false)!;
        var value = Read(service); ValidateObservation(value);
        if (value.StartMode != evidence.StartMode || (value.State == 4) != evidence.Running)
            throw new InvalidOperationException("Original watchdog startup/running state was not restored.");
    }
    static ServiceHandle Manager()
    {
        var handle = OpenSCManagerW(null, null, 1);
        if (handle.IsInvalid) { handle.Dispose(); throw Error("Open service manager"); }
        return handle;
    }
    static ServiceHandle? Open(ServiceHandle manager, uint access, bool allowAbsent)
    {
        var handle = OpenServiceW(manager, Name, access);
        if (!handle.IsInvalid) return handle;
        int error = Marshal.GetLastWin32Error(); handle.Dispose();
        if (allowAbsent && error == 1060) return null;
        throw new Win32Exception(error, "Open owned watchdog service");
    }
    static LegacyServiceObservation Read(ServiceHandle service, bool allowStoppedMissingImage = false)
    {
        const uint length = 8192; var buffer = Marshal.AllocHGlobal((int)length);
        try
        {
            if (!QueryServiceConfigW(service, buffer, length, out _)) throw Error("Read watchdog configuration");
            var config = Marshal.PtrToStructure<Config>(buffer);
            if (!QueryServiceConfig2W(service, 3, out int delayed, 4, out _)) throw Error("Read watchdog delayed-start mode");
            var status = Status(service);
            string imageHash;
            if (allowStoppedMissingImage && status.State == 1 && ImageIsAbsent())
            {
                // A completed legacy file deletion may precede service removal.
                // Only quiescence accepts absence; no missing code is started.
                string parent = ImagePath;
                while (!Directory.Exists(parent) && !File.Exists(parent))
                    parent = Path.GetDirectoryName(parent) ?? throw new InvalidDataException("Invalid watchdog image path.");
                ProtectedJournal.RejectReparsePath(parent);
                // A directory at the executable path is not confirmed absence.
                if (Directory.Exists(ImagePath)) throw new InvalidDataException("Watchdog image path is a directory.");
                imageHash = "absent";
            }
            else
            {
                ProtectedJournal.RejectReparsePath(ImagePath);
                imageHash = Payload.Hash(ImagePath);
            }
            return new LegacyServiceObservation { Type = config.Type, StartMode = config.Start, ErrorControl = config.Error,
                BinaryPath = Marshal.PtrToStringUni(config.Binary) ?? "", Account = Marshal.PtrToStringUni(config.Account) ?? "",
                Group = Marshal.PtrToStringUni(config.Group) ?? "", Dependencies = config.Dependencies != IntPtr.Zero && Marshal.ReadInt16(config.Dependencies) != 0,
                Tag = config.Tag, DisplayName = Marshal.PtrToStringUni(config.DisplayName) ?? "", DelayedAutoStart = delayed != 0,
                State = status.State, ProcessPath = status.State == 4 ? ProcessPath(status.Pid) : "", ImageHash = imageHash };
        }
        finally { Marshal.FreeHGlobal(buffer); }
    }
    static string ProcessPath(uint pid)
    {
        if (pid == 0) throw new InvalidOperationException("Running watchdog has no process identity.");
        using var process = OpenProcess(0x1000, false, pid);
        if (process.IsInvalid) throw Error("Inspect watchdog process");
        var path = new StringBuilder(32768); uint size = (uint)path.Capacity;
        if (!QueryFullProcessImageNameW(process, 0, path, ref size)) throw Error("Read watchdog process image");
        return path.ToString();
    }
    static bool ImageIsAbsent()
    {
        // File.Exists also returns false on access errors. Those are unknown
        // outcomes and must not authorize the missing-file exception.
        try { _ = File.GetAttributes(ImagePath); return false; }
        catch (FileNotFoundException) { return true; }
        catch (DirectoryNotFoundException) { return true; }
    }
    static ServiceStatus Status(ServiceHandle service)
    {
        if (!QueryServiceStatusEx(service, 0, out var status, (uint)Marshal.SizeOf<ServiceStatus>(), out _)) throw Error("Read watchdog status");
        if (status.Type != 16) throw new InvalidOperationException("Unexpected watchdog service type.");
        return status;
    }
    static void Wait(ServiceHandle service, uint expected)
    {
        var timer = Stopwatch.StartNew();
        while (true)
        {
            uint state = Status(service).State;
            if (state == expected) return;
            if (state != (expected == 1 ? 3u : 2u)) throw new InvalidOperationException("Unexpected watchdog transition.");
            if (timer.ElapsedMilliseconds >= 15000) throw new TimeoutException("Watchdog transition remains pending; recovery retained.");
            Thread.Sleep(100);
        }
    }
    static void SetStart(ServiceHandle service, uint mode)
    {
        if (!ChangeServiceConfigW(service, uint.MaxValue, mode, uint.MaxValue, null, null, IntPtr.Zero, null, null, null, null))
            throw Error("Set owned watchdog startup mode");
    }
    static Exception Error(string message) => new Win32Exception(Marshal.GetLastWin32Error(), message);
    sealed class ServiceHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        public ServiceHandle() : base(true) { }
        protected override bool ReleaseHandle() => CloseServiceHandle(handle);
    }
    [StructLayout(LayoutKind.Sequential)] struct Config { public uint Type, Start, Error; public IntPtr Binary, Group; public uint Tag; public IntPtr Dependencies, Account, DisplayName; }
    [StructLayout(LayoutKind.Sequential)] struct ServiceStatus { public uint Type, State, Accepted, Win32Exit, ServiceExit, Checkpoint, WaitHint, Pid, Flags; }
    [StructLayout(LayoutKind.Sequential)] struct BasicStatus { public uint Type, State, Accepted, Win32Exit, ServiceExit, Checkpoint, WaitHint; }
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)] static extern ServiceHandle OpenSCManagerW(string? machine, string? database, uint access);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)] static extern ServiceHandle OpenServiceW(ServiceHandle manager, string name, uint access);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)] static extern bool QueryServiceConfigW(ServiceHandle service, IntPtr buffer, uint size, out uint needed);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)] static extern bool QueryServiceConfig2W(ServiceHandle service, uint level, out int value, uint size, out uint needed);
    [DllImport("advapi32.dll", SetLastError = true)] static extern bool QueryServiceStatusEx(ServiceHandle service, int level, out ServiceStatus status, uint size, out uint needed);
    [DllImport("advapi32.dll", SetLastError = true)] static extern bool ControlService(ServiceHandle service, uint control, out BasicStatus status);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)] static extern bool StartServiceW(ServiceHandle service, uint count, IntPtr arguments);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)] static extern bool ChangeServiceConfigW(ServiceHandle service, uint type, uint start, uint error, string? binary, string? group, IntPtr tag, string? dependencies, string? account, string? password, string? display);
    [DllImport("advapi32.dll", SetLastError = true)] static extern bool CloseServiceHandle(IntPtr service);
    [DllImport("kernel32.dll", SetLastError = true)] static extern SafeProcessHandle OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)] static extern bool QueryFullProcessImageNameW(SafeProcessHandle process, uint flags, StringBuilder path, ref uint size);
}
