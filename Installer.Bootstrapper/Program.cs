using System.Diagnostics;
using System.IO.Pipes;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Windows.Forms;
using HidHide.Setup;
using Microsoft.Win32;
using WixToolset.BootstrapperApplicationApi;

namespace HidHide.Bootstrapper;

internal static class Program
{
    static int Main() { ManagedBootstrapperApplication.Run(new Application()); return 0; }
}

public sealed class Application : BootstrapperApplication
{
    IBootstrapperCommand command = null!;
    readonly ManualResetEventSlim detected = new(false), planned = new(false), applied = new(false);
    int detectStatus, planStatus, applyStatus;
    bool restart;
    bool registrationOnly;
    int packageCount;
    bool ownPackageAbsent;
    bool ownPackagePresent;
    readonly List<string> incompatibleRelated = new();
    string bundleVersion = "";
    ProgressWindow? progressWindow;
    bool rollingBack;
    bool CancellationRequested => !registrationOnly && progressWindow?.CancellationRequested == true;
    protected override void OnProgress(ProgressEventArgs args)
    {
        progressWindow?.Update(rollingBack ? "Restoring the previous installation…" : "Applying HidHide setup…", args.OverallPercentage, !rollingBack);
        if (!rollingBack && CancellationRequested) args.Cancel = true;
        base.OnProgress(args);
    }
    protected override void OnExecuteProgress(ExecuteProgressEventArgs args)
    {
        progressWindow?.Update(rollingBack ? "Restoring the previous installation…" : "Installing or removing the HidHide package…", args.OverallPercentage, !rollingBack);
        if (!rollingBack && CancellationRequested) args.Cancel = true;
        base.OnExecuteProgress(args);
    }
    protected override void OnExecutePackageBegin(ExecutePackageBeginEventArgs args)
    {
        rollingBack = !args.ShouldExecute;
        if (!rollingBack && CancellationRequested) args.Cancel = true;
        base.OnExecutePackageBegin(args);
    }
    protected override void OnDetectPackageComplete(DetectPackageCompleteEventArgs args)
    {
        packageCount++;
        if (args.PackageId == "Unified")
        {
            // WiX marks an absent old ProductCode Obsolete when its detect-only
            // upgrade row sees the newer MSI. Superseded still owns a ProductCode.
            ownPackageAbsent = args.Status >= 0 && (args.State == PackageState.Absent || args.State == PackageState.Obsolete);
            ownPackagePresent = args.Status >= 0 && args.State == PackageState.Present;
        }
        base.OnDetectPackageComplete(args);
    }
    protected override void OnDetectRelatedBundle(DetectRelatedBundleEventArgs args)
    {
        if (args.RelationType != RelationType.Upgrade || !UpgradeProtocol.CompatibleRelatedBundle(args.BundleTag, args.PerMachine, args.MissingFromCache) || !UpgradeProtocol.OlderVersion(args.Version, bundleVersion))
            incompatibleRelated.Add(args.ProductCode);
        base.OnDetectRelatedBundle(args);
    }
    protected override void OnPlanPackageBegin(PlanPackageBeginEventArgs args)
    {
        if (registrationOnly)
        {
            args.State = RequestState.None;
            bool expected = command.Action == LaunchAction.Uninstall ? args.CurrentState == PackageState.Absent || args.CurrentState == PackageState.Obsolete : args.CurrentState == PackageState.Present;
            if (args.PackageId != "Unified" || !expected) args.Cancel = true;
        }
        else if (CancellationRequested) args.Cancel = true;
        base.OnPlanPackageBegin(args);
    }
    protected override void OnPlanCompatibleMsiPackageBegin(PlanCompatibleMsiPackageBeginEventArgs args)
    {
        if (registrationOnly) args.RequestRemove = false;
        base.OnPlanCompatibleMsiPackageBegin(args);
    }
    protected override void OnPlanRelatedBundle(PlanRelatedBundleEventArgs args)
    {
        if (registrationOnly) args.State = RequestState.None;
        base.OnPlanRelatedBundle(args);
    }
    protected override void OnCreate(CreateEventArgs args) { base.OnCreate(args); command = args.Command; }
    protected override void OnDetectComplete(DetectCompleteEventArgs args) { detectStatus = args.Status; detected.Set(); base.OnDetectComplete(args); }
    protected override void OnPlanComplete(PlanCompleteEventArgs args) { planStatus = args.Status; planned.Set(); base.OnPlanComplete(args); }
    protected override void OnApplyComplete(ApplyCompleteEventArgs args)
    {
        applyStatus = args.Status; restart = args.Restart != ApplyRestart.None;
        args.Action = 0; applied.Set(); base.OnApplyComplete(args);
    }
    static void Wait(ManualResetEventSlim signal, int ms, string step) { if (!signal.Wait(ms)) throw new TimeoutException(step + " timed out; recovery data is retained."); }
    protected override void Run()
    {
        int exit = 1;
        Process? controller = null;
        bool preparationStarted = false;
        try
        {
            bundleVersion = engine.GetVariableVersion("WixBundleVersion").ToString();
            // Burn runs the superseded bundle after the new MSI's transactional
            // major upgrade. The old MSI must already be absent. This path can
            // unregister only this empty bundle; it cannot touch applications,
            // driver, maintenance marker, user completion or another bundle.
            if (command.Relation == RelationType.Upgrade && (command.Action == LaunchAction.Uninstall || command.Action == LaunchAction.Install))
            {
                registrationOnly = true;
                engine.Detect(); Wait(detected, 30000, "Superseded package detection");
                if (detectStatus < 0 || !UpgradeProtocol.CanFinalizeRegistration(packageCount, command.Action == LaunchAction.Uninstall ? ownPackageAbsent : ownPackagePresent))
                    throw new InvalidOperationException("Superseded setup still owns an application package; registration cleanup refused.");
                engine.Plan(command.Action); Wait(planned, 30000, "Superseded registration planning");
                if (planStatus < 0) throw new InvalidOperationException("Superseded registration planning failed.");
                using var parent = new ApplyWindow();
                engine.Apply(parent.Handle); Wait(applied, 120000, "Superseded registration removal");
                if (applyStatus < 0 || restart) throw new InvalidOperationException("Superseded registration removal failed.");
                engine.Quit(0); return;
            }
            if (command.CommandLine.Trim() == "--inspect-only")
            {
                engine.Detect(); Wait(detected, 30000, "Read-only package detection");
                if (detectStatus < 0) throw new InvalidOperationException("Read-only package detection failed.");
                using var inspect = Process.Start(new ProcessStartInfo(Path.Combine(AppContext.BaseDirectory, "HidHide.SetupController.exe"), "--inspect") { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true })!;
                var output = inspect.StandardOutput.ReadToEndAsync(); var errors = inspect.StandardError.ReadToEndAsync();
                if (!inspect.WaitForExit(15000)) { inspect.Kill(); throw new TimeoutException("Read-only controller inspection timed out."); }
                if (!Task.WaitAll(new Task[] { output, errors }, 5000) || inspect.ExitCode != 0) throw new InvalidOperationException("Read-only controller loading failed.");
                engine.Log(LogLevel.Standard, "HidHide inspect-only passed: WiX 5 bootstrapper loaded; no preparation, elevation, plan or apply requested.");
                engine.Quit(0); return;
            }
            if (command.Action == LaunchAction.Help)
            { MessageBox.Show("Install, repair or uninstall HidHide. Start as the ordinary configuration user. Setup never restarts Windows automatically.", "HidHide (mikeev261 fork)"); engine.Quit(0); return; }
            if (command.Action != LaunchAction.Install && command.Action != LaunchAction.Modify && command.Action != LaunchAction.Repair && command.Action != LaunchAction.Uninstall)
                throw new InvalidOperationException("This setup supports install, repair, uninstall and recovery only.");
            using (var user = WindowsIdentity.GetCurrent())
                if (new WindowsPrincipal(user).IsInRole(WindowsBuiltInRole.Administrator)) throw new InvalidOperationException("Start setup normally, without Run as administrator, so the configuration owner can confirm the baseline before elevation.");
            using var machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64);
            using (var service = machine.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\HidHide"))
                if ((int?)service?.GetValue("DeleteFlag") == 1) throw new InvalidOperationException(SessionWire.RestartRequiredMessage);
            using var marker = machine.OpenSubKey(@"SOFTWARE\mikeev261\HidHide\Maintenance");
            bool resume = marker != null;
            Guid id = resume ? Guid.ParseExact(marker!.GetValue("Transaction") as string ?? "", "D") : Guid.NewGuid();
            string operation = command.Action == LaunchAction.Uninstall ? "uninstall" : command.Action == LaunchAction.Repair || command.Action == LaunchAction.Modify ? "repair" : "install";
            if (command.Display == Display.Full)
            {
                progressWindow = new ProgressWindow(operation, resume);
                if (!progressWindow.WaitForStart()) { engine.Quit(1602); return; }
                progressWindow.Update("Checking installed packages…");
            }
            // Detect first, but never plan/apply before protected preparation.
            engine.Detect(); Wait(detected, 30000, "Package detection"); if (detectStatus < 0) throw new InvalidOperationException("Package detection failed.");
            if (incompatibleRelated.Count != 0) throw new InvalidOperationException("Related setup is not an older compatible cached version: " + string.Join(", ", incompatibleRelated) + ". Use the installed setup for same-version maintenance; uninstall unsupported previews normally first.");
            if (CancellationRequested) throw new OperationCanceledException("Setup cancelled before preparation.");
            progressWindow?.Update("Confirming your configuration and preparing elevation…");
            using var maintenance = resume ? null : new MaintenanceClient(Path.Combine(AppContext.BaseDirectory, "HidHideCLI.exe"));
            var security = SetupPipeSecurity.Create(WindowsIdentity.GetCurrent().User!);
            using var pipe = new NamedPipeServerStream(SessionWire.Name(id), PipeDirection.InOut, 1, PipeTransmissionMode.Byte, PipeOptions.Asynchronous, 4096, 4096, security);
            if (CancellationRequested) throw new OperationCanceledException("Setup cancelled before preparation.");
            var connection = pipe.WaitForConnectionAsync();
            preparationStarted = true;
            var worker = ElevationLaunch.Start(() => Process.Start(new ProcessStartInfo(Path.Combine(AppContext.BaseDirectory, "HidHide.SetupController.exe"), "--session " + id.ToString("D") + " " + Process.GetCurrentProcess().Id) { UseShellExecute = true, Verb = "runas", WindowStyle = ProcessWindowStyle.Hidden })!, resume);
            controller = worker;
            if (!connection.Wait(30000)) throw new TimeoutException("Elevated controller did not connect.");
            connection.GetAwaiter().GetResult();
            if (SessionWire.ClientPid(pipe) != worker.Id) throw new UnauthorizedAccessException("Unexpected controller process.");
            using var wire = new SessionWire(pipe);
            bool validateOnly = command.CommandLine.Trim() == "--validate-only";
            if (validateOnly && resume) throw new InvalidOperationException("Complete the existing maintenance transaction before validation.");
            wire.Write(validateOnly ? "validate" : progressWindow?.RestoreLegacyRequested == true ? "restore-legacy" : resume ? "resume" : operation);
            if (!resume) { wire.Write(maintenance!.Ready); wire.Expect("handoff"); maintenance.Handoff(); wire.Write("handed-off"); }
            progressWindow?.Update("Verifying recovery files and preparing maintenance. Cancellation waits for a safe stopping point…");
            string control = wire.Read(5 * 60 * 1000);
            if (validateOnly)
            {
                if (control != "validated" || !worker.WaitForExit(5000) || worker.ExitCode != 0) throw new InvalidOperationException("Preparation validation did not complete.");
                engine.Log(LogLevel.Standard, "HidHide validate-only passed: authenticated handoff, protected cache and driver/product preflight; no journal, marker, MSI plan or driver changes.");
                engine.Quit(0); return;
            }
            if (control.StartsWith("apply:", StringComparison.Ordinal))
            {
                var action = control == "apply:install" || control == "apply:upgrade" ? LaunchAction.Install : control == "apply:repair" ? LaunchAction.Repair : control == "apply:uninstall" ? LaunchAction.Uninstall : throw new InvalidDataException("Unknown controller apply command.");
                engine.SetVariableString("HidHideTransaction", id.ToString("D"), false);
                progressWindow?.Update("Planning the application package…");
                if (CancellationRequested) wire.Write("cancel:before-apply");
                else
                {
                    engine.Plan(action); Wait(planned, 30000, "Package planning");
                    if (planStatus < 0 || CancellationRequested) wire.Write("result:1");
                    else
                    {
                        using var parent = progressWindow == null ? new ApplyWindow() : null;
                        progressWindow?.Update("Applying the application package…", 0);
                        engine.Apply(progressWindow?.Handle ?? parent!.Handle); Wait(applied, 30 * 60 * 1000, "Package execution");
                        wire.Write(applyStatus < 0 ? "result:1" : restart ? "result:3010" : "result:0");
                    }
                }
                progressWindow?.Update("Verifying the final state and preserving recovery data…", allowCancel: false);
                control = wire.Read(5 * 60 * 1000);
            }
            if (control == "cancelled:resumable")
            {
                if (!worker.WaitForExit(5000) || worker.ExitCode != 1602) throw new InvalidOperationException("Cancellation acknowledgement did not verify.");
                progressWindow?.Finish("Setup paused before package execution. Recovery files and your configuration are preserved. Run this same setup again to resume.", false);
                progressWindow?.WaitForClose();
                engine.Quit(1602); return;
            }
            if (control == "recovery" && CancellationRequested)
                throw new InvalidOperationException("Setup was cancelled at a safe stopping point. Maintenance is incomplete and recovery data has been retained. Run this same setup again to continue.");
            exit = control == "complete:install" || control == "complete:uninstall" || control == "complete:legacy" ? 0 : control == "reboot" ? 3010 : throw new InvalidOperationException("Setup remains incomplete; rerun this exact setup to inspect recovery.");
            if (!worker.WaitForExit(5000) || worker.ExitCode != exit) throw new InvalidOperationException("Controller completion did not verify.");
            maintenance?.Dispose();
            if (exit == 0 && control != "complete:legacy") UserCompletion.Apply(control == "complete:uninstall");
            if (progressWindow != null)
            {
                progressWindow.Finish(control == "complete:legacy" ? "The previous installation and configuration were restored and verified. Run setup again when you are ready to migrate." : exit == 0 ? "HidHide maintenance completed and verified." : progressWindow.RestoreLegacyRequested ? "Restart Windows, then run this setup and select Restore previous installation again. Recovery may require several restarts; configuration remains suspended until verification completes." : "Restart Windows when convenient, then run this setup again to finish verification. Configuration remains suspended until recovery completes.", exit == 0);
                progressWindow.WaitForClose();
            }
        }
        catch (Exception error)
        {
            exit = ElevationLaunch.FailureExitCode(error, preparationStarted);
            engine.Log(LogLevel.Error, error.GetType().Name + ": " + error.Message);
            if (progressWindow != null) { progressWindow.Finish(error.Message, false); progressWindow.WaitForClose(); }
            else if (command.Display == Display.Full) MessageBox.Show(error.Message, "HidHide setup could not complete", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally
        {
            // Only the exact controller process we started is supervised. A
            // timeout never clears durable exclusion or proves OS cancellation.
            try
            {
                if (controller != null && !controller.HasExited)
                {
                    // A cancel request is cooperative. Never terminate an active
                    // native operation to manufacture a cancellation result.
                    if (CancellationRequested) engine.Log(LogLevel.Standard, "Cancellation requested; controller supervision and protected recovery remain active.");
                    else { controller.Kill(); controller.WaitForExit(5000); }
                }
            }
            catch (InvalidOperationException) { }
            catch (System.ComponentModel.Win32Exception) { engine.Log(LogLevel.Error, "Controller termination could not be confirmed; its own deadline remains active and durable exclusion is retained."); }
            controller?.Dispose();
            progressWindow?.Dispose();
        }
        engine.Quit(exit);
    }
}
