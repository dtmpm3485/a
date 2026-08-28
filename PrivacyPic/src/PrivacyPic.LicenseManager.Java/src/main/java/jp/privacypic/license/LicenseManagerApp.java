package jp.privacypic.license;

import javafx.application.Application;
import javafx.geometry.Insets;
import javafx.scene.Scene;
import javafx.scene.control.*;
import javafx.scene.layout.*;
import javafx.stage.FileChooser;
import javafx.stage.Stage;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.*;
import java.security.spec.PKCS8EncodedKeySpec;
import java.time.*;
import java.util.Base64;
import java.util.UUID;

public final class LicenseManagerApp extends Application {
    private final TextField orderId = new TextField();
    private final TextField deviceId = new TextField();
    private final TextField keyPath = new TextField();
    private final RadioButton permanent = new RadioButton("永久ライセンス");
    private final RadioButton expiring = new RadioButton("期限付き");
    private final DatePicker expiryDate = new DatePicker(LocalDate.now().plusYears(1));
    private final ListView<String> history = new ListView<>();

    public static void launchApp(String[] args) {
        Application.launch(LicenseManagerApp.class, args);
    }

    @Override
    public void start(Stage stage) {
        stage.setTitle("PrivacyPic License Manager v2");
        stage.setMinWidth(760);
        stage.setMinHeight(620);

        var title = new Label("PrivacyPic Pro ライセンス管理");
        title.setStyle("-fx-font-size: 24px; -fx-font-weight: 700;");

        var warning = new Label("秘密鍵は購入者へ絶対に渡さないでください。この管理ツールは端末固定の署名付きライセンスを発行します。");
        warning.setWrapText(true);
        warning.setStyle("-fx-text-fill: #9b1c1c; -fx-font-weight: 600;");

        orderId.setPromptText("例: X-20260828-001");
        deviceId.setPromptText("購入者のPrivacyPicに表示される PP2-... を貼り付け");
        keyPath.setPromptText("PrivacyPic-private-key.pem");
        keyPath.setText(Path.of(System.getProperty("user.dir"), "PrivacyPic-private-key.pem").toString());

        var chooseKey = new Button("秘密鍵を選択");
        chooseKey.setOnAction(e -> choosePrivateKey(stage));

        var keyRow = new HBox(8, keyPath, chooseKey);
        HBox.setHgrow(keyPath, Priority.ALWAYS);

        var group = new ToggleGroup();
        permanent.setToggleGroup(group);
        expiring.setToggleGroup(group);
        permanent.setSelected(true);
        expiryDate.disableProperty().bind(permanent.selectedProperty());

        var typeRow = new HBox(18, permanent, expiring, expiryDate);

        var generate = new Button("Proライセンスを発行");
        generate.setStyle("-fx-font-size: 15px; -fx-font-weight: 700; -fx-padding: 10 20;");
        generate.setOnAction(e -> generateLicense(stage));

        var copyDeviceHint = new Label("※ 購入者には PrivacyPicLauncher.exe から起動 → 画面下部の「端末ID」を送ってもらいます。");
        copyDeviceHint.setWrapText(true);
        copyDeviceHint.setStyle("-fx-text-fill: #555;");

        history.setPrefHeight(190);
        loadHistory();

        var form = new VBox(
            8,
            fieldLabel("注文ID / 管理番号"), orderId,
            fieldLabel("購入者の端末ID"), deviceId,
            fieldLabel("秘密鍵"), keyRow,
            fieldLabel("ライセンス種別"), typeRow,
            copyDeviceHint,
            generate,
            new Separator(),
            fieldLabel("発行履歴"), history
        );

        var root = new VBox(14, title, warning, form);
        root.setPadding(new Insets(22));
        root.setStyle("-fx-background-color: #f7f8fb;");

        stage.setScene(new Scene(root, 780, 650));
        stage.show();
    }

    private static Label fieldLabel(String text) {
        var label = new Label(text);
        label.setStyle("-fx-font-weight: 700; -fx-padding: 6 0 0 0;");
        return label;
    }

    private void choosePrivateKey(Stage stage) {
        var chooser = new FileChooser();
        chooser.setTitle("PrivacyPic秘密鍵を選択");
        chooser.getExtensionFilters().add(new FileChooser.ExtensionFilter("PEM秘密鍵", "*.pem"));
        var file = chooser.showOpenDialog(stage);
        if (file != null) keyPath.setText(file.getAbsolutePath());
    }

