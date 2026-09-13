using HidHide.Setup;

internal static class PublicDiagnosticTests
{
    public static void Run()
    {
        int checks = 0;
        void Check(bool value) { if (!value) throw new Exception("Public diagnostic test failed."); checks++; }
        foreach (string code in new[] { "product-ownership", "driver-ownership", "legacy-files", "other-user", "restart-required", "failure" })
        {
            string encoded = PublicDiagnostic.Encode(code, "cache", new IOException("private baseline: secret-path"));
            Check(!encoded.Contains("secret") && encoded.Length <= 160);
            using var bytes = new MemoryStream(); var wire = new SessionWire(bytes); wire.Write(encoded); bytes.Position = 0;
            string message = ""; try { wire.Read(); } catch (InvalidOperationException error) { message = error.Message; }
            Check(message.Contains(code + "/cache/IOException/") && message.Contains(PublicDiagnostic.Message(code)) && !message.Contains("secret"));
        }
        foreach (string value in new[] { "error:public:injected:cache:IOException:80000000", "error:public:failure:secret-path:IOException:80000000", "error:public:failure:cache:PrivateSecretType:80000000", "error:public:failure:cache:IOException:80000000\nsecret", "error:" + new string('x', 200), "error:controller:cache:IOException:8000000G" })
        {
            var error = PublicDiagnostic.Decode(value);
            Check(error is InvalidDataException && !error.Message.Contains("secret") && !error.Message.Contains("injected"));
        }
        Check(PublicDiagnostic.Decode("apply:install") == null);
        Check(PublicDiagnostic.Encode("failure", "private-path", new Exception("secret")).Contains(":unknown:Exception:"));
        Console.WriteLine(checks + " bounded public diagnostic checks passed.");
    }
}
