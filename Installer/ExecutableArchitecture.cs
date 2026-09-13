namespace HidHide.Installer;

// Bounded PE header validation, without loading or executing staged code.
// Machine constants/header layout: Microsoft PE/COFF specification.
public static class ExecutableArchitecture
{
    public static void Require(string path, bool arm64)
    {
        using var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        Require(input, arm64, Path.GetFileName(path));
    }
    public static void Require(Stream input, bool arm64, string name)
    {
        InvalidDataException Bad(string reason) => new($"Staged executable {name}: {reason}.");
        if (!input.CanRead || !input.CanSeek || input.Length < 64 || input.Length > 256L * 1024 * 1024)
            throw Bad("invalid or oversized PE image");
        using var reader = new BinaryReader(input, System.Text.Encoding.UTF8, true);
        input.Position = 0;
        if (reader.ReadUInt16() != 0x5A4D) throw Bad("missing DOS signature");
        input.Position = 0x3C;
        uint pe = reader.ReadUInt32();
        if (pe < 64 || pe > input.Length - 24) throw Bad("invalid PE header offset");
        input.Position = pe;
        if (reader.ReadUInt32() != 0x00004550) throw Bad("missing PE signature");
        ushort machine = reader.ReadUInt16(), sections = reader.ReadUInt16();
        if (machine != (arm64 ? 0xAA64 : 0x8664))
            throw Bad($"expected {(arm64 ? "ARM64" : "x64")} Machine, found 0x{machine:X4}");
        input.Position = pe + 20;
        ushort optionalSize = reader.ReadUInt16(), characteristics = reader.ReadUInt16();
        long optional = pe + 24L, sectionTable = optional + optionalSize;
        if ((characteristics & 2) == 0 || (characteristics & 0x2000) != 0 || sections < 1 || sections > 96 ||
            optionalSize < 112 || sectionTable + 40L * sections > input.Length)
            throw Bad("invalid or truncated executable headers");
        if (reader.ReadUInt16() != 0x20B) throw Bad("expected a PE32+ optional header");
        input.Position = optional + 60;
        uint headersSize = reader.ReadUInt32();
        if (headersSize < sectionTable + 40L * sections || headersSize > input.Length)
            throw Bad("invalid PE header size");
        input.Position = optional + 108;
        uint directories = reader.ReadUInt32();
        if (directories > 16 || 112L + directories * 8L > optionalSize)
            throw Bad("truncated or invalid data-directory table");
        for (int i = 0; i < sections; i++)
        {
            input.Position = sectionTable + i * 40L + 16;
            uint size = reader.ReadUInt32(), offset = reader.ReadUInt32();
            if (size != 0 && (offset < headersSize || (long)offset + size > input.Length))
                throw Bad("truncated or invalid section data");
        }
    }
}
