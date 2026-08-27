namespace PrivacyPicLicenseGenerator;

public sealed class GeneratorForm : Form
{
    private readonly TextBox orderId = new();
    private readonly RadioButton permanent = new();
    private readonly RadioButton expires = new();
    private readonly DateTimePicker expiry = new();
    private readonly Button generate = new();

    public GeneratorForm()
    {
        Text = "PrivacyPic License Generator — PRIVATE";
        Width = 600;
        Height = 390;
        StartPosition = FormStartPosition.CenterScreen;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        Font = new Font("Segoe UI", 10f);
        BackColor = Color.White;

        var title = new Label { Text = "PrivacyPic Pro ライセンス発行", Font = new Font("Segoe UI Semibold", 18f), AutoSize = true, Location = new Point(24, 20) };
        var warning = new Label { Text = "⚠ このexeは署名用の秘密鍵を含みます。購入者・第三者へ絶対に配布しないでください。", AutoSize = false, Width = 530, Height = 44, ForeColor = Color.DarkRed, Location = new Point(26, 64) };
        var l1 = new Label { Text = "注文ID / 管理番号", AutoSize = true, Location = new Point(26, 124) };

        orderId.Location = new Point(26, 150);
        orderId.Width = 530;
        orderId.PlaceholderText = "例: X-20260828-001";

        permanent.Text = "永久ライセンス";
        permanent.Location = new Point(26, 200);
        permanent.AutoSize = true;
        permanent.Checked = true;

        expires.Text = "有効期限を設定";
        expires.Location = new Point(180, 200);
        expires.AutoSize = true;

        expiry.Location = new Point(320, 197);
        expiry.Width = 235;
        expiry.Format = DateTimePickerFormat.Custom;
        expiry.CustomFormat = "yyyy/MM/dd HH:mm";
        expiry.Value = DateTime.Now.AddYears(1);
        expiry.Enabled = false;

        permanent.CheckedChanged += (_, _) => expiry.Enabled = expires.Checked;
        expires.CheckedChanged += (_, _) => expiry.Enabled = expires.Checked;

        generate.Text = "license.lic を発行";
        generate.Width = 220;
        generate.Height = 44;
        generate.Location = new Point(26, 252);
        generate.Click += (_, _) => Generate();

        var foot = new Label
        {
            Text = "発行した .lic を購入者へ送信し、PrivacyPic の「Proライセンス読込」から読み込んでもらいます。",
            AutoSize = false,
            Width = 530,
            Height = 42,
            Location = new Point(26, 310),
            ForeColor = Color.DimGray
        };

        Controls.AddRange([title, warning, l1, orderId, permanent, expires, expiry, generate, foot]);
    }

    private void Generate()
    {
        try
        {
            DateTime? exp = expires.Checked ? expiry.Value.ToUniversalTime() : null;
            var json = LicenseSigner.Create(orderId.Text, exp);

            using var d = new SaveFileDialog
            {
                Filter = "PrivacyPicライセンス|*.lic",
                FileName = $"PrivacyPic_{Safe(orderId.Text)}.lic",
                Title = "ライセンスを保存"
            };

            if (d.ShowDialog(this) != DialogResult.OK) return;
            File.WriteAllText(d.FileName, json);

            MessageBox.Show(this, $"発行しました。\n\n{d.FileName}", "PrivacyPic", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "発行エラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private static string Safe(string s)
    {
        var x = string.Concat((s ?? "license").Select(ch => Path.GetInvalidFileNameChars().Contains(ch) ? '_' : ch));
        return string.IsNullOrWhiteSpace(x) ? "license" : x;
    }
}
