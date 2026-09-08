using HidHide.Installer;

var checks = 0;
void Check(bool condition, string name) { if (!condition) throw new Exception(name); checks++; }
foreach (var text in new[] { "2.0.0.0", "0.0.0.0", "255.255.65535.0" })
    Check(ProductContract.ParseVersion(text).ToString() == text, "Valid version rejected");
foreach (var text in new[] { "", "1", "1.2.3", "1.2.3.1", "256.0.0.0", "1.256.0.0", "1.2.65536.0", "1.2.3.0-beta", "1.2.3.0+commit", "01.2.3.0", "1.2.3.0.0", "1.2.3.0\n", "99999999999999.0.0.0" })
{
    var rejected = false;
    try { ProductContract.ParseVersion(text); } catch (ArgumentException) { rejected = true; }
    Check(rejected, "Invalid version accepted: " + text);
}
var target = ProductContract.ParseVersion("2.0.0.0");
Check(ProductContract.UnifiedProductCode(new Version(2, 1, 0)) == ProductContract.UnifiedProductCode(new Version(2, 1, 0, 0)), "same MSI version has deterministic ProductCode");
Check(ProductContract.UnifiedProductCode(new Version(2, 1, 0)) != ProductContract.UnifiedProductCode(new Version(2, 2, 0)), "major upgrade has new ProductCode");
Check(ProductContract.Select(ProductContract.MsiUpgradeCode, Guid.NewGuid(), target, target) == ProductContract.Operation.Repair, "Repair");
Check(ProductContract.Select(ProductContract.MsiUpgradeCode, Guid.NewGuid(), new Version(1,9,0,0), target) == ProductContract.Operation.Upgrade, "Upgrade");
Check(ProductContract.Select(ProductContract.MsiUpgradeCode, ProductContract.UnifiedProductCode(new Version(2,1,0)), new Version(2,1,0,0), target) == ProductContract.Operation.RejectDowngrade, "Downgrade");
Check(ProductContract.Select(ProductContract.MsiUpgradeCode, Guid.NewGuid(), new Version(2,1,0), new Version(2,1,0)) == ProductContract.Operation.RejectUnknown, "same-family foreign same-version MSI rejected");
Check(ProductContract.Select(ProductContract.MsiUpgradeCode, Guid.NewGuid(), new Version(2,1,0), new Version(2,2,0)) == ProductContract.Operation.RejectUnknown, "same-family foreign older MSI rejected");
Check(ProductContract.Select(ProductContract.MsiUpgradeCode, ProductContract.UnifiedProductCode(new Version(2,1,0)), new Version(2,1,0), new Version(2,1,0)) == ProductContract.Operation.Repair, "exact compatible release MSI repairs");
Check(ProductContract.Select(ProductContract.CompanionUpgradeCode, new Guid("B7E9D4A2-6F31-4E88-9C0D-1A2B6C4D5E70"), new Version(99,0,0,0), target) == ProductContract.Operation.MigrateCompanion, "Legacy 99 migration");
Check(ProductContract.Select(ProductContract.CompanionUpgradeCode, Guid.NewGuid(), new Version(99,0,0,0), target) == ProductContract.Operation.RejectUnknown, "Unknown legacy identity");
Check(ProductContract.Select(ProductContract.UpstreamUpgradeCode, ProductContract.UpstreamProductCode, new Version(1,5,230), target) == ProductContract.Operation.MigrateUpstream, "Upstream migration");
Check(ProductContract.Select(ProductContract.UpstreamUpgradeCode, ProductContract.UpstreamProductCode, new Version(1,6,0), target) == ProductContract.Operation.RejectUnknown, "Unknown upstream version");
Check(DriverFilters.Change(new[] { "VendorA", "VendorB" }, true).SequenceEqual(new[] { "VendorA", "VendorB", "HidHide" }), "Attach preserves ordering");
Check(DriverFilters.Change(new[] { "VendorA", "hidhide", "VendorB" }, true).SequenceEqual(new[] { "VendorA", "hidhide", "VendorB" }), "Attach idempotence");
Check(DriverFilters.Change(new[] { "VendorA", "HidHide", "VendorB", "HIDHIDE" }, false).SequenceEqual(new[] { "VendorA", "VendorB" }), "Detach own entries only");
Check(DriverFilters.Change(new[] { "VendorHidHide", "HidHideOther" }, false).Length == 2, "No substring matching");
Check(DriverFilters.Change(Array.Empty<string>(), false).Length == 0, "Empty detach");
Check(DriverFilters.DecodeRegistryEntries(new[] { "" }).Length == 0, "Upstream empty REG_MULTI_SZ");
Check(DriverFilters.DecodeRegistryEntries(new[] { "VendorA", "VendorB" }).SequenceEqual(new[] { "VendorA", "VendorB" }), "Registry decoding preserves vendor filters");
bool malformedRejected = false;
try { DriverFilters.DecodeRegistryEntries(new[] { "VendorA", "" }); } catch (System.IO.InvalidDataException) { malformedRejected = true; }
Check(malformedRejected, "Mixed empty and vendor entries remain rejected");
Console.WriteLine($"{checks} installer contract checks passed.");
