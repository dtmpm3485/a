using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace PrivacyPicLicenseGenerator;

internal static class LicenseSigner
{
    // PRIVATE: ビルド時に実鍵へ置換される。発行exeは購入者へ配布しないこと。
    private const string PrivateKeyPem = @"__PRIVATE_KEY_PEM__";

    internal static string Create(string orderId, DateTime? expiresUtc)
    {
        orderId = Sanitize(orderId);

        if (string.IsNullOrWhiteSpace(orderId))
            throw new ArgumentException("注文IDを入力してください。");

        var licenseId = Guid.NewGuid().ToString("N").ToUpperInvariant();
        var issued = DateTime.UtcNow.ToString("O");
        var expires = expiresUtc is null
            ? "NEVER"
            : expiresUtc.Value.ToUniversalTime().ToString("O");

        var payloadText = $"PP1|{licenseId}|{orderId}|PRO|{issued}|{expires}";
        var payload = Encoding.UTF8.GetBytes(payloadText);

        using var rsa = RSA.Create();
        rsa.ImportFromPem(PrivateKeyPem);

        var sig = rsa.SignData(
            payload,
            HashAlgorithmName.SHA256,
            RSASignaturePadding.Pkcs1);

        var envelope = new
        {
            payload = Convert.ToBase64String(payload),
            signature = Convert.ToBase64String(sig)
        };

        return JsonSerializer.Serialize(
            envelope,
            new JsonSerializerOptions { WriteIndented = true });
    }

    private static string Sanitize(string s) =>
        (s ?? "")
            .Replace("|", "-")
            .Replace("\r", " ")
            .Replace("\n", " ")
            .Trim();
}
