using System.ComponentModel;

namespace PrivacyPic;

public sealed class MainForm : Form
{
    private readonly ListView files = new();
    private readonly Label planLabel = new();
    private readonly Label hintLabel = new();
    private readonly Button addFiles = new();
    private readonly Button addFolder = new();
    private readonly Button analyze = new();
    private readonly Button clean = new();
    private readonly Button license = new();
    private readonly ProgressBar progress = new();
    private const int FreeBatchLimit = 5;

    public MainForm()
    {
        Text = "PrivacyPic — 写真の個人情報クリーナー";
        Width = 980; Height = 650; MinimumSize = new Size(820, 520);
        StartPosition = FormStartPosition.CenterScreen;
        AllowDrop = true;
        Font = new Font("Segoe UI", 10f);
        BackColor = Color.FromArgb(248, 249, 251);

        var header = new Panel { Dock = DockStyle.Top, Height = 86, Padding = new Padding(18, 14, 18, 10), BackColor = Color.White };
        var title = new Label { Text = "PrivacyPic", AutoSize = true, Font = new Font("Segoe UI Semibold", 20f), Location = new Point(18, 12) };
        var sub = new Label { Text = "SNS投稿前に、写真に残ったGPS・撮影情報・端末情報などを確認して削除", AutoSize = true, ForeColor = Color.DimGray, Location = new Point(20, 52) };
        planLabel.AutoSize = true; planLabel.Font = new Font("Segoe UI Semibold", 10f); planLabel.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        header.Controls.AddRange([title, sub, planLabel]);
        header.Resize += (_, _) => planLabel.Location = new Point(header.ClientSize.Width - planLabel.PreferredWidth - 20, 18);

        var toolbar = new FlowLayoutPanel { Dock = DockStyle.Top, Height = 58, Padding = new Padding(14, 10, 10, 8), BackColor = Color.FromArgb(242, 244, 247) };
        SetupButton(addFiles, "画像を追加"); SetupButton(addFolder, "フォルダ追加 (Pro)"); SetupButton(analyze, "解析"); SetupButton(clean, "安全化して保存"); SetupButton(license, "Proライセンス読込");
        toolbar.Controls.AddRange([addFiles, addFolder, analyze, clean, license]);

        files.Dock = DockStyle.Fill; files.View = View.Details; files.FullRowSelect = true; files.GridLines = true; files.AllowDrop = true;
        files.Columns.Add("ファイル", 360); files.Columns.Add("状態", 120); files.Columns.Add("検出された情報", 380);

        var footer = new Panel { Dock = DockStyle.Bottom, Height = 72, Padding = new Padding(16, 9, 16, 9), BackColor = Color.White };
        progress.Dock = DockStyle.Top; progress.Height = 18;
        hintLabel.Dock = DockStyle.Bottom; hintLabel.Height = 32; hintLabel.ForeColor = Color.DimGray;
        footer.Controls.Add(progress); footer.Controls.Add(hintLabel);

        Controls.Add(files); Controls.Add(footer); Controls.Add(toolbar); Controls.Add(header);

        addFiles.Click += (_, _) => AddFilesDialog();
        addFolder.Click += (_, _) => AddFolderDialog();
        analyze.Click += async (_, _) => await AnalyzeAsync();
        clean.Click += async (_, _) => await CleanAsync();
        license.Click += (_, _) => ImportLicense();
        DragEnter += OnDragEnter; DragDrop += OnDragDrop; files.DragEnter += OnDragEnter; files.DragDrop += OnDragDrop;

        _ = NativeCore.LicenseStatus(); // 起動時に10段階検証を実施
        RefreshPlan();
    }

    private static void SetupButton(Button b, string text)
    {
        b.Text = text; b.AutoSize = true; b.Height = 34; b.Padding = new Padding(8, 2, 8, 2); b.FlatStyle = FlatStyle.System;
    }

    private bool IsPro => NativeCore.LicenseStatus();

