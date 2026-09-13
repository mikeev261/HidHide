namespace HidHide.Setup;

public sealed class SetupPublicException : InvalidOperationException
{
    public string Code { get; }
    public SetupPublicException(string code) : base(PublicDiagnostic.Message(code)) { Code = code; }
}
public static class PublicDiagnostic
{
    static readonly string[] Stages = { "handoff", "cache", "driver-inspection", "product-policy", "legacy-files", "journal", "recovery-owner", "maintenance", "legacy-restoration", "package-result", "unknown" };
    static readonly string[] Types = { "InvalidOperationException", "InvalidDataException", "UnauthorizedAccessException", "IOException", "FileNotFoundException", "DirectoryNotFoundException", "TimeoutException", "Win32Exception", "ArgumentException", "PlatformNotSupportedException", "Exception" };
    public static string Message(string code) => code switch {
        "product-ownership" => "Setup found an unsupported, newer, or conflicting HidHide product. Use its original installer to repair or remove it before continuing.",
        "driver-ownership" => "Setup could not verify the installed driver as the supported signed HidHide driver. Preserve it and use its original installer for maintenance.",
        "legacy-files" => "The previous installation has missing, changed, or customized files or shortcuts. Setup cannot prove it can restore them; repair it with its original installer first.",
        "other-user" => "This recovery belongs to another Windows user. Sign in as the user who started setup and run this same installer.",
        "restart-required" => SessionWire.RestartRequiredMessage,
        "failure" => "Setup could not complete this stage. Existing recovery data has been retained.",
        _ => throw new InvalidDataException("Unknown public setup diagnostic.")
    };
    static string Stage(string stage) => Stages.Contains(stage) ? stage : "unknown";
    static string StageDescription(string stage) => stage switch {
        "handoff" => "confirming configuration ownership", "cache" => "verifying recovery files", "driver-inspection" => "checking the installed driver",
        "product-policy" => "checking installed product ownership", "legacy-files" => "checking the previous installation", "journal" => "saving recovery state",
        "recovery-owner" => "checking the recovery owner", "maintenance" => "performing maintenance", "legacy-restoration" => "restoring the previous installation",
        "package-result" => "verifying package completion", _ => "performing setup"
    };
    public static string Encode(string code, string stage, Exception error)
    {
        _ = Message(code);
        string type = error.GetType().Name;
        return "error:public:" + code + ":" + Stage(stage) + ":" + (Types.Contains(type) ? type : "Exception") + ":" + error.HResult.ToString("X8");
    }
    public static Exception? Decode(string value)
    {
        if (!value.StartsWith("error:", StringComparison.Ordinal)) return null;
        if (value == "error:restart-required") return new SetupPublicException("restart-required");
        if (value == "error:controller-failed") return new SetupPublicException("failure");
        if (value.Length > 160) return Malformed();
        var parts = value.Split(':');
        bool old = parts.Length == 5 && parts[1] == "controller";
        if (!old && (parts.Length != 6 || parts[1] != "public")) return Malformed();
        int index = old ? 2 : 3;
        if (!Stages.Contains(parts[index]) || !Types.Contains(parts[index + 1]) || !System.Text.RegularExpressions.Regex.IsMatch(parts[index + 2], @"\A[0-9A-F]{8}\z")) return Malformed();
        string code = old ? "failure" : parts[2]; string message;
        try { message = Message(code); } catch (InvalidDataException) { return Malformed(); }
        return new InvalidOperationException(message + " The failure occurred while " + StageDescription(parts[index]) + ". Diagnostic: " + code + "/" + parts[index] + "/" + parts[index + 1] + "/0x" + parts[index + 2] + ". Include this code when reporting the failure.");
    }
    static Exception Malformed() => new InvalidDataException("Setup returned an unsupported diagnostic. Existing recovery data has been retained.");
}
