using System.Reflection;
using HidHide.Bootstrapper;
using HidHide.Setup;
using WixToolset.BootstrapperApplicationApi;

int checks = 0;
void Check(bool value, string name) { if (!value) throw new Exception(name); checks++; }
void Field(Application app, string name, object value) => typeof(Application).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(app, value);
T Read<T>(Application app, string name) => (T)typeof(Application).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(app)!;
void Callback(Application app, string name, object args) => typeof(Application).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(app, new[] { args });
Application Create(LaunchAction action)
{
    var app = new Application();
    Field(app, "command", new BootstrapperCommand(action, Display.None, "", 0, ResumeType.None, IntPtr.Zero, RelationType.Upgrade, false, "", "", ""));
    Field(app, "registrationOnly", true);
    return app;
}
foreach (PackageState state in Enum.GetValues(typeof(PackageState)))
{
    var app = Create(LaunchAction.Uninstall);
    Callback(app, "OnDetectPackageComplete", new DetectPackageCompleteEventArgs("Unified", 0, state, false));
    bool absent = state is PackageState.Absent or PackageState.Obsolete;
    Check(Read<bool>(app, "ownPackageAbsent") == absent, "exact absent states: " + state);
    foreach (LaunchAction action in new[] { LaunchAction.Install, LaunchAction.Uninstall })
    {
        app = Create(action);
        var planArgs = new PlanPackageBeginEventArgs("Unified", state, true, 0, 0, RequestState.ForceAbsent, 0, RequestState.ForceAbsent, 0, false);
        Callback(app, "OnPlanPackageBegin", planArgs);
        Check(planArgs.State == RequestState.None, "never executes MSI: " + action + state);
        Check(planArgs.Cancel == !(action == LaunchAction.Uninstall ? absent : state == PackageState.Present), "requires expected existing MSI ownership: " + action + state);
    }
}
{
    var app = Create(LaunchAction.Uninstall);
    var planArgs = new PlanPackageBeginEventArgs("Unexpected", PackageState.Absent, true, 0, 0, RequestState.Absent, 0, RequestState.Absent, 0, false);
    Callback(app, "OnPlanPackageBegin", planArgs);
    Check(planArgs.Cancel, "unknown chain package rejected");
    var compatible = new PlanCompatibleMsiPackageBeginEventArgs("Unified", "new-product", "2.2.0", true, true, false);
    Callback(app, "OnPlanCompatibleMsiPackageBegin", compatible);
    Check(!compatible.RequestRemove, "newer compatible MSI cannot be removed");
    var related = new PlanRelatedBundleEventArgs("new-bundle", RequestState.Absent, RequestState.Absent, false);
    Callback(app, "OnPlanRelatedBundle", related);
    Check(related.State == RequestState.None, "another bundle cannot be executed");
}
foreach (var version in new[] { "2.1.0.0", "2.2.0", "2.2.0.0", "2.3.0.0", "invalid" })
{
    var app = Create(LaunchAction.Install);
    Field(app, "bundleVersion", "2.2.0.0");
    var related = new DetectRelatedBundleEventArgs(Guid.NewGuid().ToString("B"), RelationType.Upgrade, UpgradeProtocol.Tag, true, version, false, false);
    Callback(app, "OnDetectRelatedBundle", related);
    Check(Read<List<string>>(app, "incompatibleRelated").Count == (version == "2.1.0.0" ? 0 : 1), "related bundle version gate: " + version);
}
void Click(ProgressWindow window, string control)
{
    window.Invoke(() =>
    {
        var form = (System.Windows.Forms.Form)typeof(ProgressWindow).GetField("window", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window)!;
        typeof(System.Windows.Forms.Control).GetMethod("OnClick", BindingFlags.Instance | BindingFlags.NonPublic)!
            .Invoke(form.Controls[control], new object[] { EventArgs.Empty });
    });
}
using (var window = new ProgressWindow("install", false, false))
{
    Check(window.Handle != IntPtr.Zero, "real responsive parent HWND exists before setup begins");
    var start = Task.Run(window.WaitForStart);
    Click(window, "Continue");
    Check(start.Wait(2000) && start.Result, "Continue button releases worker through actual UI event");
    window.Update("Caching payload", 37);
    window.Invoke(() =>
    {
        var form = (System.Windows.Forms.Form)typeof(ProgressWindow).GetField("window", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window)!;
        Check(form.Controls["Status"].Text == "Caching payload" && ((System.Windows.Forms.ProgressBar)form.Controls["Progress"]).Value == 37, "worker progress marshals to actual controls");
    });
    Click(window, "Cancel");
    Check(window.CancellationRequested, "Cancel button records cooperative request");
    var app = Create(LaunchAction.Install);
    Field(app, "registrationOnly", false); Field(app, "progressWindow", window);
    var progressArgs = new ProgressEventArgs(30, 45, false);
    Callback(app, "OnProgress", progressArgs);
    Check(progressArgs.Cancel, "Burn progress callback receives cancellation");
    var execute = new ExecuteProgressEventArgs("Unified", 30, 45, false);
    Callback(app, "OnExecuteProgress", execute);
    Check(execute.Cancel, "MSI execution progress receives cooperative cancellation");
    var cancelledPlan = new PlanPackageBeginEventArgs("Unified", PackageState.Absent, true, 0, 0, RequestState.Present, 0, RequestState.Present, 0, false);
    Callback(app, "OnPlanPackageBegin", cancelledPlan);
    Check(cancelledPlan.Cancel, "pending cancellation prevents new package planning");
    var rollback = new ExecutePackageBeginEventArgs("Unified", false, ActionState.Uninstall, 0, false, false);
    Callback(app, "OnExecutePackageBegin", rollback);
    Check(!rollback.Cancel, "rollback package cannot be cancelled");
    execute = new ExecuteProgressEventArgs("Unified", 50, 50, false);
    Callback(app, "OnExecuteProgress", execute);
    Check(!execute.Cancel, "rollback progress ignores previous cancellation");
    progressArgs = new ProgressEventArgs(50, 50, false);
    Callback(app, "OnProgress", progressArgs);
    Check(!progressArgs.Cancel, "overall rollback progress ignores previous cancellation");
    Field(app, "registrationOnly", true); Field(app, "rollingBack", false);
    progressArgs = new ProgressEventArgs(50, 50, false);
    Callback(app, "OnProgress", progressArgs);
    Check(!progressArgs.Cancel, "superseded registration protocol remains independent of user cancellation");
    window.Finish("Recovery retained", false);
    Click(window, "Cancel");
    Check(Task.Run(window.WaitForClose).Wait(2000), "Close button ends actual STA window loop after completion");
}
using (var window = new ProgressWindow("repair", true, false))
{
    Click(window, "Cancel");
    Check(!window.WaitForStart(), "initial Cancel refuses operation before preparation");
}
using (var window = new ProgressWindow("install", false, false))
{
    Click(window, "Continue");
    window.Update("Final verification", allowCancel: false);
    window.RequestCancellation();
    Check(!window.CancellationRequested, "final verification does not promise a late cancellation");
}
using (var window = new ProgressWindow("uninstall", false, false))
{
    Click(window, "Continue");
    window.Invoke(() =>
    {
        var form = (System.Windows.Forms.Form)typeof(ProgressWindow).GetField("window", BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(window)!;
        form.Close();
        Check(!form.IsDisposed && window.CancellationRequested, "titlebar Close requests cancellation and keeps active worker window alive");
    });
}
using (var window = new ProgressWindow("repair", true, false))
{
    Click(window, "RestoreLegacy");
    Check(window.WaitForStart() && window.RestoreLegacyRequested && !window.CancellationRequested, "explicit restore button selects legacy recovery through actual UI event");
    window.Finish("Previous installation restored", true);
    Click(window, "Cancel");
    Check(Task.Run(window.WaitForClose).Wait(2000), "legacy completion window closes cleanly");
}
foreach (bool recovering in new[] { false, true })
{
    int launches = 0;
    try { ElevationLaunch.Start(() => { launches++; throw new System.ComponentModel.Win32Exception(1223); }, recovering); throw new Exception("UAC refusal accepted"); }
    catch (ElevationNotApprovedException error)
    {
        Check(launches == 1 && ElevationLaunch.FailureExitCode(error, true) == 1602, "only pre-controller UAC refusal returns cancellation after ordinary-user preparation");
        Check(error.Message.Contains("Elevated setup did not start") && error.Message.Contains("recovery data") == recovering, "elevation message preserves existing recovery context");
    }
}
foreach (int code in new[] { 5, 2, 740 })
{
    var original = new System.ComponentModel.Win32Exception(code);
    try { ElevationLaunch.Start(() => throw original, false); throw new Exception("launch failure accepted"); }
    catch (System.ComponentModel.Win32Exception error) { Check(ReferenceEquals(error, original) && ElevationLaunch.FailureExitCode(error, true) == 1, "other launch errors retain failure semantics"); }
}
Check(ElevationLaunch.FailureExitCode(new System.ComponentModel.Win32Exception(1223), true) == 1, "later native cancellation is never reclassified as declined elevation");
Check(ElevationLaunch.FailureExitCode(new OperationCanceledException(), true) == 1, "prepared maintenance cancellation retains recovery failure semantics");
Check(ElevationLaunch.FailureExitCode(new OperationCanceledException(), false) == 1602, "existing early cooperative cancellation retained");
Console.WriteLine($"{checks} bootstrapper callback/window checks passed; no engine plan/apply or machine mutation.");
