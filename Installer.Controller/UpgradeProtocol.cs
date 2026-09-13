namespace HidHide.Setup;

// Both sides ship this contract. Older previews do not advertise it and may
// never be invoked under another setup's protected maintenance transaction.
public static class UpgradeProtocol
{
    public const string Tag = "HidHide.Upgrade.v1";
    public static bool CompatibleRelatedBundle(string tag, bool perMachine, bool missingFromCache) =>
        tag == Tag && perMachine && !missingFromCache;
    public static bool CanFinalizeRegistration(int packageCount, bool ownPackageAbsent) =>
        packageCount == 1 && ownPackageAbsent;
    public static bool OlderVersion(string related, string current) =>
        Version.TryParse(related, out var oldVersion) && Version.TryParse(current, out var newVersion) &&
        new Version(oldVersion.Major, oldVersion.Minor, Math.Max(0, oldVersion.Build)) < new Version(newVersion.Major, newVersion.Minor, Math.Max(0, newVersion.Build));
}
