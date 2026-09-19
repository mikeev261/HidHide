using System.Drawing;
using System.Windows.Forms;

namespace HidHide.Bootstrapper;

// An independent STA loop keeps cancellation responsive while the BA worker waits
// for authenticated controller messages or Burn completion callbacks.
internal sealed class ProgressWindow : IDisposable
{
    readonly Thread thread;
    readonly ManualResetEventSlim ready = new(false), decision = new(false), closed = new(false);
    Form? window;
    Label? title, status;
    ProgressBar? progress;
    Button? proceed, cancel, restore;
    Exception? failure;
    int cancellation;
    int disposed;
    bool started, complete, disposing, allowCancellation = true, restartPage;
    public IntPtr Handle { get; private set; }
    public bool CancellationRequested => Volatile.Read(ref cancellation) != 0;
    public bool RestoreLegacyRequested { get; private set; }
    public bool RestartRequested { get; private set; }

    public ProgressWindow(string operation, bool resume, bool visible = true, string? recoveryOperation = null, bool canRestoreLegacy = false, bool restartPending = false)
    {
        thread = new Thread(() =>
        {
            try
            {
                using var form = new Form
                {
                    Text = "HidHide Profiles Setup", ClientSize = new Size(560, 330), BackColor = Color.White,
                    FormBorderStyle = FormBorderStyle.FixedDialog, MaximizeBox = false,
                    StartPosition = FormStartPosition.CenterScreen, AutoScaleMode = AutoScaleMode.Dpi,
                    Font = new Font("Segoe UI", 9), ShowInTaskbar = visible
                };
                window = form;
                string operationName = operation == "uninstall" ? "Uninstall" : operation == "repair" ? "Repair" : "Install";
                string titleText = resume && restartPending ? "Restart required to finish setup" : resume && recoveryOperation == "uninstall" ? "Finish uninstalling HidHide Profiles" : resume ? "Finish setting up HidHide Profiles" : operationName + " HidHide Profiles";
                string statusText = resume && recoveryOperation == "uninstall"
                    ? "A previous uninstall needs to finish. Continue to verify the restart and complete removal."
                    : resume && restartPending ? "Restart Windows before continuing. If you have already restarted, select Continue to verify the restart and finish setup."
                    : resume ? "A previous setup operation needs to finish before HidHide Profiles can be used."
                    : operation == "uninstall" ? "This will remove HidHide Profiles and its device driver. Your exported profile backups are not affected."
                    : operation == "repair" ? "Setup will verify and repair the installed application and device driver."
                    : "Setup will install HidHide Profiles and its device driver. Windows may require a restart.";
                var footer = new Panel { Name = "Footer", Location = new Point(0, 260), Size = new Size(560, 70), BackColor = SystemColors.Control };
                var divider = new Panel { Location = new Point(0, 259), Size = new Size(560, 1), BackColor = Color.FromArgb(210, 210, 210) };
                title = new Label { Name = "Title", Text = titleText, AutoSize = false, Location = new Point(32, 34), Size = new Size(496, 40), Font = new Font("Segoe UI", 16, FontStyle.Regular) };
                status = new Label { Name = "Status", Text = statusText, AutoSize = false, Location = new Point(34, 92), Size = new Size(492, 92) };
                progress = new ProgressBar { Name = "Progress", Location = new Point(34, 214), Size = new Size(492, 18), Style = ProgressBarStyle.Blocks, Visible = false };
                string proceedText = resume && recoveryOperation == "uninstall" ? "Finish uninstall" : resume ? "Continue" : operationName;
                proceed = new Button { Name = "Continue", Text = proceedText, Location = new Point(326, 279), Size = new Size(105, 32) };
                cancel = new Button { Name = "Cancel", Text = "Cancel", Location = new Point(441, 279), Size = new Size(87, 32), DialogResult = DialogResult.None };
                restore = new Button { Name = "RestoreLegacy", Text = "Restore previous version", Location = new Point(32, 279), Size = new Size(180, 32), Visible = resume && canRestoreLegacy && recoveryOperation != "uninstall" };
                void Start(bool legacy)
                {
                    RestoreLegacyRequested = legacy; started = true; title.Text = operation == "uninstall" ? "Uninstalling HidHide Profiles" : resume ? "Finishing HidHide Profiles setup" : operation == "repair" ? "Repairing HidHide Profiles" : "Installing HidHide Profiles";
                    status.Text = "Please wait while Windows configures HidHide Profiles."; proceed.Visible = restore.Visible = false; progress.Visible = true; progress.Style = ProgressBarStyle.Marquee; decision.Set();
                }
                proceed.Click += (_, _) =>
                {
                    if (complete && restartPage) { RestartRequested = true; form.Close(); }
                    else if (!complete) Start(false);
                };
                restore.Click += (_, _) => Start(true);
                cancel.Click += (_, _) => { if (complete) form.Close(); else RequestCancellation(); };
                form.FormClosing += (_, args) =>
                {
                    if (!complete && !disposing)
                    {
                        args.Cancel = true;
                        RequestCancellation();
                    }
                };
                form.FormClosed += (_, _) => { closed.Set(); System.Windows.Forms.Application.ExitThread(); };
                form.Controls.AddRange(new Control[] { footer, divider, title, status, progress, proceed, cancel, restore });
                form.AcceptButton = proceed;
                Handle = form.Handle;
                // Ensure BeginInvoke dispatch is usable before publishing readiness.
                form.BeginInvoke(new Action(() => ready.Set()));
                if (visible) form.Show();
                System.Windows.Forms.Application.Run();
            }
            catch (Exception error) { failure = error; ready.Set(); decision.Set(); }
            finally { closed.Set(); }
        }) { IsBackground = true, Name = "HidHide setup progress" };
        thread.SetApartmentState(ApartmentState.STA); thread.Start();
        if (!ready.Wait(10000) || Handle == IntPtr.Zero || failure != null)
            throw new InvalidOperationException("Could not create the setup progress window.", failure);
    }

