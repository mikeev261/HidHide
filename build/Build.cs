using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

using Nuke.Common;
using Nuke.Common.CI.AppVeyor;
using Nuke.Common.IO;
using Nuke.Common.Tooling;
using Nuke.Common.Tools.MSBuild;

using static Nuke.Common.IO.FileSystemTasks;
using static Nuke.Common.Tools.MSBuild.MSBuildTasks;

class Build : NukeBuild
{
    [Parameter("Configuration to build - Default is 'Debug' (local) or 'Release' (server)")]
    readonly string Configuration = IsLocalBuild ? "Debug" : "Release";

    [Parameter("Platform to build: x64 | ARM64. Default is current CI platform or x64 locally.")]
    readonly string Platform = IsLocalBuild ? "x64" : (AppVeyor.Instance.Platform ?? "x64");

    [Parameter("Optional MSBuild.exe path when NUKE cannot discover the installed Visual Studio version.")]
    readonly string? CompilerPath;

    [Parameter("Verified unchanged x64 driver payload directory (or HIDHIDE_DRIVER_PAYLOAD).")]
    readonly string? DriverPayload = Environment.GetEnvironmentVariable("HIDHIDE_DRIVER_PAYLOAD");

    [Parameter("Windows SDK signtool.exe for kernel catalog verification.")]
    readonly string? SignTool;

    /// <summary>
    /// Explicit repo-root solution path so CI does not depend on NUKE solution injection / .nuke parameters.
    /// </summary>
    static AbsolutePath SolutionFile => RootDirectory / "HidHide.sln";

    AbsolutePath ArtifactsDirectory => RootDirectory / "artifacts";
    AbsolutePath StagingRoot => ArtifactsDirectory / "staging";
    AbsolutePath OutputRoot => RootDirectory / "bin" / Configuration / Platform;

    AbsolutePath StageDir => StagingRoot / Platform;

    Target Clean => _ => _
        .Before(Restore)
        .Executes(() =>
        {
            // artifacts also holds irreplaceable lifecycle journals and recovery packages.
            // Clean only this build's disposable staging directory.
            EnsureCleanDirectory(StageDir);
        });

    Target Restore => _ => _
        .Executes(() =>
        {
            EnsureGoogleTestNuGetPackage();
        });

    Target Compile => _ => _
        .DependsOn(Restore)
        .Executes(() =>
        {
            foreach (var project in new[] { "HidHideCLI", "HidHideClient", "HidHide.Tests" })
                BuildProject(project);
        });

    Target UnitTest => _ => _
        .DependsOn(Compile)
        // Cross-compilation is not runtime validation. Execute ARM64 only on ARM64 Windows.
        .OnlyWhenStatic(() => RuntimeInformation.IsOSPlatform(OSPlatform.Windows)
            && (Platform.Equals("x64", StringComparison.OrdinalIgnoreCase)
                || RuntimeInformation.OSArchitecture == Architecture.Arm64))
        .Executes(() =>
        {
            var testExe = OutputRoot / "HidHide.Tests.exe";
            if (!File.Exists(testExe))
                throw new FileNotFoundException($"Expected unit test runner at '{testExe}'. Build HidHide.Tests for {Configuration}|{Platform}.");

            var results = ArtifactsDirectory / "tests" / Platform;
            EnsureExistingDirectory(results);
            ProcessTasks.StartProcess(testExe, $"--gtest_output=xml:\"{results / "results.xml"}\"", workingDirectory: OutputRoot, logInvocation: false)
                .AssertZeroExitCode();
        });

    Target StageInstallerPayload => _ => _
        .DependsOn(Compile)
        .Executes(() =>
        {
            EnsureCleanDirectory(StageDir);

            // Compile only user-mode code. The separately verified signed driver is
            // supplied to the packaging stage unchanged.
            CopyFileToDirectory(OutputRoot / "HidHideClient.exe", StageDir, FileExistsPolicy.Fail);
            CopyFileToDirectory(OutputRoot / "HidHideCLI.exe", StageDir, FileExistsPolicy.Fail);
            ProcessTasks.StartProcess("pwsh.exe",
                $"-NoProfile -ExecutionPolicy Bypass -File \"{RootDirectory / "build" / "BuildEditor.ps1"}\" -Destination \"{StageDir / "Editor"}\"", RootDirectory).AssertZeroExitCode();
        });

