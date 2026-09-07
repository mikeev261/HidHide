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
            // AppVeyor runs `build.ps1` once per platform; clearing all of `artifacts/` would delete the other arch's MSI.
            if (IsLocalBuild)
                EnsureCleanDirectory(ArtifactsDirectory);
            else
                EnsureCleanDirectory(StagingRoot);
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

            // The companion package deliberately excludes the kernel driver and
            // installs alongside an existing Microsoft-signed HidHide release.
            CopyFileToDirectory(OutputRoot / "HidHideClient.exe", StageDir, FileExistsPolicy.Fail);
            CopyFileToDirectory(OutputRoot / "HidHideCLI.exe", StageDir, FileExistsPolicy.Fail);
        });

    Target BuildMsi => _ => _
        .DependsOn(StageInstallerPayload)
        .Executes(() =>
        {
            // Run the WixSharp installer builder; on CI this runs on Windows.
            AbsolutePath installerProject = RootDirectory / "Installer" / "HidHide.Installer.csproj";
            AbsolutePath outDir = ArtifactsDirectory / "msi" / Platform;
            EnsureExistingDirectory(StageDir);
            EnsureExistingDirectory(outDir);

            var args =
                $"run --project \"{installerProject}\" -c {Configuration} -- " +
                $"--staging \"{StageDir}\" --out \"{outDir}\" --platform {Platform}";

            ProcessTasks.StartProcess("dotnet", args, RootDirectory)
                .AssertZeroExitCode();

            // Normalize output name so release scripts can rely on stable filenames.
            var builtMsi = Directory.GetFiles(outDir, "*.msi");
            if (builtMsi.Length != 1)
                throw new Exception($"Expected exactly one MSI in {outDir}, found {builtMsi.Length}.");

            var targetName = $"HidHideAppProfiles_{Platform}.msi";
            var targetPath = Path.Combine(outDir, targetName);
            if (File.Exists(targetPath))
                File.Delete(targetPath);
            File.Move(builtMsi[0], targetPath);
        });

    Target Ci => _ => _
        .DependsOn(UnitTest)
        .DependsOn(BuildMsi);

    void BuildProject(string project)
    {
        ParsePlatform(Platform);
        var logs = ArtifactsDirectory / "logs" / Platform;
        EnsureExistingDirectory(logs);
        var compiler = CompilerPath ?? ToolPathResolver.GetPathExecutable("MSBuild.exe");
        ProcessTasks.StartProcess(compiler,
            $"\"{RootDirectory / project / (project + ".vcxproj")}\" /t:Rebuild " +
            $"/p:Configuration=\"{Configuration}\" /p:Platform={Platform} " +
            $"/p:SolutionDir=\"{RootDirectory}/\" /m /nr:false /v:minimal " +
            $"/bl:\"{logs / (project + ".binlog")}\"", RootDirectory).AssertZeroExitCode();
    }

    public static int Main() => Execute<Build>(x => x.UnitTest);

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
