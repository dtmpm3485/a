namespace PrivacyPic;

internal static class Program
{
    [STAThread]
    static void Main(string[] args)
    {
        ApplicationConfiguration.Initialize();

        if (!args.Contains("--pp-launch-v2", StringComparer.Ordinal))
        {
            MessageBox.Show(
                "PrivacyPic.exe から起動してください。",
                "PrivacyPic",
                MessageBoxButtons.OK,
                MessageBoxIcon.Warning);
            return;
        }

        try
        {
            var guard = NativeGuard.Check();
            if (guard != 0)
            {
                MessageBox.Show(
                    $"整合性チェックに失敗しました。\nGuard code: {guard}",
                    "PrivacyPic",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
                return;
            }
        }
        catch
        {
            MessageBox.Show(
                "privacypic_guard.dll を読み込めませんでした。",
                "PrivacyPic",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return;
        }

        Application.Run(new MainForm());
    }
}
