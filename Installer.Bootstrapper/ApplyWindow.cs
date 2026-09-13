using System.Windows.Forms;

namespace HidHide.Bootstrapper;

// Burn requires a real parent HWND even for quiet execution. Keep its message
// loop independent of the worker that waits for engine completion events.
internal sealed class ApplyWindow : IDisposable
{
    readonly Thread thread;
    readonly ManualResetEventSlim ready = new(false);
    Form? window;
    Exception? failure;
    public IntPtr Handle { get; private set; }
    public ApplyWindow()
    {
        thread = new Thread(() =>
        {
            try
            {
                using var form = new Form { ShowInTaskbar = false, Text = "HidHide setup" };
                window = form; Handle = form.Handle; ready.Set();
                System.Windows.Forms.Application.Run();
            }
            catch (Exception error) { failure = error; ready.Set(); }
        }) { IsBackground = true };
        thread.SetApartmentState(ApartmentState.STA); thread.Start();
        if (!ready.Wait(10000) || Handle == IntPtr.Zero) throw new InvalidOperationException("Could not create setup's Windows Installer parent window.", failure);
    }
    public void Dispose()
    {
        if (window != null && !window.IsDisposed) window.BeginInvoke(new Action(System.Windows.Forms.Application.ExitThread));
        thread.Join(5000); ready.Dispose();
    }
}
