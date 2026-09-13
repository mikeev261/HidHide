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
byte[] Pe(ushort machine = 0x8664)
{
    var bytes = new byte[1024]; using var stream = new MemoryStream(bytes); using var writer = new BinaryWriter(stream);
    writer.Write((ushort)0x5A4D); stream.Position = 0x3C; writer.Write(0x80u);
    stream.Position = 0x80; writer.Write(0x00004550u); writer.Write(machine); writer.Write((ushort)1);
    stream.Position = 0x80 + 20; writer.Write((ushort)240); writer.Write((ushort)2); writer.Write((ushort)0x20B);
    stream.Position = 0x80 + 24 + 60; writer.Write(512u);
    stream.Position = 0x80 + 24 + 108; writer.Write(16u);
    stream.Position = 0x80 + 24 + 240 + 16; writer.Write(512u); writer.Write(512u); return bytes;
}
void ValidPe(byte[] bytes, bool arm64 = false) { using var stream = new MemoryStream(bytes); ExecutableArchitecture.Require(stream, arm64, "fixture.exe"); }
void BadPe(byte[] bytes, string name, bool arm64 = false)
{
    bool rejected = false;
    try { ValidPe(bytes, arm64); } catch (InvalidDataException error) { rejected = error.Message.Contains("fixture.exe"); }
    Check(rejected, name);
}
ValidPe(Pe()); Check(true, "x64 executable accepted");
ValidPe(Pe(0xAA64), true); Check(true, "historical ARM64 companion executable accepted");
BadPe(Pe(0xAA64), "ARM64 cannot enter x64 MSI");
BadPe(Pe(0x014C), "x86 cannot enter x64 MSI");
BadPe(Pe(0xA641), "ARM64EC cannot enter x64 MSI");
BadPe(Pe(), "x64 cannot enter ARM64 companion MSI", true);
foreach (int length in new[] { 0, 63, 128, 150, 200, 400, 1000 }) BadPe(Pe().Take(length).ToArray(), "truncated PE rejected");
foreach (var edit in new (int Offset, uint Value)[] { (0, 0), (0x3C, uint.MaxValue), (0x80, 0), (0x80 + 24 + 60, uint.MaxValue), (0x80 + 24 + 108, 17), (0x80 + 24 + 240 + 20, uint.MaxValue) })
{
    byte[] bytes = Pe(); BitConverter.GetBytes(edit.Value).CopyTo(bytes, edit.Offset); BadPe(bytes, "malformed PE bounds rejected");
}
foreach (var edit in new (int Offset, ushort Value)[] { (0x80 + 6, 0), (0x80 + 6, 97), (0x80 + 20, 2), (0x80 + 22, 0x2002), (0x80 + 22, 0), (0x80 + 24, 0x10B) })
{
    byte[] bytes = Pe(); BitConverter.GetBytes(edit.Value).CopyTo(bytes, edit.Offset); BadPe(bytes, "malformed executable header rejected");
}
foreach (string executable in args) { ExecutableArchitecture.Require(executable, false); Check(true, "actual staged x64 executable accepted"); }
Console.WriteLine($"{checks} installer contract checks passed.");