    public bool WaitForStart() { decision.Wait(); return started && !CancellationRequested; }
    public void RequestCancellation()
    {
        Invoke(() =>
        {
            if (!allowCancellation || complete || Interlocked.Exchange(ref cancellation, 1) != 0) return;
            cancel!.Enabled = false;
            status!.Text = started ? "Cancellation requested. Waiting for the current operation to reach a safe stopping point. Recovery data will be retained if needed." : "Setup cancelled before preparation.";
            decision.Set();
        });
    }
    public void Update(string text, int? percentage = null, bool allowCancel = true)
    {
        Invoke(() =>
        {
            allowCancellation = allowCancel;
            if (!CancellationRequested) status!.Text = text;
            cancel!.Enabled = allowCancel && !CancellationRequested;
            progress!.Visible = true;
            progress!.Style = percentage.HasValue ? ProgressBarStyle.Blocks : ProgressBarStyle.Marquee;
            if (percentage.HasValue) progress.Value = Math.Max(0, Math.Min(100, percentage.Value));
        });
    }
    public void Finish(string text, bool success, bool restartRequired = false)
    {
        Invoke(() =>
        {
            complete = true; restartPage = restartRequired; status!.Text = text;
            title!.Text = restartRequired ? "Restart required" : success ? "Setup complete" : "Setup couldn't finish";
            proceed!.Visible = restartRequired; proceed.Text = "Restart now";
            restore!.Visible = false; cancel!.Text = restartRequired ? "Restart later" : success ? "Finish" : "Close"; cancel.Enabled = true;
            progress!.Style = ProgressBarStyle.Blocks;
            progress.Visible = success && !restartRequired;
            if (success) progress.Value = 100;
            window!.AcceptButton = restartRequired ? proceed : cancel;
        });
    }
    public void WaitForClose() => closed.Wait();
    internal void Invoke(Action action)
    {
        var form = window;
        if (form == null || form.IsDisposed || closed.IsSet) return;
        try
        {
            if (form.InvokeRequired) form.Invoke(action); else action();
        }
        catch (InvalidOperationException) when (closed.IsSet || form.IsDisposed || !form.IsHandleCreated) { }
        catch (System.ComponentModel.InvalidAsynchronousStateException) when (closed.IsSet || !thread.IsAlive) { }
    }
    public void Dispose()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0) return;
        // A completed full-UI flow waits for the user to close the form before
        // Dispose runs. Do not Invoke onto an STA whose message loop has ended.
        if (!closed.IsSet) Invoke(() => { disposing = true; window!.Close(); });
        if (!thread.Join(5000)) throw new TimeoutException("Setup progress window did not close.");
        ready.Dispose(); decision.Dispose(); closed.Dispose();
    }
}
