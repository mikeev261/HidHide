using System.Diagnostics;
using IOFile = System.IO.File;
using WixSharp;

namespace HidHide.Installer;

/// <summary>
/// Builds the Secure-Boot-compatible HidHide App Profiles companion MSI.
/// The package installs only user-mode tools and deliberately leaves the separately installed,
/// Microsoft-signed HidHide driver untouched.
/// </summary>
public static class Program
{
    const string Manufacturer = "mikeev261";

    /// <summary>
    /// Identifies only the user-mode companion product family. This must never match
    /// the official HidHide package, or MSI major-upgrade detection will uninstall
    /// the signed driver package while installing the companion.
    /// </summary>
    static readonly Guid UpgradeCode = new("7078E839-3A07-4FA9-BC3A-7677356C88CF");

    internal const string EnvStaging = "HIDHIDE_INSTALLER_STAGING";
    internal const string EnvOut = "HIDHIDE_INSTALLER_OUT";

    static int Main(string[] args)
    {
        try
        {
            if (args.Length == 1 && args[0] == "--inspect-installed")
            {
                var version = ProductContract.ReadVersion(Path.Combine(AppContext.BaseDirectory, "ProductVersion.props"));
                foreach (var product in InstalledProducts.Detect(version))
                    Console.WriteLine($"{product.Family:B} | {product.Product:B} | {product.Version} | {product.Operation} | {product.CachedPackage}");
                foreach (var deviceClass in DriverFilters.Classes)
                    Console.WriteLine($"UpperFilters {deviceClass:B}: {string.Join(", ", DriverFilters.Read(deviceClass))}");
                return 0;
            }
            var options = Options.Parse(args);
            options.Validate();

            string licensePath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "LICENSE.rtf"));
            if (!IOFile.Exists(licensePath))
                throw new FileNotFoundException($"License file not found: {licensePath}");

            string installRel = options.Unified ? @"%ProgramFiles64Folder%\HidHide" : @"%ProgramFiles64Folder%\HidHide App Profiles";
            // Must be a root-level Dir sibling of the install tree (WiX 5 / WIX0094); see WixSharp #1727, #1855.
            string startMenuRel = options.Unified ? @"%ProgramMenu%\HidHide" : @"%ProgramMenu%\HidHide App Profiles";
            string sd = options.StagingDir;
            var installDir = new Dir(
                installRel,
                new WixSharp.File(Path.Combine(sd, "HidHideClient.exe")),
                new WixSharp.File(Path.Combine(sd, "HidHideCLI.exe")))
            {
                IsInstallDir = true,
            };

            var startMenuDir = new Dir(
                startMenuRel,
                new ExeFileShortcut(
                    options.Unified ? ProductContract.Name : "HidHide App Profiles",
                    @"[INSTALLDIR]HidHideClient.exe",
                    "")
                {
                    WorkingDirectory = "[INSTALLDIR]",
                });

            var project = new ManagedProject(
                "HidHide App Profiles",
                installDir,
                startMenuDir)
            {
                GUID = new Guid("B7E9D4A2-6F31-4E88-9C0D-1A2B3C4D5E6F"),
                UpgradeCode = UpgradeCode,
                Platform = options.Platform,
                Scope = InstallScope.perMachine,
                UI = WUI.WixUI_FeatureTree,
                LicenceFile = licensePath,
                Version = ReadProductVersion(Path.Combine(options.StagingDir, "HidHideClient.exe")),
                OutDir = options.OutputDir,
                OutFileName = "HidHideAppProfiles",
            };

            project.ControlPanelInfo.Manufacturer = Manufacturer;
            if (options.Unified) UnifiedPreview.Configure(project, installDir, options.DriverPayload);

            // Align with WiX 5.x + WixToolset.UI.wixext/5.0.x; WiX 6 defaults are not compatible with WixSharp + WixUI without tweaks.
            WixExtension.UI.PreferredVersion = "5.0.2";