    Target InstallerTests => _ => _
        .Executes(() =>
        {
            foreach (var project in new[] { "Installer.Tests", "Installer.Driver.Tests", "Installer.Controller.Tests" })
                ProcessTasks.StartProcess("dotnet", $"run --project \"{RootDirectory / project}\" -c Release", RootDirectory)
                    .AssertZeroExitCode();
            string evidenceArguments = $"-NoProfile -ExecutionPolicy Bypass -File \"{RootDirectory / "build" / "TestReleaseEvidence.ps1"}\"";
            ProcessTasks.StartProcess("pwsh.exe", evidenceArguments, RootDirectory).AssertZeroExitCode();
        });

    Target BuildSetup => _ => _
        .DependsOn(StageInstallerPayload)
        .Executes(() =>
        {
            if (!Platform.Equals("x64", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Unified setup supports only x64; ARM64 applications may be compiled separately.");
            var signingTool = ResolveSignTool();
            string? driverPayload = DriverPayload?.Trim('"');
            if (string.IsNullOrWhiteSpace(driverPayload))
            {
                var acquired = ArtifactsDirectory / "driver-payload" / Guid.NewGuid().ToString("N");
                var archiveCache = ArtifactsDirectory / "driver-cache";
                string acquireArguments = $"-NoProfile -ExecutionPolicy Bypass -File \"{RootDirectory / "build" / "AcquireSignedDriver.ps1"}\" " +
                    $"-Out \"{acquired}\" -Cache \"{archiveCache}\" -SignTool \"{signingTool}\"";
                ProcessTasks.StartProcess("pwsh.exe", acquireArguments, RootDirectory).AssertZeroExitCode();
                if (string.IsNullOrWhiteSpace(driverPayload)) driverPayload = acquired;
            }
            var outDir = ArtifactsDirectory / "setup" / ("x64-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N"));
            string setupArguments = $"-NoProfile -ExecutionPolicy Bypass -File \"{RootDirectory / "build" / "BuildUnifiedSetup.ps1"}\" " +
                $"-Staging \"{StageDir}\" -DriverPayload \"{driverPayload}\" " +
                $"-SignTool \"{signingTool}\" -Out \"{outDir}\"";
            ProcessTasks.StartProcess("pwsh.exe", setupArguments, RootDirectory).AssertZeroExitCode();
            string verificationArguments = $"-NoProfile -ExecutionPolicy Bypass -File \"{RootDirectory / "build" / "TestUnifiedPreview.ps1"}\" -Msi \"{outDir / "HidHide.Profiles.msi"}\"";
            ProcessTasks.StartProcess("pwsh.exe", verificationArguments, RootDirectory).AssertZeroExitCode();
        });

    Target Ci => _ => _
        .DependsOn(UnitTest, InstallerTests, BuildSetup);

    string ResolveSignTool()
    {
        if (!string.IsNullOrWhiteSpace(SignTool)) return SignTool.Trim('"');
        var kits = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Windows Kits", "10", "bin");
        if (Directory.Exists(kits))
        {
            var tool = Directory.GetDirectories(kits)
                .Select(path => new { Path = path, Version = Version.TryParse(Path.GetFileName(path), out var version) ? version : new Version(0, 0) })
                .OrderByDescending(item => item.Version)
                .Select(item => Path.Combine(item.Path, "x64", "signtool.exe")).FirstOrDefault(File.Exists);
            if (tool != null) return tool;
        }
        return ToolPathResolver.GetPathExecutable("signtool.exe");
    }

    void BuildProject(string project)
    {
        ParsePlatform(Platform);
        var logs = ArtifactsDirectory / "logs" / Platform;
        EnsureExistingDirectory(logs);
        var compiler = ResolveCompiler();
        ProcessTasks.StartProcess(compiler,
            $"\"{RootDirectory / project / (project + ".vcxproj")}\" /t:Rebuild " +
            $"/p:Configuration=\"{Configuration}\" /p:Platform={Platform} " +
            $"/p:SolutionDir=\"{RootDirectory}/\" /m /nr:false /v:minimal " +
            $"/bl:\"{logs / (project + ".binlog")}\"", RootDirectory).AssertZeroExitCode();
    }

    public static int Main() => Execute<Build>(x => x.UnitTest);

    string ResolveCompiler()
    {
        if (!string.IsNullOrWhiteSpace(CompilerPath)) return CompilerPath.Trim('"');
        var vswhere = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Microsoft Visual Studio", "Installer", "vswhere.exe");
        if (File.Exists(vswhere))
        {
            var result = ProcessTasks.StartProcess(vswhere,
                "-latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\\**\\Bin\\MSBuild.exe", logOutput: false)
                .AssertZeroExitCode();
            var discovered = result.Output.Select(line => line.Text.Trim()).FirstOrDefault(File.Exists);
            if (discovered != null) return discovered;
        }
        return ToolPathResolver.GetPathExecutable("MSBuild.exe");
    }

    /// <summary>Downloads and extracts the Microsoft Google Test NuGet package if missing (ignored by git under /packages).</summary>
    void EnsureGoogleTestNuGetPackage()
    {
        const string packageVersion = "1.8.1.7";
        var packageDir = RootDirectory / "packages" / $"Microsoft.googletest.v140.windesktop.msvcstl.static.rt-static.{packageVersion}";
        var marker = packageDir / "build" / "native" / "Microsoft.googletest.v140.windesktop.msvcstl.static.rt-static.targets";
        if (File.Exists(marker))
            return;

        Logger.Normal($"Downloading Google Test NuGet package {packageVersion} â€¦");
        var tempZip = RootDirectory / ".nuke" / "temp" / $"googletest.{packageVersion}.nupkg";
        EnsureExistingDirectory(tempZip.Parent);

        var url =
            $"https://api.nuget.org/v3-flatcontainer/microsoft.googletest.v140.windesktop.msvcstl.static.rt-static/{packageVersion}/microsoft.googletest.v140.windesktop.msvcstl.static.rt-static.{packageVersion}.nupkg";

        const int maxAttempts = 3;
        byte[]? payload = null;
        Exception? lastException = null;
        for (var attempt = 0; attempt < maxAttempts; attempt++)
        {
            try
            {
                using var http = new HttpClient();
                payload = http.GetByteArrayAsync(url).GetAwaiter().GetResult();
                break;
            }
            catch (Exception ex) when (attempt < maxAttempts - 1 && IsTransientNuGetDownloadFailure(ex))
            {
                lastException = ex;
                var jitter = Random.Shared.Next(0, 101);
                var delayMs = 200 * (1 << attempt) + jitter;
                Thread.Sleep(delayMs);
            }
        }

        if (payload == null)
            throw new InvalidOperationException(
                $"Failed to download Google Test NuGet package after {maxAttempts} attempts from {url}",
                lastException);

        File.WriteAllBytes(tempZip, payload);

        var verifyProcess = ProcessTasks.StartProcess(
            "dotnet",
            $"nuget verify \"{tempZip}\" --verbosity quiet",
            RootDirectory,
            logInvocation: false);
        verifyProcess.WaitForExit();
        if (verifyProcess.ExitCode != 0)
        {
            var log = string.Join(Environment.NewLine, verifyProcess.Output.Select(x => x.Text));
            throw new InvalidOperationException(
                $"Google Test NuGet package failed integrity verification (dotnet nuget verify exited with code {verifyProcess.ExitCode}).{Environment.NewLine}{log}");
        }

        if (Directory.Exists(packageDir))
            Directory.Delete(packageDir, true);
        EnsureExistingDirectory(packageDir);
        ZipFile.ExtractToDirectory(tempZip, packageDir);

        if (!File.Exists(marker))
            throw new InvalidOperationException($"Extracted Google Test package but marker file is missing: {marker}");
    }

    static bool IsTransientNuGetDownloadFailure(Exception ex) =>
        ex is HttpRequestException or TaskCanceledException or OperationCanceledException;

    static MSBuildTargetPlatform ParsePlatform(string platform)
    {
        if (string.IsNullOrWhiteSpace(platform))
            throw new ArgumentNullException(nameof(platform));

        // Maps Build.Platform parameter to Nuke.Common.Tools.MSBuild.MSBuildTargetPlatform.
        return platform.ToUpperInvariant() switch
        {
            "ARM64" => (MSBuildTargetPlatform)"ARM64",
            "X64" => MSBuildTargetPlatform.x64,
            _ => throw new ArgumentException($"Unsupported platform: {platform}", nameof(platform))
        };
    }

}
