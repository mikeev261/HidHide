namespace HidHide.DriverSetup;

public sealed class DriverRestartRequiredException : InvalidOperationException
{
    public DriverRestartRequiredException() : base("The previous HidHide uninstall requires a Windows restart. Restart Windows, then run setup again.") { }
}