    private void generateLicense(Stage stage) {
        try {
            var order = sanitize(orderId.getText());
            var device = sanitize(deviceId.getText()).toUpperCase();

            if (order.isBlank()) throw new IllegalArgumentException("注文IDを入力してください。");
            if (!device.matches("(?:PP2-[0-9A-F]{32}|PC-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4})"))
                throw new IllegalArgumentException("端末IDが不正です。PrivacyPicに表示された端末IDをそのまま貼り付けてください。");

            long issued = Instant.now().getEpochSecond();
            long expires = 0;

            if (expiring.isSelected()) {
                var date = expiryDate.getValue();
                if (date == null || date.isBefore(LocalDate.now()))
                    throw new IllegalArgumentException("有効期限を確認してください。");
                expires = date.atTime(23, 59, 59)
                        .atZone(ZoneId.systemDefault())
                        .toInstant()
                        .getEpochSecond();
            }

            String licenseId = UUID.randomUUID().toString().replace("-", "").toUpperCase();
            String payloadText = String.join("|",
                    "PP2",
                    licenseId,
                    order,
                    "PRO",
                    Long.toString(issued),
                    Long.toString(expires),
                    device,
                    "BATCH,FOLDER"
            );

            byte[] payload = payloadText.getBytes(StandardCharsets.UTF_8);
            byte[] signature = sign(payload, loadPrivateKey(Path.of(keyPath.getText())));

            String json = "{\n" +
                    "  \"version\": 2,\n" +
                    "  \"payload\": \"" + Base64.getEncoder().encodeToString(payload) + "\",\n" +
                    "  \"signature\": \"" + Base64.getEncoder().encodeToString(signature) + "\"\n" +
                    "}\n";

            var chooser = new FileChooser();
            chooser.setTitle("PrivacyPicライセンスを保存");
            chooser.getExtensionFilters().add(new FileChooser.ExtensionFilter("PrivacyPic License", "*.lic"));
            chooser.setInitialFileName("PrivacyPic_" + safeFileName(order) + ".lic");

            var file = chooser.showSaveDialog(stage);
            if (file == null) return;

            Files.writeString(file.toPath(), json, StandardCharsets.UTF_8);
            appendHistory(order, device, expires, licenseId);

            alert(Alert.AlertType.INFORMATION,
                    "発行完了",
                    "ライセンスを発行しました。\n\n" + file.getAbsolutePath() +
                    "\n\n購入者へ送るのは .lic ファイルだけです。");
        } catch (Exception ex) {
            alert(Alert.AlertType.ERROR, "発行エラー", ex.getMessage());
        }
    }

    private static PrivateKey loadPrivateKey(Path path) throws Exception {
        String pem = Files.readString(path, StandardCharsets.US_ASCII)
                .replace("-----BEGIN PRIVATE KEY-----", "")
                .replace("-----END PRIVATE KEY-----", "")
                .replaceAll("\\s+", "");
        byte[] der = Base64.getDecoder().decode(pem);
        return KeyFactory.getInstance("RSA").generatePrivate(new PKCS8EncodedKeySpec(der));
    }

    private static byte[] sign(byte[] payload, PrivateKey key) throws Exception {
        Signature signer = Signature.getInstance("SHA256withRSA");
        signer.initSign(key);
        signer.update(payload);
        return signer.sign();
    }

    private void loadHistory() {
        var p = historyPath();
        if (!Files.exists(p)) return;
        try {
            history.getItems().setAll(Files.readAllLines(p, StandardCharsets.UTF_8));
        } catch (IOException ignored) {
        }
    }

    private void appendHistory(String order, String device, long expires, String licenseId) {
        try {
            var p = historyPath();
            Files.createDirectories(p.getParent());
            String expiry = expires == 0 ? "永久" : Instant.ofEpochSecond(expires).toString();
            String line = LocalDateTime.now() + " | " + order + " | " + device + " | " + expiry + " | " + licenseId;
            Files.writeString(
                    p,
                    line + System.lineSeparator(),
                    StandardCharsets.UTF_8,
                    StandardOpenOption.CREATE,
                    StandardOpenOption.APPEND
            );
            history.getItems().add(0, line);
        } catch (IOException ignored) {
        }
    }

    private static Path historyPath() {
        String appdata = System.getenv("APPDATA");
        Path base = appdata == null || appdata.isBlank()
                ? Path.of(System.getProperty("user.home"))
                : Path.of(appdata);
        return base.resolve("PrivacyPicLicenseManager").resolve("history.log");
    }

    private static String sanitize(String s) {
        return s == null ? "" : s.replace("|", "-").replace("\r", " ").replace("\n", " ").trim();
    }

    private static String safeFileName(String s) {
        return s.replaceAll("[\\\\/:*?\"<>|]", "_");
    }

    private static void alert(Alert.AlertType type, String title, String text) {
        var a = new Alert(type);
        a.setTitle(title);
        a.setHeaderText(null);
        a.setContentText(text);
        a.showAndWait();
    }
}
