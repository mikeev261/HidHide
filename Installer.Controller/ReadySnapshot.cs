using System.Security.Principal;
using System.Text;

namespace HidHide.Setup;

// Exact counterpart of Shared/Configuration.h, including its UTF-16-as-u32 wire format.
// Parsing never logs configuration and cannot select privileged paths or registry keys.
public sealed class ReadySnapshot
{
    public const int Limit = 1024 * 1024;
    public string Sid { get; private set; } = "";
    public bool DriverPresent { get; private set; }
    public bool BaselineAvailable { get; private set; }
    public bool Active { get; private set; }
    public bool Inverse { get; private set; }
    public string[] Blacklist { get; private set; } = Array.Empty<string>();
    public string[] Whitelist { get; private set; } = Array.Empty<string>();
    public Dictionary<string, string[]> Profiles { get; private set; } = new(StringComparer.Ordinal);
    public static ReadySnapshot Parse(string line, string authenticatedSid)
    {
        if (!line.StartsWith("READY ", StringComparison.Ordinal) || line.Length > 6 + Limit * 2 || (line.Length - 6) % 2 != 0)
            throw new InvalidDataException("Invalid maintenance response.");
        var bytes = new byte[(line.Length - 6) / 2];
        int Nibble(char c) => c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : throw new InvalidDataException("Invalid maintenance encoding.");
        for (int i = 0; i < bytes.Length; i++) bytes[i] = (byte)(Nibble(line[6 + i * 2]) * 16 + Nibble(line[7 + i * 2]));
        int offset = 0;
        uint Number()
        {
            if (bytes.Length - offset < 4) throw new InvalidDataException("Truncated maintenance response.");
            uint n = (uint)bytes[offset] | (uint)bytes[offset + 1] << 8 | (uint)bytes[offset + 2] << 16 | (uint)bytes[offset + 3] << 24; offset += 4; return n;
        }
        bool Flag() { uint n = Number(); return n <= 1 ? n == 1 : throw new InvalidDataException("Invalid maintenance flag."); }
        uint Count() { uint n = Number(); return n <= 4096 ? n : throw new InvalidDataException("Too many maintenance entries."); }
        string String()
        {
            uint n = Number(); if (n > 32767 || n > (bytes.Length - offset) / 4) throw new InvalidDataException("Invalid maintenance string.");
            var text = new StringBuilder((int)n);
            for (uint i = 0; i < n; i++) { uint c = Number(); if (c == 0 || c > 65535) throw new InvalidDataException("Invalid maintenance character."); text.Append((char)c); }
            return text.ToString();
        }
        string[] Strings()
        {
            var values = new HashSet<string>(StringComparer.Ordinal); uint n = Count();
            for (uint i = 0; i < n; i++) if (!values.Add(String())) throw new InvalidDataException("Duplicate maintenance entry.");
            return values.OrderBy(x => x, StringComparer.Ordinal).ToArray();
        }
        if (Number() != 2) throw new InvalidDataException("Unsupported maintenance protocol.");
        var result = new ReadySnapshot { Sid = String() };
        if (!new SecurityIdentifier(result.Sid).Equals(new SecurityIdentifier(authenticatedSid))) throw new UnauthorizedAccessException("Maintenance owner differs from authenticated initiating user.");
        result.DriverPresent = Flag(); result.BaselineAvailable = Flag(); result.Active = Flag(); result.Inverse = Flag();
        if (result.DriverPresent && !result.BaselineAvailable) throw new InvalidDataException("Live driver baseline missing.");
        result.Blacklist = Strings(); result.Whitelist = Strings(); uint profiles = Count();
        if (!result.BaselineAvailable && (result.Active || result.Inverse || result.Blacklist.Length != 0 || result.Whitelist.Length != 0)) throw new InvalidDataException("Unconfirmed baseline values are not accepted.");
        for (uint i = 0; i < profiles; i++) { string path = String(); var devices = Strings(); if (result.Profiles.ContainsKey(path)) throw new InvalidDataException("Duplicate maintenance profile."); result.Profiles.Add(path, devices); }
        if (offset != bytes.Length) throw new InvalidDataException("Trailing maintenance response.");
        return result;
    }
    public static byte[] MultiString(IEnumerable<string> values)
    {
        var all = values.ToArray();
        string text = string.Join("\0", all) + (all.Length == 0 ? "\0" : "\0\0");
        var bytes = new byte[text.Length * 2];
        for (int i = 0; i < text.Length; i++) { bytes[i * 2] = (byte)text[i]; bytes[i * 2 + 1] = (byte)(text[i] >> 8); }
        return bytes;
    }
}
