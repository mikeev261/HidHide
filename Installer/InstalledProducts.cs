using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace HidHide.Installer;

public sealed class InstalledProduct
{
    public Guid Family { get; set; }
    public Guid Product { get; set; }
    public Version Version { get; set; } = new Version(0, 0, 0);
    public string CachedPackage { get; set; } = "";
    public ProductContract.Operation Operation { get; set; }
}

public static class InstalledProducts
{
    // These APIs read Windows Installer registration. They do not perform the
    // consistency checks triggered by Win32_Product enumeration.
    [DllImport("msi.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    static extern uint MsiEnumRelatedProductsW(string upgrade, uint reserved, uint index, StringBuilder product);
    [DllImport("msi.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    static extern uint MsiGetProductInfoW(string product, string property, StringBuilder value, ref uint count);

    static string Read(string product, string property)
    {
        uint capacity = 256;
        var value = new StringBuilder((int)capacity);
        var result = MsiGetProductInfoW(product, property, value, ref capacity);
        if (result == 234)
        {
            if (capacity > 32767) throw new InvalidOperationException("Oversized Windows Installer registration value.");
            capacity++;
            value = new StringBuilder((int)capacity);
            result = MsiGetProductInfoW(product, property, value, ref capacity);
        }
        if (result != 0) throw new Win32Exception((int)result, "Cannot read installed product " + product + " / " + property);
        return value.ToString();
    }

    public static IReadOnlyList<InstalledProduct> Detect(Version target)
    {
        var products = new List<InstalledProduct>();
        foreach (var family in new[] { ProductContract.MsiUpgradeCode, ProductContract.UpstreamUpgradeCode, ProductContract.CompanionUpgradeCode })
        {
            for (uint index = 0; ; index++)
            {
                if (index >= 128) throw new InvalidOperationException("Too many related MSI registrations; refusing ambiguous maintenance.");
                var code = new StringBuilder(39);
                var result = MsiEnumRelatedProductsW(family.ToString("B"), 0, index, code);
                if (result == 259) break;
                if (result != 0) throw new Win32Exception((int)result, "Cannot enumerate related MSI products.");
                var product = Guid.Parse(code.ToString());
                var version = Version.Parse(Read(code.ToString(), "VersionString"));
                products.Add(new InstalledProduct {
                    Family = family, Product = product, Version = version,
                    CachedPackage = Read(code.ToString(), "LocalPackage"),
                    Operation = ProductContract.Select(family, product, version, target)
                });
            }
        }
        return products;
    }
}
