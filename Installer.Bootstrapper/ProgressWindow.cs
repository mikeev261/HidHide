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
    TextBox? status;
    ProgressBar? progress;
    Button? proceed, cancel, restore;
    Exception? failure;
    int cancellation;
    bool started, complete, disposing, allowCancellation = true;
    public IntPtr Handle { get; private set; }
    public bool CancellationRequested => Volatile.Read(ref cancellation) != 0;
    public bool RestoreLegacyRequested { get; private set; }

    public ProgressWindow(string operation, bool resume, bool visible = true)
    {
        thread = new Thread(() =>
        {
            try
            {
                using var form = new Form
                {
                    Text = "HidHide (mikeev261 fork)", ClientSize = new Size(520, 245),
                    FormBorderStyle = FormBorderStyle.FixedDialog, MaximizeBox = false,
                    StartPosition = FormStartPosition.CenterScreen, AutoScaleMode = AutoScaleMode.Dpi,
                    Font = new Font("Segoe UI", 10), ShowInTaskbar = visible
                };
                window = form;
                var title = new Label { Text = resume ? "Resume HidHide setup" : "HidHide " + operation, AutoSize = false, Location = new Point(24, 20), Size = new Size(472, 32), Font = new Font(form.Font, FontStyle.Bold) };
                status = new TextBox { Name = "Status", Text = "Setup preserves your confirmed configuration. Windows may need a restart; setup never restarts it automatically.", Location = new Point(24, 62), Size = new Size(472, 78), Multiline = true, ReadOnly = true, BorderStyle = BorderStyle.None, BackColor = form.BackColor, ScrollBars = ScrollBars.Vertical };
                progress = new ProgressBar { Name = "Progress", Location = new Point(24, 151), Size = new Size(472, 18), Style = ProgressBarStyle.Blocks };
                proceed = new Button { Name = "Continue", Text = resume ? "Resume" : "Continue", Location = new Point(288, 194), Size = new Size(100, 32) };
                cancel = new Button { Name = "Cancel", Text = "Cancel", Location = new Point(396, 194), Size = new Size(100, 32) };
                restore = new Button { Name = "RestoreLegacy", Text = "Restore previous installation", Location = new Point(24, 194), Size = new Size(250, 32), Visible = resume };
                void Start(bool legacy) { RestoreLegacyRequested = legacy; started = true; proceed.Visible = restore.Visible = false; progress.Style = ProgressBarStyle.Marquee; decision.Set(); }
                proceed.Click += (_, _) => Start(false);
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
                form.Controls.AddRange(new Control[] { title, status, progress, proceed, cancel, restore });
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
            progress!.Style = percentage.HasValue ? ProgressBarStyle.Blocks : ProgressBarStyle.Marquee;
            if (percentage.HasValue) progress.Value = Math.Max(0, Math.Min(100, percentage.Value));
        });
    }
    public void Finish(string text, bool success)
    {
        Invoke(() =>
        {
            complete = true; status!.Text = text;
            proceed!.Visible = false; restore!.Visible = false; cancel!.Text = "Close"; cancel.Enabled = true;
            progress!.Style = ProgressBarStyle.Blocks;
            if (success) progress.Value = 100;
            window!.AcceptButton = cancel;
        });
    }
    public void WaitForClose() => closed.Wait();
    internal void Invoke(Action action)
    {
        if (window == null || window.IsDisposed) return;
        if (window.InvokeRequired) window.Invoke(action); else action();
    }
    public void Dispose()
    {
        Invoke(() => { disposing = true; window!.Close(); });
        if (!thread.Join(5000)) throw new TimeoutException("Setup progress window did not close.");
        ready.Dispose(); decision.Dispose(); closed.Dispose();
    }
}
