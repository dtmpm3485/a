# H5GGX

H5GG の JavaScript API と互換性を保ちながら、スマホ操作向けの UI と解析補助を追加するカスタムフロントエンドです。

## v0.1

- 日本語ダーク UI
- 数値検索 / Nearby 検索
- 検索履歴
- 結果の絞り込み
- アドレスのブックマーク
- リアルタイム値監視
- 前回値との changed / unchanged / increased / decreased 差分フィルタ
- 複数選択した結果の一括編集
- セッションを localStorage に保存
- JSON export / import
- 既存 `h5gg.searchNumber`, `getResults`, `getValue`, `setValue`, `editAll`, `clearResults` API を使用

## 使い方

H5GG からこのリポジトリの `Index.html` を読み込んでください。ブラウザ単体で開いた場合は Demo mode になり、UIだけ確認できます。

## 構成

- `Index.html` - メインUI
- `src/h5ggx.js` - H5GG API ラッパー、履歴、監視、差分、ブックマーク
- `src/style.css` - モバイル向けUI

## 方針

H5GG 本体の JavaScript API 名は変更せず、既存スクリプトとの互換性を崩さない方針です。公式 H5GG でも JS API の互換維持が推奨されています。
