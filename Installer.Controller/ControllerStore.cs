using HidHide.DriverSetup;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Security.AccessControl;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Xml;

namespace HidHide.Setup;

public sealed class ControllerStore
{
    public string Cache => Path.Combine(ProtectedJournal.Root, id.ToString("D") + ".cache");
    readonly Guid id;
    public ControllerStore(Guid id) { if (id == Guid.Empty) throw new InvalidDataException("Empty setup ID."); this.id = id; }
    static readonly SecurityIdentifier Admins = new(WellKnownSidType.BuiltinAdministratorsSid, null);
    static readonly SecurityIdentifier SystemSid = new(WellKnownSidType.LocalSystemSid, null);
    static FileSecurity FileAcl()
    {
        var acl = new FileSecurity(); acl.SetAccessRuleProtection(true, false); acl.SetOwner(Admins);
        foreach (var sid in new[] { Admins, SystemSid }) acl.AddAccessRule(new FileSystemAccessRule(sid, FileSystemRights.FullControl, AccessControlType.Allow)); return acl;
    }
    static void DirectoryAt(string path)
    {
        if (!Directory.Exists(path))
        {
            var acl = new DirectorySecurity(); acl.SetAccessRuleProtection(true, false); acl.SetOwner(Admins);
            foreach (var sid in new[] { Admins, SystemSid }) acl.AddAccessRule(new FileSystemAccessRule(sid, FileSystemRights.FullControl, InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit, PropagationFlags.None, AccessControlType.Allow));
            Directory.CreateDirectory(path, acl);
        }
        ProtectedJournal.ValidateDirectory(path);
    }
    static readonly string[] ApplicationFiles = { "HidHideCLI.exe", "HidHideClient.exe", "mfc140u.dll", "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll" };
    static readonly string[] Allowed = { "HidHide.inf", "HidHide.sys", "hidhide.cat", "LICENSE.rtf", "HidHide.Unified.Preview.msi", "HidHideCLI.exe", "HidHideClient.exe", "mfc140u.dll", "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll", "UpstreamRecovery.exe", "Companion1Recovery.msi", "Companion99Recovery.msi" };
    static Dictionary<string, string> Index()
    {
        using var source = typeof(ControllerStore).Assembly.GetManifestResourceStream("cache.index") ?? throw new InvalidDataException("Controller was built without a verified recovery cache.");
        using var reader = new StreamReader(source); var result = new Dictionary<string, string>(StringComparer.Ordinal);
        string? line;
        while ((line = reader.ReadLine()) != null)
        {
            var parts = line.Split('|');
            if (parts.Length != 2 || !Allowed.Contains(parts[0]) || !System.Text.RegularExpressions.Regex.IsMatch(parts[1], "\\A[A-F0-9]{64}\\z") || result.ContainsKey(parts[0])) throw new InvalidDataException("Invalid embedded cache index.");
            result.Add(parts[0], parts[1]);
        }
        foreach (var name in Allowed.Take(11)) if (!result.ContainsKey(name)) throw new InvalidDataException("Incomplete embedded setup cache.");
        return result;
    }
    static string Hash(Stream source) { using var hash = SHA256.Create(); return BitConverter.ToString(hash.ComputeHash(source)).Replace("-", ""); }
    static void VerifyFile(string path, string hash)
    {
        ProtectedJournal.RejectReparsePath(path); ProtectedJournal.ValidateAcl(File.GetAccessControl(path));
        using var source = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (source.Length > 128 * 1024 * 1024 || Hash(source) != hash) throw new InvalidDataException("Protected setup cache changed.");
    }
    public void PrepareCache()
    {
        ProtectedJournal.CreateRoot(); DirectoryAt(Cache); DirectoryAt(Path.Combine(ProtectedJournal.Root, "payload"));
        foreach (var item in Index())
        {
            string path = Path.Combine(Cache, item.Key);
            if (!File.Exists(path))
            {
                using var source = typeof(ControllerStore).Assembly.GetManifestResourceStream("cache." + item.Key) ?? throw new InvalidDataException("Missing embedded recovery payload.");
                using var target = new FileStream(path, FileMode.CreateNew, FileSystemRights.Write, FileShare.None, 4096, FileOptions.WriteThrough, FileAcl());
                source.CopyTo(target); target.Flush(true);
            }
            VerifyFile(path, item.Value);
            if (Allowed.Take(4).Contains(item.Key))
            {
                string driver = Path.Combine(ProtectedJournal.Root, "payload", item.Key);
                if (!File.Exists(driver))
                {
                    using var source = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
                    using var target = new FileStream(driver, FileMode.CreateNew, FileSystemRights.Write, FileShare.None, 4096, FileOptions.WriteThrough, FileAcl()); source.CopyTo(target); target.Flush(true);
                }
                VerifyFile(driver, item.Value);
            }
        }
        Payload.Verify(Path.Combine(ProtectedJournal.Root, "payload"));
    }
    public void Verify(SetupRecord record)
    {
        ProtectedJournal.ValidateDirectory(Cache);
        var index = Index();
        bool currentPackage = string.IsNullOrEmpty(record.PackageHash) || record.PackageHash == index["HidHide.Unified.Preview.msi"];
        if (!currentPackage)
        {
            // Finishing an already-applied uninstall does not execute another MSI
            // or any cached application binary. A later setup may therefore
            // complete that removal using the transaction-owned MSI digest and
            // the product-wide immutable signed-driver payload. All earlier,
            // ambiguous, install, upgrade and legacy-migration phases remain
            // bound to the exact setup that created them.
            if (!CanFinalizeUninstallWithJournalPackage(record))
                throw new InvalidDataException("Journal belongs to another setup package.");
            VerifyFile(Path.Combine(Cache, "HidHide.Unified.Preview.msi"), record.PackageHash);
            Payload.Verify(Path.Combine(ProtectedJournal.Root, "payload"));
            return;
        }
        foreach (var item in index) VerifyFile(Path.Combine(Cache, item.Key), item.Value);
        if (record.PriorUnified != null) VerifyFile(Path.Combine(Cache, "PriorUnified.msi"), record.PriorUnified.PackageHash);
        foreach (var legacy in record.Legacy)
        {
            string name = legacy.Family == HidHide.Installer.ProductContract.UpstreamUpgradeCode ? "UpstreamRecovery.exe" : legacy.Version.StartsWith("99.", StringComparison.Ordinal) ? "Companion99Recovery.msi" : "Companion1Recovery.msi";
            if (!index.ContainsKey(name)) throw new InvalidDataException("Complete pinned recovery source is unavailable for " + legacy.Product.ToString("B") + ". No products were removed.");
            string expected = name == "UpstreamRecovery.exe" ? "F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6" : name == "Companion1Recovery.msi" ? "A9877ED39F5D36998302FBCB2BE94EC8F5C59C5C2990BC41B25A6A772B9431E6" : "EBC9EE898608C3265E59960A9F08EA13DDF4E327D89F33936F6E7ABBC43DE4B4";
            if (index[name] != expected) throw new InvalidDataException("Recovery media does not match original legacy file evidence.");
        }
        Payload.Verify(Path.Combine(ProtectedJournal.Root, "payload"));
    }
    public static bool CanFinalizeUninstallWithJournalPackage(SetupRecord record)
    {
        if (record == null) return false;
        if (record.Operation != Operation.Uninstall || record.Legacy == null || record.Legacy.Count != 0 || record.PriorUnified != null || record.MsiFailureReported ||
            record.PackageProduct == Guid.Empty || record.BeforeMsiProduct != record.PackageProduct ||
            !System.Text.RegularExpressions.Regex.IsMatch(record.PackageHash ?? "", "\\A[A-F0-9]{64}\\z")) return false;
        return record.Phase is SetupPhase.MsiApplied or SetupPhase.Restoring or SetupPhase.Complete ||
            record.Phase == SetupPhase.WaitingForReboot && record.ResumePhase == SetupPhase.MsiApplied;
    }
    public void PreparePackageRecord(SetupRecord record, IReadOnlyList<HidHide.Installer.InstalledProduct> products)
    {
        record.PackageProduct = HidHide.Installer.ProductContract.UnifiedProductCode(Version.Parse(record.Version));
        record.PackageHash = Index()["HidHide.Unified.Preview.msi"];
        if (record.Operation != Operation.Upgrade) return;
        var installed = products.Single();
        if (installed.Family != HidHide.Installer.ProductContract.MsiUpgradeCode || installed.Product != HidHide.Installer.ProductContract.UnifiedProductCode(installed.Version))
            throw new InvalidDataException("Installed unified product identity is not supported.");
        // LocalPackage may be a stripped Windows Installer database. It is not
        // accepted as the full recovery source. Retain our original compressed
        // MSI from the protected completed installation journal instead.
        ProtectedJournal.RejectReparsePath(installed.CachedPackage);
        using (var database = new FileStream(installed.CachedPackage, FileMode.Open, FileAccess.Read, FileShare.Read))
            if (database.Length == 0 || database.Length > 128 * 1024 * 1024) throw new InvalidDataException("Installed Windows Installer database is unavailable.");
        var candidates = Directory.EnumerateFiles(ProtectedJournal.Root, "*.setup.xml", SearchOption.TopDirectoryOnly).Take(4097).ToArray();
        if (candidates.Length > 4096) throw new InvalidDataException("Too many setup journals to resolve recovery source.");
        foreach (string path in candidates)
        {
            string name = Path.GetFileName(path);
            if (!Guid.TryParseExact(name.Substring(0, name.Length - ".setup.xml".Length), "D", out var sourceId) || sourceId == id) continue;
            var previousStore = new ControllerStore(sourceId);
            var previous = previousStore.Load();
            if (previous.Phase != SetupPhase.Complete || previous.Operation == Operation.Uninstall || previous.PackageProduct != installed.Product || string.IsNullOrEmpty(previous.PackageHash)) continue;
            string sourcePath = Path.Combine(previousStore.Cache, "HidHide.Unified.Preview.msi");
            VerifyFile(sourcePath, previous.PackageHash);
            string targetPath = Path.Combine(Cache, "PriorUnified.msi");
            if (!File.Exists(targetPath))
            {
                using var source = new FileStream(sourcePath, FileMode.Open, FileAccess.Read, FileShare.Read);
                using var target = new FileStream(targetPath, FileMode.CreateNew, FileSystemRights.Write, FileShare.None, 4096, FileOptions.WriteThrough, FileAcl());
                source.CopyTo(target); target.Flush(true);
            }
            VerifyFile(targetPath, previous.PackageHash);
            record.PriorUnified = new PriorUnifiedProduct { Product = installed.Product, Version = $"{installed.Version.Major}.{installed.Version.Minor}.{installed.Version.Build}.0", PackageHash = previous.PackageHash, SourceTransaction = sourceId };
            return;
        }
        throw new InvalidDataException("Complete protected recovery source for the installed unified version is unavailable; installation preserved.");
    }
    public void VerifyInstalledApplications()
    {
        var index = Index();
        foreach (string name in ApplicationFiles)
        {
            string path = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "HidHide", name);
            ProtectedJournal.RejectReparsePath(path);
            using var source = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            if (source.Length > 128 * 1024 * 1024 || Hash(source) != index[name]) throw new InvalidDataException("Installed application or runtime failed byte verification: " + name);
        }
    }
    public void ArchiveDriverRetry(SetupRecord record, TransactionRecord driver)
    {
        if (record.Id != id || driver.Id != id || record.MsiRetryCount < 0 || record.MsiRetryCount >= 32) throw new InvalidDataException("Invalid driver retry archive identity.");
        ProtectedJournal.ValidateDirectory(ProtectedJournal.Root);
        string path = Path.Combine(ProtectedJournal.Root, id.ToString("D") + ".retry-" + record.MsiRetryCount + ".driver.xml");
        using var bytes = new MemoryStream(); new DataContractSerializer(typeof(TransactionRecord)).WriteObject(bytes, driver);
        string expectedHash; bytes.Position = 0; expectedHash = Hash(bytes);
        if (!File.Exists(path))
        {
            bytes.Position = 0;
            using var target = new FileStream(path, FileMode.CreateNew, FileSystemRights.Write, FileShare.None, 4096, FileOptions.WriteThrough, FileAcl());
            bytes.CopyTo(target); target.Flush(true);
        }
        // A completed archive write followed by a crash can be accepted again;
        // partial or changed history cannot be overwritten or blessed.
        VerifyFile(path, expectedHash);
    }
    string PathFor(string suffix) => Path.Combine(ProtectedJournal.Root, id.ToString("D") + ".setup" + suffix);
    string RecoveryPath(string suffix) => Path.Combine(ProtectedJournal.Root, id.ToString("D") + ".legacy-recovery" + suffix);
    public LegacyRecoveryRecord LoadOrCreateRecovery(string boot)
    {
        ProtectedJournal.ValidateDirectory(ProtectedJournal.Root);
        if (File.Exists(RecoveryPath(".pending"))) throw new InvalidDataException("Interrupted legacy recovery journal write requires inspection.");
        string path = RecoveryPath(".xml");
        if (!File.Exists(path))
        {
            var created = new LegacyRecoveryRecord { Transaction = id, Boot = boot };
            SaveRecovery(created); return created;
        }
        ProtectedJournal.RejectReparsePath(path); ProtectedJournal.ValidateAcl(File.GetAccessControl(path));
        using var source = File.OpenRead(path);
        if (source.Length > 65536) throw new InvalidDataException("Oversized legacy recovery journal.");
        using var reader = XmlReader.Create(source, new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null, MaxCharactersInDocument = 65536 });
        var record = (LegacyRecoveryRecord)new DataContractSerializer(typeof(LegacyRecoveryRecord)).ReadObject(reader)!;
        LegacyRecovery.Validate(record, id); return record;
    }
    public void SaveRecovery(LegacyRecoveryRecord record)
    {
        LegacyRecovery.Validate(record, id); ProtectedJournal.ValidateDirectory(ProtectedJournal.Root);
        string path = RecoveryPath(".xml");
        if (File.Exists(path)) { ProtectedJournal.RejectReparsePath(path); ProtectedJournal.ValidateAcl(File.GetAccessControl(path)); }
        using var data = new MemoryStream(); new DataContractSerializer(typeof(LegacyRecoveryRecord)).WriteObject(data, record);
        if (data.Length > 65536) throw new InvalidDataException("Oversized legacy recovery journal.");
        using (var target = new FileStream(RecoveryPath(".pending"), FileMode.CreateNew, FileSystemRights.Write, FileShare.None, 4096, FileOptions.WriteThrough, FileAcl())) { data.Position = 0; data.CopyTo(target); target.Flush(true); }
        if (!MoveFileExW(RecoveryPath(".pending"), path, 1 | 8)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
    }
    public void Save(SetupRecord record)
    {
        SetupTransaction.Validate(record); if (record.Id != id) throw new InvalidDataException("Setup transaction mismatch.");
        ProtectedJournal.ValidateDirectory(ProtectedJournal.Root);
        string path = PathFor(".xml"); if (File.Exists(path)) { ProtectedJournal.RejectReparsePath(path); ProtectedJournal.ValidateAcl(File.GetAccessControl(path)); }
        using var data = new MemoryStream(); new DataContractSerializer(typeof(SetupRecord)).WriteObject(data, record);
        if (data.Length > 2 * 1024 * 1024) throw new InvalidDataException("Oversized setup journal.");
        using (var target = new FileStream(PathFor(".pending"), FileMode.CreateNew, FileSystemRights.Write, FileShare.None, 4096, FileOptions.WriteThrough, FileAcl())) { data.Position = 0; data.CopyTo(target); target.Flush(true); }
        if (!MoveFileExW(PathFor(".pending"), path, 1 | 8)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
    }
    public SetupRecord Load()
    {
        ProtectedJournal.ValidateDirectory(ProtectedJournal.Root);
        if (File.Exists(PathFor(".pending"))) throw new InvalidDataException("Interrupted setup journal write requires recovery.");
        string path = PathFor(".xml"); ProtectedJournal.RejectReparsePath(path); ProtectedJournal.ValidateAcl(File.GetAccessControl(path));
        using var source = File.OpenRead(path); if (source.Length > 2 * 1024 * 1024) throw new InvalidDataException("Oversized setup journal.");
        using var reader = XmlReader.Create(source, new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null, MaxCharactersInDocument = 2 * 1024 * 1024 });
        var record = (SetupRecord)new DataContractSerializer(typeof(SetupRecord)).ReadObject(reader)!;
        SetupTransaction.Validate(record); if (record.Id != id) throw new InvalidDataException("Setup transaction mismatch."); return record;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)] static extern bool MoveFileExW(string source, string target, int flags);
}