    private void RefreshPlan()
    {
        if (IsPro)
        {
            planLabel.Text = "PRO ✓"; planLabel.ForeColor = Color.SeaGreen;
            addFolder.Enabled = true;
            hintLabel.Text = $"Pro有効 — 10段階検証 {NativeCore.SecurityStageCount()}/10 / 端末ID: {NativeCore.GetDeviceId()}";
        }
        else
        {
            planLabel.Text = "FREE"; planLabel.ForeColor = Color.DarkOrange;
            addFolder.Enabled = false;
            hintLabel.Text = $"Free版は1回 {FreeBatchLimit} 枚まで。端末ID: {NativeCore.GetDeviceId()}  / 購入時にこのIDを送ってください。";
        }
    }

    private static bool Supported(string p)
    {
        var e = Path.GetExtension(p).ToLowerInvariant();
        return e is ".jpg" or ".jpeg" or ".png" or ".webp";
    }

    private void AddPaths(IEnumerable<string> paths)
    {
        var existing = files.Items.Cast<ListViewItem>().Select(x => (string)x.Tag!).ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (var path in paths.Where(File.Exists).Where(Supported))
        {
            if (!existing.Add(path)) continue;
            var item = new ListViewItem(Path.GetFileName(path)) { Tag = path };
            item.SubItems.Add("未解析"); item.SubItems.Add(""); files.Items.Add(item);
        }

        if (!IsPro && files.Items.Count > FreeBatchLimit)
            hintLabel.Text = $"Free版では処理時に先頭 {FreeBatchLimit} 枚までが対象です。Proで制限解除できます。";
    }

    private void AddFilesDialog()
    {
        using var d = new OpenFileDialog { Filter = "画像|*.jpg;*.jpeg;*.png;*.webp", Multiselect = true, Title = "画像を選択" };
        if (d.ShowDialog(this) == DialogResult.OK) AddPaths(d.FileNames);
    }

    private void AddFolderDialog()
    {
        if (!IsPro) return;
        using var d = new FolderBrowserDialog { Description = "画像フォルダを選択" };
        if (d.ShowDialog(this) == DialogResult.OK)
            AddPaths(Directory.EnumerateFiles(d.SelectedPath, "*", SearchOption.AllDirectories));
    }

    private void OnDragEnter(object? sender, DragEventArgs e)
    {
        if (e.Data?.GetDataPresent(DataFormats.FileDrop) == true) e.Effect = DragDropEffects.Copy;
    }

    private void OnDragDrop(object? sender, DragEventArgs e)
    {
        if (e.Data?.GetData(DataFormats.FileDrop) is not string[] dropped) return;
        var gathered = new List<string>();

        foreach (var p in dropped)
        {
            if (File.Exists(p)) gathered.Add(p);
            else if (Directory.Exists(p) && IsPro) gathered.AddRange(Directory.EnumerateFiles(p, "*", SearchOption.AllDirectories));
        }

        AddPaths(gathered);
    }

    private async Task AnalyzeAsync()
    {
        var targets = GetTargets();
        if (targets.Count == 0) return;

        SetBusy(true, targets.Count);
        await Task.Run(() =>
        {
            for (int i = 0; i < targets.Count; i++)
            {
                var item = targets[i];
                try
                {
                    var flags = NativeCore.Scan((string)item.Tag!);
                    var details = Describe(flags);
                    BeginInvoke((Action)(() =>
                    {
                        item.SubItems[1].Text = flags == 0 ? "検出なし" : "情報あり";
                        item.SubItems[2].Text = details;
                        progress.Value = Math.Min(progress.Maximum, i + 1);
                    }));
                }
                catch (DllNotFoundException)
                {
                    BeginInvoke((Action)(() => item.SubItems[1].Text = "Core DLLなし"));
                }
                catch
                {
                    BeginInvoke((Action)(() => item.SubItems[1].Text = "解析失敗"));
                }
            }
        });

        SetBusy(false, targets.Count);
    }