            ReleaseSigning.Configure(project);
            string msiPath = project.BuildMsi();
            Console.WriteLine(msiPath);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(ex.Message);
            Console.Error.WriteLine(ex);
            return 1;
        }
    }

    static Version ReadProductVersion(string clientExe)
    {
        var version = ProductContract.ReadVersion(Path.Combine(AppContext.BaseDirectory, "ProductVersion.props"));
        if (!IOFile.Exists(clientExe)) throw new FileNotFoundException("Missing versioned client", clientExe);
        var info = FileVersionInfo.GetVersionInfo(clientExe);
        if (ProductContract.ParseVersion(info.ProductVersion ?? "") != version || ProductContract.ParseVersion(info.FileVersion ?? "") != version)
            throw new InvalidDataException("Staged executable version disagrees with ProductVersion.props.");
        return version;
    }

    sealed class Options
    {
        public string StagingDir { get; }
        public string OutputDir { get; }
        public Platform Platform { get; }
        public bool Unified { get; private set; }
        public string DriverPayload { get; private set; } = "";

        Options(string stagingDir, string outputDir, Platform platform)
        {
            StagingDir = stagingDir;
            OutputDir = outputDir;
            Platform = platform;
        }

        public static Options Parse(string[] args)
        {
            string cwd = Directory.GetCurrentDirectory();
            string staging = Path.GetFullPath(Path.Combine(cwd, "staging"));
            string output = Path.GetFullPath(Path.Combine(cwd, "msi-out"));
            string arch = "x64";
            bool unified = false;
            string driverPayload = "";

            ApplyEnvironmentOverrides(ref staging, ref output);

            for (var i = 0; i < args.Length; i++)
            {
                string a = args[i];
                if (a is "--staging" or "-s")
                    staging = RequirePath(args, ref i, "staging");
                else if (a is "--out" or "-o")
                    output = RequirePath(args, ref i, "out");
                else if (a is "--platform" or "-p")
                    arch = RequireArg(args, ref i, "platform");
                else if (a == "--unified-preview") unified = true;
                else if (a == "--driver-payload") driverPayload = RequirePath(args, ref i, "driver-payload");
                else if (a is "--help" or "-h")
                    PrintHelp();
                else
                    throw new ArgumentException($"Unknown argument: {a}");
            }

            Platform platform =
                arch.Equals("ARM64", StringComparison.OrdinalIgnoreCase)
                    ? Platform.arm64
                    : arch.Equals("X64", StringComparison.OrdinalIgnoreCase) ||
                      arch.Equals("AMD64", StringComparison.OrdinalIgnoreCase) ||
                      arch.Equals("x64", StringComparison.OrdinalIgnoreCase)
                        ? Platform.x64
                        : throw new ArgumentException($"Unsupported platform: {arch}", nameof(arch));

            return new Options(
                staging,
                output,
                platform) { Unified = unified, DriverPayload = driverPayload };
        }

        static void ApplyEnvironmentOverrides(ref string staging, ref string output)
        {
            string? v = Environment.GetEnvironmentVariable(EnvStaging);
            if (!string.IsNullOrWhiteSpace(v))
                staging = Path.GetFullPath(v);

            v = Environment.GetEnvironmentVariable(EnvOut);
            if (!string.IsNullOrWhiteSpace(v))
                output = Path.GetFullPath(v);
        }

        static string RequirePath(string[] args, ref int i, string name)
        {
            if (i + 1 >= args.Length)
                throw new ArgumentException($"Missing value for --{name}.");
            i++;
            return Path.GetFullPath(args[i]);
        }

        static string RequireArg(string[] args, ref int i, string name)
        {
            if (i + 1 >= args.Length)
                throw new ArgumentException($"Missing value for --{name}.");
            i++;
            return args[i];
        }

        static void PrintHelp()
        {
            Console.WriteLine(
                """
                HidHide App Profiles — companion MSI builder (WixSharp / WiX 5)

                Options:
                  --staging, -s   Flat folder with all payload files (see INSTALL_LAYOUT.md)
                  --out, -o       MSI output directory
                  --platform, -p  x64 | ARM64 (default: x64)
                  --unified-preview Build the guarded private MSI development preview
                  --driver-payload Verified INF/SYS/CAT/license folder for the preview

                Environment (optional):
                  HIDHIDE_INSTALLER_STAGING, HIDHIDE_INSTALLER_OUT

                Staging must include HidHideClient.exe and HidHideCLI.exe.

                Requires Windows, WiX 5.0.2 global tool + WixToolset.UI.wixext/5.0.2:
                  dotnet tool install --global wix --version 5.0.2
                  wix extension add -g WixToolset.UI.wixext/5.0.2
                """);
            Environment.Exit(0);
        }

        public void Validate()
        {
            if (Unified && (Platform != Platform.x64 || string.IsNullOrEmpty(DriverPayload)))
                throw new ArgumentException("Unified preview requires x64 and --driver-payload pointing to the verified signed package.");
            if (Unified) HidHide.DriverSetup.Payload.Verify(DriverPayload);
            if (!Directory.Exists(StagingDir))
                throw new DirectoryNotFoundException($"Staging directory not found: {StagingDir}");

            foreach (string name in RequiredPayloadFiles)
            {
                string p = Path.Combine(StagingDir, name);
                if (!IOFile.Exists(p))
                    throw new FileNotFoundException($"Staging payload incomplete; missing: {p}");
                ExecutableArchitecture.Require(p, Platform == Platform.arm64);
                ReadProductVersion(p);
            }

            Directory.CreateDirectory(OutputDir);
        }

        static readonly string[] RequiredPayloadFiles =
        [
            "HidHideClient.exe",
            "HidHideCLI.exe",
        ];
    }
}
