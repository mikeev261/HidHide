using System.IO.Pipes;
using System.Security.AccessControl;
using System.Security.Principal;
using HidHide.Setup;

static class SetupPipeSecurityTests
{
    public static void Run()
    {
        var user = new SecurityIdentifier("S-1-5-21-1-2-3-1000");
        var security = SetupPipeSecurity.Create(user);
        var rules = security.GetAccessRules(true, true, typeof(SecurityIdentifier)).Cast<PipeAccessRule>().ToArray();
        var admins = new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null);
        if (!security.AreAccessRulesProtected || rules.Length != 2 ||
            rules.Any(r => r.AccessControlType != AccessControlType.Allow || r.PipeAccessRights != PipeAccessRights.FullControl) ||
            !rules.Any(r => r.IdentityReference.Equals(user)) || !rules.Any(r => r.IdentityReference.Equals(admins)))
            throw new Exception("Elevation pipe must allow only initiating user and elevated administrators.");
        bool rejected = false;
        try { SetupPipeSecurity.Create(new SecurityIdentifier(WellKnownSidType.WorldSid, null)); }
        catch (ArgumentException) { rejected = true; }
        if (!rejected) throw new Exception("Broad principal accepted as configuration user.");
        // Exercise the actual Windows pipe ACL, not just a disconnected policy model.
        using var server = new NamedPipeServerStream("HidHide.PipeAcl.Test." + Guid.NewGuid().ToString("N"),
            PipeDirection.InOut, 1, PipeTransmissionMode.Byte, PipeOptions.Asynchronous, 4096, 4096,
            SetupPipeSecurity.Create(WindowsIdentity.GetCurrent().User!));
        if (!server.GetAccessControl().AreAccessRulesProtected)
            throw new Exception("Windows did not retain protected pipe ACL.");
        Console.WriteLine("3 elevation pipe security checks passed; alternate-account UAC still requires integration testing.");
    }
}
