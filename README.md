Live viewer: **[https://1921music.github.io/mscz_webviewer/](https://1921music.github.io/mscz_webviewer/)**

# mscz_webviewer

A browser viewer for MuseScore and MusicXML scores, powered by WebAssembly and rendered as SVG. Open a score, zoom in, and transpose the displayed notation without changing the original file.

This project uses substantial portions of [aria](https://github.com/hdi200/aria), which is based on MuseScore Studio, and additional code taken directly from [MuseScore Studio](https://github.com/musescore/MuseScore), including the MusicXML importer. Thank you to their authors and contributors.

[日本語](#日本語)

## Use the viewer

1. Open the [live viewer](https://1921music.github.io/mscz_webviewer/) and wait for the engine to load.
2. Choose a score file, or drag and drop it onto the page.
3. Use the zoom and semitone transpose controls. Reset transposition to return to the original key.

Supported files: **`.mscz`, `.musicxml`, `.xml` (score-partwise), and `.mxl`**.

Scores selected from your device are processed locally in your browser; they are not uploaded to a server. The current interface includes Japanese labels. The viewer focuses on display and transposition; playback and score editing are not included.

## One HTML file, including offline use

[Download the standalone HTML](https://github.com/1921music/mscz_webviewer/raw/refs/heads/main/docs/index.html), save it as `index.html`, and open it directly in a compatible browser such as Microsoft Edge. It also works offline over **`file://`**; no local web server is required.

The HTML contains the JavaScript, WASM engine, fonts, and other required resources. No companion files need to be downloaded. Open scores from your device using the file picker or drag and drop. Loading a score from a URL requires a network connection.

### File sizes

Measured on November 3, 2026. MiB = 1,048,576 bytes; KiB = 1,024 bytes.

| File | Bytes | Size |
|---|---:|---:|
| **Complete standalone HTML** (`docs/index.html`) | **3,784,130** | **3.61 MiB** |
| WASM engine (`mscz-engine.wasm`) | 4,791,882 | 4.57 MiB |
| Engine JavaScript (`mscz-engine.js`) | 17,138 | 16.74 KiB |
| JavaScript wrapper (`mscz.js`) | 7,349 | 7.18 KiB |
| Font handling (`text-fonts.js`) | 5,687 | 5.55 KiB |

The component rows show the regular build's individual files. The standalone HTML embeds its assets using gzip compression and Base64 encoding, so the complete HTML is smaller than the uncompressed WASM engine. **The 3.61 MiB total already includes the engine and fonts.**

## Development

The rendering core is C++, compiled to WebAssembly without Qt. The web interface lives in [`web/`](web/), the browser integration in [`src/`](src/), and build and verification scripts in [`scripts/`](scripts/).

For build instructions, architecture, provenance, benchmarks, and implementation history, see [Development notes](DEVELOPMENT_NOTES.md). These notes include historical plans and machine-specific paths. GitHub Pages serves the checked-in standalone HTML from `main` → `/docs`.

## License

**GPL-3.0-only.** See [LICENSE.txt](LICENSE.txt), copied unchanged from aria, and [Third-party notices](THIRD_PARTY_NOTICES.md).

The viewer includes GPL-licensed aria and MuseScore code. Distributing the complete viewer under MIT would require separate permission from the relevant copyright holders. Fonts and third-party libraries retain their respective licenses and copyright notices. Corresponding source and build instructions are available in this repository.

## 日本語

公開版: **[https://1921music.github.io/mscz_webviewer/](https://1921music.github.io/mscz_webviewer/)**

MuseScore と MusicXML の楽譜をブラウザーで表示するビューアーです。WebAssembly で楽譜を処理し、SVG で描画します。元の楽譜ファイルを変更せずに、表示の拡大・縮小と移調ができます。

本プロジェクトは [aria](https://github.com/hdi200/aria) の多くのコードを利用しています。また、[MuseScore Studio](https://github.com/musescore/MuseScore) からも MusicXML インポーターなどのコードを直接取り込んでいます。aria 自体も MuseScore Studio を基にしています。原著作者・貢献者の皆さまに感謝します。

### 使い方

1. [公開版](https://1921music.github.io/mscz_webviewer/) を開き、エンジンの読み込みを待ちます。
2. ファイル選択、またはドラッグ＆ドロップで楽譜を開きます。
3. 拡大・縮小や半音単位の移調を操作します。「原調に戻す」で移調をリセットできます。

対応形式: **`.mscz`、`.musicxml`、`.xml`（score-partwise）、`.mxl`**。

端末から選択した楽譜はブラウザー内で処理され、サーバーにはアップロードされません。表示と移調を目的としたビューアーで、再生・楽譜編集の機能は含みません。

### 単一 HTML とオフライン利用

[単一 HTML をダウンロード](https://github.com/1921music/mscz_webviewer/raw/refs/heads/main/docs/index.html)して `index.html` として保存し、Microsoft Edge などの対応ブラウザーで直接開けます。**`file://` でもオフラインで利用でき、ローカルサーバーの起動は不要です。**

JavaScript・WASM・フォントなどの必要な素材をすべて 1 個の HTML に内包しているため、別ファイルのダウンロードは不要です。端末内の楽譜をファイル選択またはドラッグ＆ドロップで開いてください。URL から楽譜を取得する場合は通信が必要です。

### ファイルサイズ

2026-11-03 時点の実測値です。MiB = 1,048,576 bytes、KiB = 1,024 bytes。

| ファイル | サイズ（bytes） | 換算 |
|---|---:|---:|
| **単一 HTML 全体**（`docs/index.html`） | **3,784,130** | **3.61 MiB** |
| WASM 本体（`mscz-engine.wasm`） | 4,791,882 | 4.57 MiB |
| エンジン JavaScript（`mscz-engine.js`） | 17,138 | 16.74 KiB |
| JavaScript ラッパー（`mscz.js`） | 7,349 | 7.18 KiB |
| フォント制御（`text-fonts.js`） | 5,687 | 5.55 KiB |

各素材の行は通常版のビルド成果物のサイズです。単一 HTML では素材を gzip 圧縮・Base64 化して内包するため、非圧縮の WASM 本体より小さくなっています。**3.61 MiB は WASM・フォントを含む HTML 全体のサイズです。**

### 開発について

描画コアは C++ 製で、Qt に依存せず WebAssembly にコンパイルします。Web UI は [`web/`](web/)、ブラウザー連携は [`src/`](src/)、ビルド・検証スクリプトは [`scripts/`](scripts/) にあります。

ビルド手順・設計・コードの来歴・性能計測・実装経緯は [開発メモ](DEVELOPMENT_NOTES.md) にまとめています。開発メモには過去の計画や環境固有のパスも含まれます。GitHub Pages は `main` ブランチの `/docs` に保存した単一 HTML を公開しています。

### ライセンス

**GPL-3.0-only** で公開しています。aria から原文のままコピーした [LICENSE.txt](LICENSE.txt) と [第三者通知](THIRD_PARTY_NOTICES.md) を参照してください。

aria・MuseScore 由来の GPL コードを含むため、現在のビューアー全体を MIT ライセンスで配布するには、該当する権利者から別途許諾を得る必要があります。フォントや第三者ライブラリーにはそれぞれのライセンス・著作権表記が適用されます。対応するソースコードとビルド手順はこのリポジトリで提供しています。