    private async Task CleanAsync()
    {
        var targets = GetTargets();
        if (targets.Count == 0) return;

        var ok = MessageBox.Show(
            this,
            $"{targets.Count}枚を安全化します。元画像は変更せず、同じ場所の PrivacyPic_Safe フォルダへコピーを作ります。",
            "PrivacyPic",
            MessageBoxButtons.OKCancel,
            MessageBoxIcon.Information);

        if (ok != DialogResult.OK) return;

        SetBusy(true, targets.Count);
        int success = 0;

        await Task.Run(() =>
        {
            for (int i = 0; i < targets.Count; i++)
            {
                var item = targets[i];
                var input = (string)item.Tag!;
                var dir = Path.Combine(Path.GetDirectoryName(input)!, "PrivacyPic_Safe");
                var output = UniquePath(Path.Combine(dir, Path.GetFileName(input)));

                int rc;
                try { rc = NativeCore.Sanitize(input, output); }
                catch { rc = -999; }

                if (rc == 0) success++;

                BeginInvoke((Action)(() =>
                {
                    item.SubItems[1].Text = rc == 0 ? "安全化済み" : $"失敗 ({rc})";
                    if (rc == 0) item.SubItems[2].Text = "メタデータを削除したコピーを保存";
                    progress.Value = Math.Min(progress.Maximum, i + 1);
                }));
            }
        });

        SetBusy(false, targets.Count);
        MessageBox.Show(this, $"完了: {success}/{targets.Count} 枚\n元画像は変更していません。", "PrivacyPic", MessageBoxButtons.OK, MessageBoxIcon.Information);
    }

    private List<ListViewItem> GetTargets()
    {
        var all = files.Items.Cast<ListViewItem>().ToList();
        if (!IsPro && all.Count > FreeBatchLimit) return all.Take(FreeBatchLimit).ToList();
        return all;
    }

    private void SetBusy(bool busy, int count)
    {
        addFiles.Enabled = !busy;
        addFolder.Enabled = !busy && IsPro;
        analyze.Enabled = !busy;
        clean.Enabled = !busy;
        license.Enabled = !busy;

        progress.Minimum = 0;
        progress.Maximum = Math.Max(1, count);
        if (busy) progress.Value = 0;
    }

    private static string Describe(uint f)
    {
        if (f == 0) return "削除対象の主要メタデータは検出されませんでした";

        var x = new List<string>();
        if ((f & NativeCore.Gps) != 0) x.Add("GPS位置情報");
        if ((f & NativeCore.DateTime) != 0) x.Add("撮影日時");
        if ((f & NativeCore.Device) != 0) x.Add("端末/カメラ情報");
        if ((f & NativeCore.Software) != 0) x.Add("ソフトウェア/コメント");
        if ((f & NativeCore.XmpIptc) != 0) x.Add("XMP/IPTC/テキスト情報");
        if (x.Count == 0 && (f & NativeCore.Metadata) != 0) x.Add("画像メタデータ");
        return string.Join(" / ", x);
    }

    private static string UniquePath(string p)
    {
        if (!File.Exists(p)) return p;

        var dir = Path.GetDirectoryName(p)!;
        var name = Path.GetFileNameWithoutExtension(p);
        var ext = Path.GetExtension(p);

        for (int i = 1; i < 10000; i++)
        {
            var n = Path.Combine(dir, $"{name}_{i}{ext}");
            if (!File.Exists(n)) return n;
        }

        return Path.Combine(dir, $"{name}_{Guid.NewGuid():N}{ext}");
    }

    private void ImportLicense()
    {
        using var d = new OpenFileDialog
        {
            Filter = "PrivacyPicライセンス|*.lic|すべてのファイル|*.*",
            Title = "Proライセンスを読み込む"
        };

        if (d.ShowDialog(this) != DialogResult.OK) return;

        try
        {
            var rc = NativeCore.InstallLicense(d.FileName);
            if (rc != 0)
                throw new InvalidDataException($"ライセンスを有効化できませんでした。コード: {rc}");

            RefreshPlan();
            MessageBox.Show(this, "Proライセンスを有効化しました。\n10段階検証を通過しています。", "PrivacyPic", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "ライセンスエラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }
}
