using System.ComponentModel;
using System.Diagnostics;

namespace HidHide.Bootstrapper;

public static class ElevationLaunch
{
    // Only ShellExecute's launch failure is translated. An error from a running
    // controller or any later maintenance operation is never called UAC cancel.
    public static Process Start(Func<Process> launch, bool recovering)
    {
        try { return launch() ?? throw new InvalidOperationException("Elevated controller did not start."); }
        catch (Win32Exception error) when (error.NativeErrorCode == 1223)
        { throw new ElevationNotApprovedException(recovering, error); }
    }
    public static int FailureExitCode(Exception error, bool preparationStarted) =>
        error is ElevationNotApprovedException || error is OperationCanceledException && !preparationStarted ? 1602 : 1;
}

public sealed class ElevationNotApprovedException : Exception
{
    internal ElevationNotApprovedException(bool recovering, Exception inner) : base(
        "Windows elevation was not approved. Elevated setup did not start. " +
        (recovering ? "Existing recovery data has been retained. Run this same setup again to resume." :
        "Run setup again and approve the Windows elevation prompt to continue."), inner) { }
}
