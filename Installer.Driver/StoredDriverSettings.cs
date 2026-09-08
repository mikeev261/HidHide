using Microsoft.Win32;
using System.Runtime.InteropServices;

namespace HidHide.DriverSetup;

public static class StoredDriverSettings
{
    public static DriverSettings Read(Func<string, (RegistryValueKind? Kind, object? Value)> read)
    {
        bool Flag(string name, bool optional = false)
        {
            var value = read(name);
            if (optional && value.Kind == null && value.Value == null) return false;
            if (value.Kind != RegistryValueKind.DWord || !(value.Value is int flag) || (flag != 0 && flag != 1)) throw new InvalidDataException("Invalid stored driver flag.");
            return flag != 0;
        }
        byte[] List(string name)
        {
            var value = read(name);
            if (value.Kind != RegistryValueKind.MultiString || !(value.Value is string[] strings)) throw new InvalidDataException("Invalid stored driver list.");
            var values = HidHide.Installer.DriverFilters.DecodeRegistryEntries(strings);
            if (values.Length > 4096 || values.Any(x => x.Length > 32767)) throw new InvalidDataException("Oversized stored driver list.");
            return SettingsCodec.Encode(values);
        }
        return new DriverSettings { Active = Flag("Active"), Inverse = Flag("WhitelistedInverse", true), Whitelist = List("WhitelistedFullImageNames"), Blacklist = List("BlacklistedDeviceInstancePaths") };
    }

    public static DriverSettings FromRegistry()
    {
        using var key = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\HidHide\Parameters") ?? throw new InvalidDataException("Stored driver settings are incomplete.");
        return Read(name =>
        {
            uint size = 0;
            int error = RegQueryValueExW(key.Handle.DangerousGetHandle(), name, IntPtr.Zero, out uint type, null, ref size);
            if (error == 2) return ((RegistryValueKind?)null, null);
            if (error != 0 || size > 1024 * 1024) throw new InvalidDataException("Stored driver value is unavailable or oversized.");
            var data = new byte[size]; uint actual = size;
            error = RegQueryValueExW(key.Handle.DangerousGetHandle(), name, IntPtr.Zero, out uint actualType, data, ref actual);
            if (error != 0 || actual != size || actualType != type) throw new InvalidDataException("Stored driver value changed during read.");
            object value = type == (uint)RegistryValueKind.DWord && data.Length == 4 ? (object)BitConverter.ToInt32(data, 0) :
                type == (uint)RegistryValueKind.MultiString ? SettingsCodec.Decode(data) : throw new InvalidDataException("Invalid stored driver value type.");
            return ((RegistryValueKind?)type, value);
        });
    }
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    static extern int RegQueryValueExW(IntPtr key, string name, IntPtr reserved, out uint type, byte[]? data, ref uint size);
}
