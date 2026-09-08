using HidHide.DriverSetup;
using HidHide.Installer;

namespace HidHide.Setup;
public static class SetupPolicy
{
    public static Operation Select(Operation requested, IReadOnlyList<InstalledProduct> products, DriverState state)
    {
        if (state.ServicePendingDeletion) throw new DriverRestartRequiredException();
        if (products.Any(x => x.Operation is ProductContract.Operation.RejectDowngrade or ProductContract.Operation.RejectUnknown) || products.GroupBy(x => x.Family).Any(x => x.Count() > 1)) throw new SetupPublicException("product-ownership");
        bool upgrade = products.Any(x => x.Operation == ProductContract.Operation.Upgrade);
        if (upgrade && requested == Operation.Uninstall) throw new InvalidOperationException("Use the installed version's setup to uninstall it.");
        if (upgrade && products.Any(x => x.Version < new Version(2, 1, 0)))
            throw new InvalidOperationException("Development previews before 2.1 lack the related-bundle upgrade protocol. Uninstall the preview normally before installing this version.");
        bool unified = products.Any(x => x.Family == ProductContract.MsiUpgradeCode);
        if (requested == Operation.Uninstall && (products.Count != 1 || !unified)) throw new InvalidOperationException("Unified uninstall requires exactly the unified product.");
        if (unified && products.Count > 1) throw new InvalidOperationException("Mixed unified/legacy ownership requires recovery.");
        if (upgrade && !state.Healthy) throw new InvalidOperationException("Upgrade requires the healthy unchanged signed driver; repair the installed version first.");
        if (products.Count == 0 && !state.CanInstall) throw new InvalidOperationException("Orphaned driver resources block installation.");
        return requested != Operation.Uninstall && unified ? upgrade ? Operation.Upgrade : Operation.Repair : requested;
    }
}
