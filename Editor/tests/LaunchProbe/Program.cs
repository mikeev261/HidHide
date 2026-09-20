using System.Text.Json;

var root = Environment.GetEnvironmentVariable("HIDHIDE_LAUNCH_PROBE_ROOT");
if (string.IsNullOrWhiteSpace(root) || !Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar).Equals(
        Path.GetFullPath(AppContext.BaseDirectory).TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase)
    || !Path.GetFileName(root.TrimEnd(Path.DirectorySeparatorChar)).StartsWith("HidHide-Profiles-Restart-Test-", StringComparison.Ordinal))
    return 2;

var marker = Path.Combine(root, "launch-verified.flag");
File.WriteAllText(Path.Combine(root, "launch-probe-started.json"), JsonSerializer.Serialize(new
{
    verifiedAtManagedEntry = File.Exists(marker),
    pid = Environment.ProcessId
}));
var stop = Path.Combine(root, "launch-probe-stop.flag");
for (var i = 0; i < 600 && !File.Exists(stop); i++) Thread.Sleep(50);
return 0;
