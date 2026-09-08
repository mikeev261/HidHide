using System.Diagnostics;
using WixSharp;

namespace HidHide.Installer;

internal static class ReleaseSigning
{
    internal static void Configure(ManagedProject project)
    {
        var tool = Environment.GetEnvironmentVariable("HIDHIDE_SIGN_TOOL");
        var certificate = Environment.GetEnvironmentVariable("HIDHIDE_SIGN_CERT");
        if (string.IsNullOrEmpty(tool) && string.IsNullOrEmpty(certificate)) return;
        if (string.IsNullOrWhiteSpace(tool) || string.IsNullOrWhiteSpace(certificate))
            throw new InvalidOperationException("Both signing tool and certificate subject are required.");
        // ContentSigningSignature is enabled only with SignAllFiles. Restrict the
        // signer itself to generated CA packages and MSI; application assemblies
        // were signed before this process and Microsoft payload stays unchanged.
        project.SignAllFiles = true;
        var output = Path.GetFullPath(project.OutDir).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        project.DigitalSignature = new GenericSigner
        {
            Implementation = file =>
            {
                var fullPath = Path.GetFullPath(file);
                if (!fullPath.StartsWith(output, StringComparison.OrdinalIgnoreCase) ||
                    !(fullPath.EndsWith(".CA.dll", StringComparison.OrdinalIgnoreCase) || fullPath.EndsWith(".msi", StringComparison.OrdinalIgnoreCase)))
                    return 0;
                Run(tool, "sign /v /n " + Quote(certificate) + " /tr http://timestamp.digicert.com /fd sha256 /td sha256 " + Quote(file));
                Run(tool, "verify /pa /v " + Quote(file));
                return 0;
            }
        };
    }

    static string Quote(string value)
    {
        if (value.Contains('"') || value.Contains('\r') || value.Contains('\n') || value.EndsWith("\\"))
            throw new ArgumentException("Unsupported signing argument.");
        return "\"" + value + "\"";
    }

    static void Run(string tool, string arguments)
    {
        using var process = Process.Start(new ProcessStartInfo(tool, arguments) { UseShellExecute = false, CreateNoWindow = true })
            ?? throw new InvalidOperationException("Cannot start signing tool.");
        process.WaitForExit();
        if (process.ExitCode != 0) throw new InvalidOperationException("Signing or signature verification failed: " + process.ExitCode);
    }
}
