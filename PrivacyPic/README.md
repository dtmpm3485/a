# PrivacyPic

Windows向けの写真メタデータ確認・削除ツールです。

## 構成

- PrivacyPic.exe: Free/Pro共通本体。未認証はFree、署名済み .lic でPro化。
- privacypic_core.dll: Rust製の画像メタデータ処理コア。
- PrivacyPicLicenseGenerator.exe: 管理者専用ライセンス発行ツール。購入者へ配布禁止。

## Free / Pro

Free:
- JPG/JPEG/PNG/WebPの主要メタデータ検出
- 画像を安全化したコピーとして保存
- 1回5枚まで

Pro:
- 枚数制限なし
- フォルダ一括追加・再帰処理

## 削除対象

- JPEG: EXIF / XMP / IPTC(APP13) / コメント
- PNG: eXIf / tEXt / zTXt / iTXt / tIME
- WebP: EXIF / XMP

元ファイルは変更せず、PrivacyPic_Safe フォルダへコピーを作ります。

## 注意

この初期版はWindows x64向けです。EXEはコード署名していないため、Windows SmartScreenが警告する場合があります。
ライセンス発行exeには秘密鍵が埋め込まれているため、絶対に公開・販売・配布しないでください。
