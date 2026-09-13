using System.IO.Pipes;
using System.Security.AccessControl;
using System.Security.Principal;

namespace HidHide.Setup;

public static class SetupPipeSecurity
{
    // Credential elevation may use a different administrator account. This ACL
    // permits that connection; the BA still authenticates the exact child PID,
    // and the worker obtains the initiating SID from the pipe server's token.
    public static PipeSecurity Create(SecurityIdentifier initiatingUser)
    {
        if (initiatingUser == null || !initiatingUser.IsAccountSid())
            throw new ArgumentException("A configuration-user SID is required.", nameof(initiatingUser));
        var security = new PipeSecurity();
        security.SetAccessRuleProtection(true, false);
        security.AddAccessRule(new PipeAccessRule(initiatingUser, PipeAccessRights.FullControl, AccessControlType.Allow));
        security.AddAccessRule(new PipeAccessRule(new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null), PipeAccessRights.FullControl, AccessControlType.Allow));
        return security;
    }
}
