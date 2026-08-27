using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace PrivacyPicLicenseGenerator;

internal static class LicenseSigner
{
    // PRIVATE: この秘密鍵を含む発行ツールは購入者へ配布しないこと。
    private const string PrivateKeyPem = @"-----BEGIN PRIVATE KEY-----
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
Uh5u1cS2/jEO2NZ/fQTF6w==
-----END PRIVATE KEY-----
";

    internal static string Create(string orderId, DateTime? expiresUtc)
    {
        orderId = Sanitize(orderId);
        if (string.IsNullOrWhiteSpace(orderId))
            throw new ArgumentException("注文IDを入力してください。");

        var licenseId = Guid.NewGuid().ToString("N").ToUpperInvariant();
        var issued = DateTime.UtcNow.ToString("O");
        var expires = expiresUtc is null ? "NEVER" : expiresUtc.Value.ToUniversalTime().ToString("O");
        var payloadText = $"PP1|{licenseId}|{orderId}|PRO|{issued}|{expires}";
        var payload = Encoding.UTF8.GetBytes(payloadText);

        using var rsa = RSA.Create();
        rsa.ImportFromPem(PrivateKeyPem);
        var sig = rsa.SignData(payload, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);

        var envelope = new
        {
            payload = Convert.ToBase64String(payload),
            signature = Convert.ToBase64String(sig)
        };
        return JsonSerializer.Serialize(envelope, new JsonSerializerOptions { WriteIndented = true });
    }

    private static string Sanitize(string s) =>
        (s ?? "").Replace("|", "-").Replace("\r", " ").Replace("\n", " ").Trim();
}
