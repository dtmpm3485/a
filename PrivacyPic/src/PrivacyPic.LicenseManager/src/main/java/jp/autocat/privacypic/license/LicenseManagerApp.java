package jp.autocat.privacypic.license;

import javafx.application.Application;
import javafx.geometry.Insets;
import javafx.scene.Scene;
import javafx.scene.control.*;
import javafx.scene.layout.*;
import javafx.stage.FileChooser;
import javafx.stage.Stage;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.KeyFactory;
import java.security.PrivateKey;
import java.security.Signature;
import java.security.spec.PKCS8EncodedKeySpec;
import java.time.*;
import java.time.format.DateTimeFormatter;
import java.util.Base64;
import java.util.Locale;
import java.util.UUID;

public final class LicenseManagerApp extends Application {
    // Replaced only inside the private build job. Never committed with a real private key.
    private static final String PRIVATE_KEY_B64 = "__PRIVATE_KEY_DER_B64__";

    private final TextField orderId = new TextField();
    private final TextField deviceCode = new TextField();
    private final RadioButton permanent = new RadioButton("永久");
    private final RadioButton expiring = new RadioButton("期限付き");
    private final DatePicker expiryDate = new DatePicker(LocalDate.now().plusYears(1));
    private final Spinner<Integer> expiryHour = new Spinner<>(0, 23, 23);
    private final Spinner<Integer> expiryMinute = new Spinner<>(0, 59, 59);
    private final Label status = new Label(" ");

    @Override
    public void start(Stage stage) {
        stage.setTitle("PrivacyPic License Manager — PRIVATE");

        var title = new Label("PrivacyPic Pro ライセンス管理");
        title.setStyle("-fx-font-size: 24px; -fx-font-weight: 700;");

        var warning = new Label(
                "管理者専用です。このアプリにはProライセンスを署名できる秘密鍵が含まれます。\n" +
                "購入者や第三者には絶対に配布しないでください。");
        warning.setStyle("-fx-text-fill: #a40000; -fx-font-weight: 600;");

        orderId.setPromptText("例: X-20260828-001");
        deviceCode.setPromptText("例: PC-1A2B-3C4D-5E6F-7788");

        var copyHelp = new Label(
                "購入者にPrivacyPicの「端末コードをコピー」を押してもらい、そのコードをここへ貼り付けます。");
        copyHelp.setWrapText(true);

        var toggle = new ToggleGroup();
        permanent.setToggleGroup(toggle);
        expiring.setToggleGroup(toggle);
        permanent.setSelected(true);

        var expiryRow = new HBox(8, expiryDate, new Label("時"), expiryHour, new Label("分"), expiryMinute);
        expiryRow.disableProperty().bind(expiring.selectedProperty().not());

        var issue = new Button("Proライセンスを発行");
        issue.setDefaultButton(true);
        issue.setStyle("-fx-font-size: 15px; -fx-font-weight: 700;");
        issue.setOnAction(e -> issueLicense(stage));

        var form = new GridPane();
        form.setHgap(12);
        form.setVgap(10);
        form.addRow(0, new Label("注文ID / 管理番号"), orderId);
        form.addRow(1, new Label("端末コード"), deviceCode);
        form.addRow(2, new Label("ライセンス"), new HBox(14, permanent, expiring));
        form.addRow(3, new Label("有効期限"), expiryRow);
        ColumnConstraints left = new ColumnConstraints();
        left.setMinWidth(150);
        ColumnConstraints right = new ColumnConstraints();
        right.setHgrow(Priority.ALWAYS);
        form.getColumnConstraints().addAll(left, right);

        status.setWrapText(true);

        var root = new VBox(15, title, warning, new Separator(), form, copyHelp, issue, status);
        root.setPadding(new Insets(22));
        root.setPrefWidth(680);

        stage.setScene(new Scene(root));
        stage.setResizable(false);
        stage.show();
    }

    private void issueLicense(Stage stage) {
        try {
            String order = sanitizeOrder(orderId.getText());
            String device = deviceCode.getText() == null
                    ? ""
                    : deviceCode.getText().trim().toUpperCase(Locale.ROOT);

            if (order.isBlank()) {
                throw new IllegalArgumentException("注文IDを入力してください。");
            }
            if (!device.matches("PC-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}")) {
                throw new IllegalArgumentException("端末コードの形式が正しくありません。");
            }

            Instant issued = Instant.now();
            String expires = "NEVER";
            if (expiring.isSelected()) {
                LocalDate date = expiryDate.getValue();
                if (date == null) throw new IllegalArgumentException("有効期限の日付を選択してください。");
                LocalDateTime local = date.atTime(expiryHour.getValue(), expiryMinute.getValue(), 59);
                Instant exp = local.atZone(ZoneId.systemDefault()).toInstant();
                if (!exp.isAfter(issued)) throw new IllegalArgumentException("有効期限は現在より後にしてください。");
                expires = DateTimeFormatter.ISO_INSTANT.format(exp);
            }

            String licenseId = UUID.randomUUID().toString().replace("-", "").toUpperCase(Locale.ROOT);
            String payloadText = String.join("|",
                    "PP2",
                    licenseId,
                    order,
                    "PRO",
                    DateTimeFormatter.ISO_INSTANT.format(issued),
                    expires,
                    device);

            byte[] payload = payloadText.getBytes(StandardCharsets.UTF_8);
            byte[] signature = sign(payload);

            String json = "{\n" +
                    "  \"payload\": \"" + Base64.getEncoder().encodeToString(payload) + "\",\n" +
                    "  \"signature\": \"" + Base64.getEncoder().encodeToString(signature) + "\"\n" +
                    "}\n";

            FileChooser chooser = new FileChooser();
            chooser.setTitle("PrivacyPic Pro ライセンスを保存");
            chooser.getExtensionFilters().add(new FileChooser.ExtensionFilter("PrivacyPic License", "*.lic"));
            chooser.setInitialFileName("PrivacyPic_" + safeFilePart(order) + ".lic");

            File target = chooser.showSaveDialog(stage);
            if (target == null) return;

            Files.writeString(target.toPath(), json, StandardCharsets.UTF_8);
            status.setStyle("-fx-text-fill: #087830; -fx-font-weight: 600;");
            status.setText("発行しました: " + target.getAbsolutePath() + "\n端末: " + device);
        } catch (Exception ex) {
            status.setStyle("-fx-text-fill: #b00020; -fx-font-weight: 600;");
            status.setText("発行エラー: " + ex.getMessage());
        }
    }

    private static byte[] sign(byte[] payload) throws Exception {
        byte[] privateDer = Base64.getDecoder().decode(PRIVATE_KEY_B64);
        PrivateKey privateKey = KeyFactory.getInstance("RSA")
                .generatePrivate(new PKCS8EncodedKeySpec(privateDer));
        Signature signer = Signature.getInstance("SHA256withRSA");
        signer.initSign(privateKey);
        signer.update(payload);
        return signer.sign();
    }

    private static String sanitizeOrder(String value) {
        if (value == null) return "";
        String out = value.replace('|', '-').replace('\r', ' ').replace('\n', ' ').trim();
        if (out.length() > 128) throw new IllegalArgumentException("注文IDは128文字以内にしてください。");
        return out;
    }

    private static String safeFilePart(String value) {
        String s = value.replaceAll("[\\\\/:*?\\"<>|]", "_");
        return s.isBlank() ? "license" : s;
    }

    public static void main(String[] args) {
        launch(args);
    }
}
