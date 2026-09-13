using System.Globalization;
using System.Management;

namespace HidHide.DriverSetup;

public static class BootIdentity
{
    public static string Current()
    {
        using var search = new ManagementObjectSearcher("root\\CIMV2", "SELECT LastBootUpTime FROM Win32_OperatingSystem");
        search.Options.Timeout = TimeSpan.FromSeconds(10);
        using var values = search.Get();
        if (values.Count != 1) throw new InvalidOperationException("Cannot uniquely identify the current Windows boot.");
        foreach (ManagementObject value in values)
        {
            using (value)
            {
                var raw = value["LastBootUpTime"] as string ?? throw new InvalidDataException("Missing Windows boot timestamp.");
                return ManagementDateTimeConverter.ToDateTime(raw).ToUniversalTime().Ticks.ToString(CultureInfo.InvariantCulture);
            }
        }
        throw new InvalidOperationException("Missing Windows boot identity.");
    }
    public static bool Valid(string? value) => value != null && long.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out long ticks) &&
        ticks > 0 && ticks <= DateTime.MaxValue.Ticks && ticks.ToString(CultureInfo.InvariantCulture) == value;
}
