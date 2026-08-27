using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace PrivacyPic;

internal sealed record LicenseInfo(string LicenseId, string OrderId, string Plan, DateTime IssuedUtc, DateTime? ExpiresUtc);

internal static class LicenseService
{
    private const string PublicKeyPem = @"-----BEGIN PUBLIC KEY-----
MIIBojANBgkqhkiG9w0BAQEFAAOCAY8AMIIBigKCAYEAi0cj25XO3viD93ssOA/U
iMR1UGlF3S3FP+tu9LQSiVfWDng+qCF7UTajO85LT8vrlai9pDFSg1w3mC3Uf89j
Wvrw37xGdVzZ4c2AklEKEw484fgZYX0wnfz06e0jhrJP2FSRauNtGIehJ7aOullS
MgeRNnQZpeFY5LbseivIB1SnksydxnaLaycYX8bB1ehRC4DzEuGGOKAuxSUdJawW
tmpq9hsGX+NGIx+8aYxmaVfRMp9Fa4FU1lJfOZFZle/3FBRZl3CpTAfwOdDMeTq/
Mk6bXKzPs5VfdJmabUTqRGfVy4PnI+B/8TFqfdqXBXrkQkGO1id+jjrLdpcJEteJ
oJVrxOk9Dtkc6bp7FNdUL8bPiNiz8uIZ65RLK+gO7WB4JqjTyvByhTOF+0C2GyP7
BB5tqLioC26lWWtnBz5MBdt1kVHapIG8ss4oaTsO1EFY0NC7JJreWx/nOWx+a06c
xjyI9vR72AsvxH+yQVjQVsXywVEH5ujcYEDKQ7jymsH9AgMBAAE=
-----END PUBLIC KEY-----
";
    private static string LicensePath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PrivacyPic", "license.lic");

    internal static LicenseInfo? LoadInstalled()
    {
        if (!File.Exists(LicensePath)) return null;
        try { return Validate(File.ReadAllText(LicensePath)); } catch { return null; }
    }

    internal static LicenseInfo Install(string sourcePath)
    {
        var text = File.ReadAllText(sourcePath);
        var info = Validate(text) ?? throw new InvalidDataException("このライセンスは無効です。");
        Directory.CreateDirectory(Path.GetDirectoryName(LicensePath)!);
        File.WriteAllText(LicensePath, text);
        return info;
    }

    internal static LicenseInfo? Validate(string json)
    {
        var doc = JsonSerializer.Deserialize<LicenseEnvelope>(json, JsonOptions);
        if (doc is null || string.IsNullOrWhiteSpace(doc.Payload) || string.IsNullOrWhiteSpace(doc.Signature)) return null;
        byte[] payload;
        byte[] signature;
        try { payload = Convert.FromBase64String(doc.Payload); signature = Convert.FromBase64String(doc.Signature); } catch { return null; }

        using var rsa = RSA.Create();
        rsa.ImportFromPem(PublicKeyPem);
        if (!rsa.VerifyData(payload, signature, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1)) return null;

        var raw = Encoding.UTF8.GetString(payload);
        var parts = raw.Split('|');
        if (parts.Length != 6 || parts[0] != "PP1" || parts[3] != "PRO") return null;
        if (!DateTime.TryParse(parts[4], null, System.Globalization.DateTimeStyles.RoundtripKind, out var issued)) return null;

        DateTime? expires = null;
        if (parts[5] != "NEVER")
        {
            if (!DateTime.TryParse(parts[5], null, System.Globalization.DateTimeStyles.RoundtripKind, out var exp)) return null;
            expires = exp.ToUniversalTime();
            if (expires <= DateTime.UtcNow) return null;
        }

        return new LicenseInfo(parts[1], parts[2], parts[3], issued.ToUniversalTime(), expires);
    }

    private sealed class LicenseEnvelope
    {
        public string Payload { get; set; } = "";
        public string Signature { get; set; } = "";
    }

    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true };
}
