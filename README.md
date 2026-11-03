公開版: [https://1921music.github.io/mscz_webviewer/](https://1921music.github.io/mscz_webviewer/)

# mscz_test — mscz をブラウザで表示する（Qt非依存・SVG出力）

このプロジェクトは [aria](https://github.com/hdi200/aria) の多くのコードを利用しています。
また、[MuseScore Studio](https://github.com/musescore/MuseScore) からも一部のコードを直接取り込んでいます
（MusicXML インポーターなど）。aria 自体も MuseScore Studio を基にしています。
原著作者・貢献者の皆さまに感謝します。

MuseScore の `.mscz` と MusicXML（`.musicxml` / `.xml` / 圧縮版 `.mxl`）をブラウザで表示し、移調して表示できるようにするための実験リポジトリ。
将来 [podium](https://github.com/studiop5/podium) に組み込むことを想定しているが、**このリポジトリの
ゴールは単体で Web 表示できるところまで**。

## 単一 HTML とファイルサイズ

公開版は **JavaScript・WASM・フォントなどの必要な素材をすべて内包した、1 個の HTML ファイル**です。
[単一 HTML をダウンロード](https://github.com/1921music/mscz_webviewer/raw/refs/heads/main/docs/index.html)し、
保存した `index.html` を Edge などの対応ブラウザーで直接開けば、**`file://` でもオフラインで利用できます**。
サーバーの起動や、別の WASM・フォントファイルのダウンロードは不要です。
楽譜は「ファイルの選択」またはドラッグ＆ドロップで開きます。URL から楽譜を取得する場合は通信が必要です。

2026-11-03 時点の実測サイズ（MiB = 1,048,576 bytes、KiB = 1,024 bytes）:

| ファイル | サイズ（bytes） | 換算 |
|---|---:|---:|
| 公開する単一 HTML（`docs/index.html`） | 3,784,130 | 3.61 MiB |
| WASM 本体（`mscz-engine.wasm`） | 4,791,882 | 4.57 MiB |
| エンジン JavaScript（`mscz-engine.js`） | 17,138 | 16.74 KiB |
| JavaScript ラッパー（`mscz.js`） | 7,349 | 7.18 KiB |
| フォント制御 JavaScript（`text-fonts.js`） | 5,687 | 5.55 KiB |

WASM と JavaScript の各行は通常版のビルド成果物のサイズです。
単一 HTML では素材を gzip 圧縮・Base64 化して内包するため、WASM 本体より小さくなっています。
**3.61 MiB は WASM・フォントを含む HTML 全体のサイズ**で、表の各ファイルを追加で取得する必要はありません。

## ライセンス

本プロジェクトは **GNU General Public License version 3 only（GPL-3.0-only）** で公開します。
aria・MuseScore 由来の GPL コードを含むため、権利者から別途許諾を得ない限り、現在のビューアー全体を MIT ライセンスに変更することはできません。
ライセンス全文は [LICENSE.txt](LICENSE.txt)、取り込み元と第三者素材の情報は
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。
各ファイルの著作権・ライセンス表記は保持し、フォントや第三者ライブラリーにはそれぞれのライセンスが適用されます。
公開している単一 HTML 版のソースコードとビルド手順は、このリポジトリで提供しています。

## MusicXML の読み込み

`.musicxml` / `.xml`（score-partwise）と圧縮版`.mxl`を、通常版・単一HTML版の両方で読み込める。
ファイル選択、ドラッグ＆ドロップ、`?score=URL`、JSの`engine.open(bytes, name)`で利用できる。
`name`の拡張子で読み込み形式を選ぶため、MusicXMLでは実際の拡張子を渡す。
読み込み後の表示・ズーム・元譜基準の移調はMSCZと共通。元ファイルは変更しない。

インポーターはエンジンと同じMuseScore v4.7.5
(`3654226c2e99289916916953a98e585a3d3b315a`) の2パス処理を直接利用する。
出典・元ソースのSHA-256・ローカル変更は
`vendor/aria/src/importexport/musicxml/SOURCE.json`に記録する。
再取得は`python3 scripts/vendor-musicxml.py --source ../MuseScore`。
`vendor-aria.sh`で全体を取り直した場合も、このスクリプトを後から実行する。
MusicXML書き出し、GUI、XSD検証は組み込まない（本家の同モジュールもXSD検証を無効化）。
XML構文の読み取り・楽譜への変換は本家のパーサーを使う。

本家の`NotationProject::doImport()`に合わせ、コードリストを初期化してから読み込み、
`EngravingProject::setupMasterScore()`で仕上げる。インポート時の既定設定も本家を使い、
MusicXMLに含まれる改行・改ページ・書体を取り込む。MSC形式の旧版互換処理は適用しない。
`.mxl`は既存ZIP実装とホストのストリームから`META-INF/container.xml`のrootfileを読み込む。
ブラウザのファイルシステムやサーバー側変換は不要。

`mscz_musicxml_test`で、通常XMLとMXLの組版一致・入れ子のrootfile・不正コンテナ・
移調とリセットを検証する。サンプルのパスを引数に渡すと圧縮前後の一致も検証する。
ブラウザの検証は`scripts/check-wasm-bun.ts`で既存MSCZと併せて行う。

2026-11-03のサイズ比較（変更前`bd42a06`、同じMinSizeRel・サイズ最適化・LTO設定）:

| 成果物 | 変更前（bytes） | MusicXML対応後（bytes） | 増加 |
|---|---:|---:|---:|
| WASM本体 | 4,507,893 | 4,791,882 | 283,989（6.30%） |
| WASM gzip-9 | 1,704,412 | 1,802,933 | 98,521（5.78%） |
| WASM Brotli-11 | 1,303,938 | 1,380,107 | 76,169（5.84%） |
| 単一HTML全体 | 3,652,533 | 3,784,130 | 131,597（3.60%） |

WASM圧縮値は配信用に測ったサイズ。単一HTMLは従来どおり各素材をgzip圧縮してからbase64化する。

ベースにするのは [aria](https://github.com/hdi200/aria)（MuseScore Studio 4.7.5 派生のスコアリーダー）の
レンダーコア。想定配置:

```
C:\work\20260809\aria       aria のクローン（エンジン本体）
C:\work\20260809\mscz_test  このリポジトリ
```

## なぜ aria をベースにできるのか

aria には `sandbox/engraving/score_render_core.h` という **Qt 型を一切公開しない C++ ファサード**が
既にある。`.mscz` の読み込み・レイアウト・移調は全部ここで済んでいる。

| やりたいこと | 既存 API |
|---|---|
| スコアを開く | `ScoreRenderSession::open(path)` |
| ページを描く | `renderPage(pageIndex, dpi, RenderedPage&)` → PNG / PDF |
| **表示だけ移調** | `setViewTransposeKey(targetKey)` / `clearViewTranspose()` |
| コンサートピッチ | `setConcertPitchEnabled(bool)` |
| パート譜切替 | `setActivePartIndex(optional<int>)` |
| 元の調を知る | `scoreStartKey()` |
| レイアウト回帰チェック | `layoutFingerprint()` |

つまり**新しく書くのは出力段だけ**。描画は必ず `muse::draw::IPaintProvider`
(`src/framework/draw/ipaintprovider.h`) を通るので、ここに SVG 実装を 1 本足せばよい。
メソッドは 30 個程度（drawPath / drawPolygon / drawText / drawSymbol / drawPixmap /
clip / transform / save-restore）。Qt 非依存の実装例として `bufferedpaintprovider.cpp`
（描画コマンドを `DrawData` に記録）が既にある。

## Qt 依存の実体

| 層 | 状況 |
|---|---|
| `src/engraving`（471 cpp） | **Qt を include しているのは 5 ファイルだけ**。CMake の `Qt::Quick` は `MUE_BUILD_ENGRAVING_QML=OFF` で不要 |
| `muse::global` | 独自の String / ByteArray / json / xml / **zip(zlib)** を持ち、Qt 依存は `NO_QT_SUPPORT` でガード済み。上流に `MUSE_QT_SUPPORT=OFF` スイッチあり |
| `muse::draw` | `DRAW_NO_INTERNAL` で Qt プロバイダを外せる |
| **aria の `sandbox/engraving/*.cpp`** | ここが一番 Qt 漬け。QImage / QPdfWriter / QBuffer（出力）、QGuiApplication（初期化）、QFile / QDir / QSaveFile（IO）、QJson*、QThread、QMimeData。量は限定的 |

### 最大のリスク: フォント

aria の iOS / Android 経路は `DRAW_QT_FONT_PROVIDER_ONLY` + `MUSE_MODULE_DRAW_USE_QTTEXTDRAW` で
**Qt のフォントメトリクスとテキスト描画に依存**している（`fontengineft.cpp` は
`src/framework/draw/CMakeLists.txt` でコメントアウト済み）。Qt を外すと FreeType + HarfBuzz の
`fontprovider.cpp` / `fontsengine.cpp` 経路に切り替わり、**メトリクスが変われば譜面の見た目が変わる**。
Phase 1 でネイティブの参照出力を取っておくのは、この差分を見えるようにするため。

また `IFontFace` には glyphBbox / advance はあるが**グリフのアウトライン取得 API が無い**
（msdfgen の Shape は無効化中）。記号をパス化するには `FT_Outline_Decompose` を使うメソッドを
追加する必要がある。

## 決定事項

| 項目 | 決定 |
|---|---|
| 出力形式 | **SVG**（1 ページ 1 SVG） |
| フォント | **ハイブリッド**: 音楽記号（`drawSymbol`）はパス化して `<defs>` + `<use>` で再利用、文章（`drawText`）は `<text>` + フォント埋め込み |
| 機能範囲 | 表示 + 移調 + パート譜切替 + コンサートピッチ。**再生・編集は後回し**（再生は FluidSynth + 数十 MB の SoundFont が必要） |
| 移調 UI | まず半音単位の上下。他方式（調指定・楽器の移調）も足せる形にしておく |
| ビルド環境 | WSL2 の使い捨てディストロ `mscz_dev` |

## フェーズ計画

**Phase 1 — ネイティブ（Qt 付き）で見た目を確定させる**
- WSL 上で aria のホストビルドを通す
- `SvgPaintProvider : IPaintProvider` を書く
- 出力 SVG を既存の PNG / PDF と見比べ、`layoutFingerprint()` で回帰チェック
- → **Qt 経由の参照出力が取れる状態で正しさを確定させるのが目的**

**Phase 2 — aria から必要なものを取り込み、Qt を外す（まだネイティブ）**
- **aria のソースをこのリポジトリに vendor し、自由に改変する**（次節）
- `MUSE_QT_SUPPORT=OFF` / `DRAW_NO_INTERNAL`
- フォントを FreeType + HarfBuzz 経路に切り替え、Phase 1 の出力と差分を取る
- グルー層（QFile / QJson / QImage / QGuiApplication …）を置換

**Phase 3 — emscripten で wasm 化**
- サイズ最小化が要件。後述の方針に従う
- → 実測結果は「Phase 3 の実測結果」節

## GitHub Pages

公開版: https://1921music.github.io/mscz_webviewer/

`docs/index.html` はエンジンとフォントを内包する単一HTML版。
このファイルを保存すればオフラインでも利用できる。
GitHub Pages は `main` ブランチの `/docs` を公開する。
更新時は `scripts/build-wasm.sh` でビルドし、`build/web/index-standalone.html` を
`docs/index.html` にコピーしてコミット・pushする。

## 単一HTML版（file:// 対応）

`scripts/build-wasm.sh` は通常のHTTP用ペイロードに加え、
`<build>/web/index-standalone.html` も生成する。このHTMLだけをコピーしてEdgeなどで開き、
「ファイルの選択」またはドラッグ＆ドロップで `.mscz` を読み込める。サーバーは不要。
JavaScript、wasm、フォント、core/optionalの全リソースをgzip圧縮してからBase64で内包し、
起動時にブラウザー標準の`DecompressionStream('gzip')`で展開する。起動時の外部取得は行わない。
圧縮は生成スクリプトのPython標準ライブラリで行い、JavaScriptの展開ライブラリは追加しない。
通常のHTTP版のJS・wasm・リソース形式は変更しない。
譜面自体はHTMLに含めず、ユーザーが選択したファイルを端末内で処理する。

生成元は共通の `web/index.html` と `web/mscz.js`。HTMLまたはラッパーの変更時も、
次のビルドで単一版を再生成する。既存のビルド済みペイロードからの再梱包も可能:

```bash
python3 scripts/gen-standalone-html.py --web build/web
```

Windows上の出力は `G:\work\20261029\podium\podium\build\web\index-standalone.html`。
WSLビルド後に配置する場合:

```bash
cp -r ~/build/mscz-wasm/web/. /mnt/g/work/20261029/podium/podium/build/web/
```

オフラインでのfile://検証（Windows Bun + Edge）:

```powershell
& C:\Tools\bun.exe scripts/check-wasm-bun.ts --standalone build/web/index-standalone.html
```

HTMLを空の検証フォルダーにコピーし、HTTPサーバーを起動せず、Edgeのネットワークを
オフラインにしてサンプルの表示・移調・モード切替・原調復帰を確認する。
追加の依存ファイルへのリクエストがないことも検証する。

## 一般文字とCJKのフォント計測

読み込み後はMuseScore標準の`EngravingCompat`を組版の前後に適用する。
これにより4.6未満の譜面では、当時存在しなかったタイトル枠と楽譜の間の余白が0spになり、
新エンジンの2spを暗黙に足して改ページが増えることを防ぐ。4.6以降の既定値や
明示された余白値は保持する。`mscz_style_compat_test`と
`Shade Of The Cedar Tree.mscz`の1ページ回帰検証で確認する。

4.7未満のリハーサルマークでは、旧`rehearsalMarkAlign`から新`rehearsalMarkPosition`への
スタイル変換も適用する。MuseScore v4.7.5とariaの変換表は同一で、この項目が欠けていた。
`Morning_Gm.mscz`では右寄せが中央寄せに変わり、衝突回避による段間隔の増加で2ページに
なっていた。変換表を補って1ページを維持し、4.7以降の独立した位置指定は保持する。

2026-11-03に、MuseScore v4.7.5 (`3654226c2e99289916916953a98e585a3d3b315a`) の
engraving本体を比較した。対象は読み込み・互換処理・スタイル・組版・音符モデル・初期化で、
tests/API/QML/playback/devtoolsは除外した。変換表や旧スタイルデータの取り込み漏れはなく、
省略したリソースは同梱しない8種類のフォントQRCのみだった。
ただし、こちらのローダーには本家`EngravingProject::setupMasterScore()`の呼び出しが抜けていたため、
同クラスでスコアを所有し、標準の初期化を直接利用するようにした。これにより保存スタイルに基づく
間隔表の再生成、タイの再接続・不要なジャンプ先の後処理を適用する。続いて本家
`NotationProject::doLoad()`と同じ順に、組版前互換処理・カポ情報の反映・`update()`・組版後互換処理を行う。
大きな音符―小節線間隔を保存した譜面とカポを使い、標準経路との組版座標の一致を回帰検証する。

また、本家v4.7.5でも`DefaultStyle`が参照する`legacy-style-defaults-v450.mss`のQRC登録が
欠けていた。実ファイルは既に取り込まれていたため、登録を追加し、旧スタイル5種類の存在と
4.50の間隔設定が実際にロードされることを検証する。
3.x以前の任意の見た目更新（Leland/Edwinへの一括変更・MDL打楽器のリマップ）は適用しない。
旧形式を読み込む必須の互換変換とは別の、デスクトップでユーザーが選ぶ移行処理である。
4.7.6の関連差分も確認したが、括弧対象・タブ譜装飾音のnull参照防止は今回の取り込み漏れとは
別の更新候補として残している。mainへのエンジン更新は行っていない。

ブラウザー版の既定は、一般文字をCanvasで計測し、同じCSSフォント指定でSVGの
`<text>`を描画する方式。端末の欧文・日本語・中国語・韓国語フォントを利用し、
NotoやLiberationの追加同梱、端末のフォントファイルへのアクセス許可を必要としない。
Times New Romanなどのセリフ体、Arialなどのサンセリフ体、Courierなどの等幅を区別する。
同梱済みのEdwin/MuseJazz Text等を一般文字に使う場合は、同じファイルをFontFace APIで
事前ロードする。音楽記号の計測と輪郭描画は従来の内蔵フォント経路を使用する。

```js
const scoreEngine = await load({ base: '.', textFonts: 'browser' }) // 既定
const embeddedEngine = await load({ base: '.', textFonts: 'embedded' }) // 従来の内蔵方式
// 独自バックエンド: { measure(family, px, bold, italic, text), family(name) }
// 返す計測値の順序・単位は web/text-fonts.js の先頭コメントを参照。
```

`create()`から手動でリソースを追加するホストは、`init()`の前に
`await engine.prepareTextFonts()`を呼ぶ。起動時の選択であり、開いている譜面の途中で
方式を変える場合は新しいエンジンを作って元譜を開き直す。
確認用にHTMLのURLへ`?textFonts=embedded`を付けても切り替えられる。

MuseScore側には常に同じ`IFontProvider`を公開し、`src/textfontbackend.h`の
`TextFontBackend`とルーターがホストの計測へ委譲する。ネイティブの既定や
ブラウザーの`embedded`では内蔵プロバイダーへ戻る。後からCJK内蔵バックエンドを
追加しても、MuseScoreの組版コードを変更する必要はない。

ブラウザー方式は端末のフォントに応じて字体・改行・ページ割りが変わる。
対応するCJKフォントが端末に必要で、一般文字を含むSVG単体の別環境への持ち出しも
フォント環境に依存する。通常文字を誤った内蔵フォントでサブセット化して上書きしない。

CJKと3書体系統の回帰検証（WSLでfixtureを生成した後、Windowsで実行）:

```bash
python3 scripts/make-browser-text-fixture.py
```

```powershell
& C:\Tools\bun.exe test/text_fonts.mjs
& C:\Tools\bun.exe scripts/check-wasm-bun.ts --samples build/browser-text-fixture
& C:\Tools\bun.exe scripts/check-wasm-bun.ts --samples build/browser-text-fixture --standalone build/web/index-standalone.html
& C:\Tools\bun.exe scripts/check-wasm-bun.ts --text-fonts embedded
```

## 表示用の移調（2026-11-02）

半音単位で上下し、原調に戻せる。ブラウザ上部の「− 半音」「＋ 半音」
「原調に戻す」で操作し、現在の半音数を表示する。範囲は原調から −12〜+12 半音。
譜面を開く前や移調中は操作を無効にし、範囲の端ではそれ以上進むボタンを無効にする。
別の譜面を開くと原調から始まる。

- C++: `Session::setViewTransposeSemitones(int, error)` と `viewTransposeSemitones()`。
- 表記方針も変更する場合は `Session::setViewTranspose(int, TransposeSpelling, error)`。
  `TransposeSpelling::Readable`（既定）と `Preserve` を選べる。
- wasm: `mscz_set_view_transpose_semitones` と `mscz_view_transpose_semitones`。
- JavaScript:

```js
score.setViewTransposeSemitones(2) // 原調から +2 半音
score.setViewTransposeSemitones(-1) // 原調から -1 半音（現在の表示からの差分ではない）
score.setViewTransposeSpelling('readable') // 現在の半音数で読みやすさ優先に切替
score.setViewTransposeSemitones(2, 'preserve') // 半音数と表記方針を同時指定
score.clearViewTranspose() // 原調に戻す
console.log(score.viewTransposeSemitones) // 0
```

エンジンの移調処理で音符・調号・コードネームを変更し、打楽器は対象外とする。
読み取り専用の元ストリームを保持し、操作のたびに原譜を読み直して候補スコアへ適用する。
候補のレイアウトが完成してから表示用スコアと入れ替えるため、元ファイルを変更せず、
操作順による異名同音の累積を避け、失敗した場合も現在の表示を保持する。
このため大きな譜面では移調ごとに読み込み・レイアウト時間がかかる。

表記方針は「元譜維持」「読みやすさ優先」の2モード。どちらも標準の調号は
♯・♭6個以内とし、6個同士ならG♭側を選ぶ。曲中の調号変更にも適用する。
原調（0半音）はこの補正も行わず元譜をそのまま再現する。

- 元譜維持: 選択した調号に沿う音程で元の綴りを機械的に変換する。
  エンジン既存のダブル記号の簡略化は維持する。
- 読みやすさ優先: 調号・元の綴り・コードの構成音・隣接音の動きからTPCを評価する。
  和声の綴りの不一致100、調に対して余計に不自然な綴り15、ダブル記号12、
  不自然な半音階8、同音名の記号交替5、臨時記号3、中立文脈でのG♭/A♯1を初期コストにする。
  元の綴りの変更は4。短く弱拍にある半音アプローチではダブル記号を40、
  和声の不一致を1とする。コード候補ごとに声部の綴りを動的計画法で評価する。
  元のコードとの関係が明確な和声音は保護し、B調のD♯7をE♭7に直すことはしない。
  C♭・F♭・E♯・B♯にもダブル記号と同じ読みづらさのコストを与える。
  この4種はコード構成音であることだけでは保護せず、例えばD♭m7上のC♭・F♭は
  B♮・E♮を選べる。短調の導音が主音へ解決するなど、明確な機能がある場合は綴りを保護する。

これは元譜を根拠にした保守的な初版で、一般的な和声解析・フレーズ認識を行うものではない。
タイと多声音は通常の綴りを保持し、読みづらい綴りは簡略化できる。タイの前後は同じ綴りに揃える。
コード品質、テンション指定、実音は変更しない。
F♯/B♭の単音としての弱い優遇より、調号と和声の整合性を優先する。
モードを切り替える場合も常に元譜から再計算し、前の表示から補正を累積しない。

ネイティブの回帰テスト（WSL、Qt 付きは `mscz-host` に変更）:

```bash
cmake --build ~/build/mscz-noqt --target mscz_transpose_test
~/build/mscz-noqt/src/mscz_transpose_test /mnt/g/work/20261029/podium/sample/*.mscz
cmake --build ~/build/mscz-noqt --target mscz_spelling_test
~/build/mscz-noqt/src/mscz_spelling_test
```

半音・全音・増4度・オクターブの上下、絶対指定の再現性、不正な範囲指定時の表示保持、
原調に戻した際のページ数・レイアウト指紋・全ページ SVG の完全一致を検証する。
`scripts/check-wasm-bun.ts` では実際のボタン操作、範囲の端、別譜面への切替も確認する。

## aria コードの取り込み方（Phase 2 以降）

**Phase 1 だけは aria を無改変で使う**（参照出力を取るため。ルートの
[CMakeLists.txt](CMakeLists.txt) が aria の `sandbox/engraving` を
`add_subdirectory` するラッパになっている）。

**Phase 2 以降は aria の必要部分をこのリポジトリに取り込み、自由に改変する。**
上流追従は目的ではないので、CMake もソースも遠慮なく削る。

### 粒度の現実

engraving は 471 個の `.cpp` が密に依存し合っているので、**ファイル単位で選ぶのは非現実的**。
実際には「モジュールごと取り込んでディレクトリ単位で削る」になる。自己完結していて
削りやすいのは:

| 削れるディレクトリ | 理由 |
|---|---|
| `engraving/api/v1/` | QML プラグイン API（`ENGRAVING_NO_API`） |
| `engraving/playback/` + `engraving/compat/midi/` | 再生はスコープ外 |
| `engraving/accessibility/` | スクリーンリーダ連携 |
| `engraving/rendering/editmode/` | 編集モード描画 |
| `engraving/qml/` | QML |
| `engraving/data/fonts/*.qrc` | **約 15MB のフォント埋め込み。最大のサイズ要因** |
| `importexport/musicxml/` 全体 | `.mscz` / `.mscx` のみ対応なら不要 |
| aria の `score_render_edit_commands.cpp` ほか編集・再生・修復系 | 表示のみなら不要 |

`rw/read114` `rw/read206` `rw/read302` は旧フォーマット読み込みなので、対応する
最小フォーマットを決めてから落とすか残すかを判断する。

### 来歴の記録

取り込んだら**どの aria / MuseScore のコミットから持ってきたか**を必ず残す
（aria 自身が `sandbox/engraving/compat/ENGINE.md` で同じことをしている）。
でないと上流のバグ修正を当てられなくなる。`vendor/` 配下に取り込み元の
コミットハッシュと取得日を書いたマニフェストを置く。

### ライセンス

**aria / MuseScore は GPL-3.0-only。** そのソースをこのリポジトリに取り込むと、
**ビューアー全体の配布には GPL-3.0-only が適用される。** このリポジトリに podium のコードは
入らないので、**GPL-3.0-only で公開すればそれで完結する。**
ライセンス全文は `LICENSE.txt`、第三者通知は `THIRD_PARTY_NOTICES.md` に記載している。

AGPL が関わるのは podium に組み込む段階だけ。詳細は後述の「将来の podium 組み込み」。

## フォント名の解決方針

スコアは手元に無いフォントを名指しする（デモスコアは `Times New Roman` と
`FreeSerif` を指定している）。その解決規則をどうするか。

### 前提: 我々は改行の連鎖崩れを起こさない

一般の文書レンダラでは、置換フォントの字幅差で改行位置が変わり、以降の行送り・
ページ送りまで連鎖して崩れる。**我々ではこれが起きない。** MuseScore が歌詞・
タイトル・コードネームの位置をすべて計算して座標に焼き込み、SVG は `<text>` 1個が
1つの配置済みランになる。**ブラウザは折り返しを一切しない。**

実害は「ランの見た目の幅がずれて枠からはみ出す／隣と衝突する」に留まり、連鎖しない。
歌詞は1音節1ランなので影響は小さく、長いランほど目立つ。したがってメトリクス互換
フォントの価値は残るが、優先度は一般の文書レンダラより低い。

### Phase 2 の実測結果（2026-11-01）

`QFontProvider` の規約を FreeType に移植（`src/compatfontprovider.cpp`）した結果:

| スコア | 要素数 | 移植前 | 移植後 |
|---|---|---|---|
| goldberg（同梱フォントのみ） | 60,232 | 93% 移動 | **3%** |
| Amazing_grace（未同梱フォント指定） | 1,013 | 99% 移動 | 83% |

**残差はフォント置換の差であって、メトリクス移植のバグではない。** この開発機で
fontconfig が見えるのは11ファミリだけで、スコアが要求する `Times New Roman` も
`FreeSerif` も無い。

| 要求 | Qt の解決 | 我々の解決 |
|---|---|---|
| Edwin / Leland | 同名（MuseScore が `QFontDatabase` に登録） | 同じ ✓ |
| Times New Roman | **DejaVu Serif**（fontconfig 経由） | Edwin（既定） |
| FreeSerif | **DejaVu Serif** | Edwin |

`Times New Roman` の `horizontalAdvance` は Qt/我々で 1.0989 倍、`xHeight` は 1.1178 倍。
歌詞幅の差（63 対 55 = 1.145）と同オーダーで、**別フォントが使われている**ことの帰結。

**したがって Amazing_grace の 83% を Qt ベースラインに寄せてはいけない。**
それは開発機の fontconfig の偶然に合わせ込むことになる（Windows なら本物の
Times New Roman が見つかり、別の答えになる）。我々の決定論的フォールバックのほうが
環境に依らない点で望ましい。

goldberg の残り 3% も同じで、筆頭の `MeasureNumber` 1048件は `FreeSerif` 指定。
差は1単位（0.1譜表スペース）レベルで、`Note` は完全一致している。

**Phase 2 の達成基準は「同梱フォントだけを使うスコアで一致すること」**とする。
未同梱フォントの扱いは、下記のフォント解決方針の話に帰着する。

### `boundingRect(文字列)` の移植（2026-11-02）

Phase 2 で最後まで残していた項目。`QFontProvider` を移植したとき、**似て非なる2つを
同じインク外接矩形で実装していた**:

| | 中身 |
|---|---|
| `tightBoundingRect(文字列)` | インクの外接矩形。これは合っていた |
| `boundingRect(文字列)` | **行ボックス。** インクではない |

実測で規則を当てるのではなく、**Qt 6.10.2（参照ビルドが実際にリンクしている版）の
ソースを apt から取って読んだ**。`apt-get source qt6-base`（この環境で切られない唯一の
経路）。鎖を降りると答えは 1 関数に収束する:

```
QFontMetricsF::boundingRect(QString)        qfontmetrics.cpp:1504
  -> QTextEngine::boundingBox(0, len)       qtextengine.cpp:2334
       -> QFontEngine::boundingBox(glyphs)  qfontengine.cpp:611
```

```cpp
QFixed w;
for (i) w += glyphs.effectiveAdvance(i);
leftBearing  = firstLeftBearing(glyphs);
lastRightBearing(glyphs);
return glyph_metrics_t(leftBearing, -(ascent()),
                       w - leftBearing - rightBearing,
                       ascent() + descent(), w, 0);
```

つまり **x = 先頭グリフの左サイドベアリング、y = −ascent、幅 = アドバンスの総和から
両端のベアリングを引いたもの、高さ = ascent + descent。** インクの箱ではなく行の箱。
だから食い違っていたのが `.h` と `.y` だったことも説明がつく。

`QTextEngine` 側は複数のスクリプトアイテムを `qMin`/`qMax` で畳むが、
**1フォント・1アイテム（ここの呼び出し元は全部そう）では素通し**になる
（`glyph_metrics_t()` が x = y = 100000、残りを 0 で初期化するので、min が値を取り
max が幅と高さを取る）。`tightBoundingBox` との違いは、葉の
`QFontEngine::tightBoundingBox` を呼ぶかどうかだけ。

#### 結果

`mscz_metriccmp`（Qt と我々を 270 項目で直接比較）で:

| | 移植前 | 移植後 |
|---|---|---|
| 差分のある項目の合計 | 84 / 270 | **54 / 270** |
| `boundingRect.h` | 15 | **0** |
| `boundingRect.y` | 11 | **3** |

**同梱フォントでは Edwin が `boundingRect` 完全一致**、Leland は `.w` が 1 項目だけ
1.4% 残る（アウトライン対ラスタライズの 1 単位差が外ベアリング経由で出たもので、
Phase 2 で既知・許容と判断した `box(cp)` の差と同源）。残る `.y` 3 項目は
Times New Roman と FreeSerif、つまり**字体置換**の話。

レイアウトは変わっていない（wasm 対 native 0 差分、SVG バイト一致、Qt 比較も
Amazing_grace 83% / goldberg 3% のまま）。**予想どおり**で、理由は
この関数を使う要素 —— `Bend`（ベンドのラベル）・`FSymbol`・グリッサンドの文字 ——
が**検証に使っている譜面に 1 つも出てこない**こと。だから今まで検出されなかった。

#### 併せて直したもの: 比較ツール自身の誤り

`Leland Text` のプローブが U+E0A4（noteheadBlack）を要求していたが、
**LelandText.otf にそのグリフは無い**（`fc-query` で確認、SMuFL 域は 21 レンジのみで
符頭は含まない）。我々は「そんなグリフは無い」と 0 を返し、Qt は黙って別フォントに
フォールバックして 54.266 を返していた。つまり**この 11 項目の食い違いは我々が正しく、
プローブが間違っていた**（しかもこのフォールバックは `QFontProvider::inFont` が
避けようとしているまさにその挙動）。U+E261（accidentalNatural）に替えたら全項目一致した。

#### 残した逸脱（意図的）

Qt の `tightBoundingBox` は各グリフの右端・下端を `x.ceil()` / `y.ceil()` で
デバイスピクセルに切り上げるが、我々はしない。1200dpi では 1/20 ポイント未満で、
実測の 0.3% 一致はほぼこれ。**歌詞の bbox がここから来ており、Phase 2 のベースラインは
現在の挙動で検証済みなので、同じ変更にまとめず手を付けていない。**

### 本当の問題は解決経路が2本あること

- レイアウトのメトリクス → Qt の `QFontProvider`
- 埋め込む／名指しするフォント → `IFontsDatabase::fontPath()`（既定へフォールバック）

**この2つは独立に解決しており食い違い得る。** 置換表を整える前にここを一本化する。
Phase 2 で Qt を外せば provider が1つになり構造的に解消するので、
**置換表の導入は Phase 2 まで保留**する（今入れると二重管理になる）。

### Phase 2 で入れる形

解決を1箇所に集約し、5段階にする。

```
指定フォント → 同梱にある？ → あればそれ
            → 既知の名前？ → メトリクス互換フォント
            → 未知      → serif / sans / mono の分類から既定
```

同梱する Latin のメトリクス互換セット（数百KB級、等幅互換を設計目標にしたもの）:

| 指定 | 代替 |
|---|---|
| Times New Roman / Times | Liberation Serif |
| Arial / Helvetica | Liberation Sans |
| Courier New / Courier | Liberation Mono |
| Calibri | Carlito |
| Cambria | Caladea |
| その他 serif 系 | Noto Serif |
| その他 sans 系 | Noto Sans |

### CJK は別扱い

**Noto Sans CJK JP は1ウェイトで約16MB** で、サイズ最小化の要件と正面衝突する。
サブセット化されるのは*出力*だけで、**サブセット元のフルフォントは wasm 側に必要**。
よって「CJK を実際に使うスコアのときだけ遅延取得する」と割り切る。

### 未解決: 文字単位の glyph fallback

MuseScore の文字単位フォールバックは**音楽記号フォント用しか無い**
（`dom/textbase.cpp` の `missingChars` は欠落文字を音楽記号フォールバックで拾うだけ）。
`Times New Roman` 指定のランに日本語が混ざった場合の一般的なフォールバックは
engine に存在しない。置換表では解決せず engine 改造が必要なので、独立した課題。

### 採らなかった選択肢

テキストもパス化すれば描画は構造的に正確になり、フォント解決はレイアウトにしか
影響しなくなる（`GlyphOutlines` があるので実装は小さい）。**ただしテキスト選択が
できなくなる。** 歌詞を選択したいという要件と矛盾するため採らない。

## Phase 3 の実測結果（2026-11-01）

**ブラウザで動く。そしてレイアウトはネイティブと完全一致した。**

| | 要素数 | ネイティブ(Qt無し)との差 |
|---|---|---|
| Amazing_grace | 1,013 | **0** |
| goldberg | 60,232 | **0** |

さらに **1 ページ目の SVG はバイト単位で一致**する（下記の描画順の修正後）。

比較対象を Qt ビルドのベースラインではなく**ネイティブの Qt 無しビルド**にしているのは、
ここで問うているのが「emscripten の libc・32bit ポインタ・削った FreeType モジュール集合・
`HB_TINY` が何かを動かしていないか」という別の問いだから。答えは一切動いていない。
（Qt ビルドとの残差は Phase 2 で決着した字体置換の話で、それはそのまま残っている。）

検証は 2 本ある。

**`scripts/check-wasm.sh`** — emsdk 同梱の node で動かし、レイアウトと SVG をネイティブと
突き合わせる。レイアウトの一致を厳密に見るのはこちら。

**`scripts/check-wasm-chrome.sh`** — **実ブラウザ**で動かす。この distro に Chromium は
入らない（Ubuntu 26.04 は snap の移行パッケージしか持たず snapd が要る）し、
playwright のダウンロードもここでは通らない。が、**Windows に Chrome があり WSL は
Windows プロセスを起動できる**ので、そちらを使う。Chrome 自身の `--dump-dom` と
`--screenshot` で済むので、Windows 側に CDP クライアントも node も要らない。
WSL 内に bind したサーバは Windows から localhost で見えるので、
配信は WSL・読み込みは Windows で成立する。

実測（Amazing_grace）:

```
status line: 2 page(s), 149 KB of SVG, drawn in 0 ms
<svg> elements in the document: 2
<text> elements: 134      @font-face blocks: 4
text runs: 134, of which notation glyphs: 0, lyrics present: True
```

**最後の行が「記号はパス」という既定を決めた理由そのものの検証。** SMuFL の
コードポイントは私用領域（U+E000〜U+F8FF）にあるので、描画後のテキストにそれが
現れれば記譜が選択対象になっているということ。両モードを native で出して比べると:

| モード | テキストラン | うち記譜 |
|---|---|---|
| **path（既定）** | 94 | **0** |
| font | 404 | **310** |

font モードなら歌詞を選ぶときに記譜 310 ラン分がついてくる。**既定の選択は正しかった。**

はまりどころ: web の資材は `POST_BUILD` でコピーしていたので、ターゲットが再リンク
されないと**古いページが配信されたまま**になり、ブラウザが新しい URL パラメータを
無視する理由を探すことになった。`configure_file` に変えて configure 時の依存にした。

なお **`Bun.WebView` は使えなかった。** Windows に bun 1.3.14 があり
`navigate` / `evaluate` / `click` / `screenshot` と E2E に必要な API は揃っているが、
chrome バックエンドが `BUN_CHROME_PATH` でも `backend.path` でも
`ERR_DLOPEN_FAILED`（Chrome の場所ではなく bun 側のネイティブライブラリのロード失敗）。
webkit バックエンドは macOS 専用。操作を伴う E2E（ドラッグ＆ドロップ、チェックボックス、
実際の選択操作）をやるならここが直ってからになる。

ユーザ提供のサンプル 3 つ（`OverJoyed` 15ページ / `サウダージ` 2ページ / `サウダージ_demo` 23ページ）も
wasm で開いて描画できることを確認した。UTF-8 のファイル名も問題ない。
MuseJazz / MuseJazz Text は同梱し、Petaluma・Inkpen/Inkpen2・Reprise系もMuseJazz系で代替する。
CJKフォントはまだ同梱していないため、日本語等の字体・表示は別途対応が必要。

### 見つけた問題: 描画順が標準ライブラリで変わる

レイアウトが完全一致したのに **SVG はバイト一致しなかった**。調べると、
描画要素は 1 つ残らず同一で、違いは**順序**だけ（`<g>` のまとめ方が 4 つ分割れて +412 バイト）。

原因は `dom/engravingitem.cpp` の `elementLessThan` が同値を大量に返すこと
（z が同じ・選択状態が同じ・可視性が同じなら双方向に false）。そこへ
`rendering/score/paint.cpp` が **不安定ソートの `std::sort`** をかけていたため、
同値要素の並びが libstdc++ と libc++ で違う答えになっていた。

`std::stable_sort` に変えた。安定ソートなら順序は探索順になり、どの環境でも同じになる。
z が実際に違う要素には影響しない。**レイアウトのフィンガープリントはソート済みなので
この種の差を検出できない**ことも分かった（だから SVG のバイト比較に意味がある）。

### 速度

| | 起動 | 読込+レイアウト | SVG 描画 |
|---|---|---|---|
| Amazing_grace（2ページ） | 130ms | 109ms | 38ms |
| goldberg（120ページ） | 130ms | 2,221ms | 602ms |

ネイティブ（同一コード、Linux ファイルシステム上のリソース）で goldberg が 2.43 秒なので、
**wasm は実質ネイティブ速度**。

### 見つけた問題: リソースの再読み込みが 100 倍以上効く

ネイティブの probe が goldberg に **978 秒**かかっていた。wasm は 2.3 秒。原因は I/O で、
エンジンは**要求ごとにリソースファイルを読み直す** —— `FontsDatabase::fontData()` が
毎回ファイルへ戻る設計（それが遅延読み込みを遅延にしている）。リソースツリーが
`/mnt/c`（WSL から見た Windows ドライブ）にあると、同じフォントを何度も取りに行くことになる。

| | goldberg のフィンガープリント | CPU 時間 |
|---|---|---|
| リソースが `/mnt/c`、キャッシュなし | **978.26 秒** | 12.91 秒 |
| リソースが Linux FS、キャッシュなし | 2.43 秒 | 2.02 秒 |
| リソースが `/mnt/c`、`makeCachingProvider` 経由 | **5.98 秒** | 5.23 秒 |

**CPU 時間が 12.91 秒しかない**のが決定的。978 秒のうち 965 秒は I/O 待ちで、
計算は一切増えていない。

対処は `src/hostio.h` の `makeCachingProvider`。**キャッシュはホスト側に置くのが正しい**:
ブラウザ側のプロバイダはもともとバイト列をメモリに持っているので、これで両者の挙動が揃う。
大きなエントリ（既定 4MB 超）は素通しするので、**大きなスコアはこれまで通りストリームされる**。

### サイズ

| | raw | gzip | brotli |
|---|---|---|---|
| `mscz-engine.wasm` | 4,464KB | — | **1,290KB** |
| `mscz-engine.js`（emscripten のローダ） | 16KB | 6KB | 5KB |
| `mscz.js`（自作ラッパ） | 5KB | 2KB | 2KB |
| 必須リソース（30ファイル） | 2,675KB | 626KB | **522KB** |
| 任意リソース（6ファイル） | 2,757KB | 133KB | 108KB |

**初回ロードは brotli で約 1.73MB**（wasm + ローダ + 必須リソース）。2回目以降はキャッシュ。

wasm の数字は下記「依存の棚卸し」の 2 つの変更を含む。棚卸し前は raw 4,974KB /
gzip 1,745KB / brotli 1,327KB、内訳は CODE 4,016KB / DATA 934KB だった。

リソースが raw 2.7MB に対し brotli 522KB しかないのは、大半が JSON と XML で 5:1 近く縮むため。
**配信を brotli にするかどうかで初回ロードが 2MB 以上変わる**ので、ここは必ず圧縮すること。

### 4.5MB の大まかな分解（2026-11-01）

`scripts/wasm-size/breakdown.sh`。**LTO と unity を切ってリンカのマップを取る**のが要点。
LTO を有効にしたままでは関数が融合されて「どの merged body に入ったか」しか言えず、
それが下の関数単位リストで `__wasm_call_ctors` 583KB のような不透明な塊になる理由。
切ればソース1ファイル=オブジェクト1個になり、**出力の全バイトが入力に厳密に割り当たる**。

| | バイト | 割合 |
|---|---|---|
| **engraving（エンジン本体）** | 4,872KB | **68.1%** |
| **HarfBuzz（整形＋サブセッタ）** | 920KB | **12.9%** |
| リンカ生成データ | 388KB | 5.4% |
| FreeType | 313KB | 4.4% |
| libc++ | 242KB | 3.4% |
| muse/global | 179KB | 2.5% |
| 自作コード（SVG・hostio・フォント） | 77KB | 1.1% |
| muse/draw | 67KB | 0.9% |
| emscripten libc | 58KB | 0.8% |
| zlib | 20KB | 0.3% |

**注意: これは `wasm-opt` 前のリンカ出力**（合計 6,985KB に対し最終 5,195KB）。
デバッグ情報も含む。**割合を見るためのもので、絶対値は最終バイナリと一致しない。**

アーカイブはメンバをベース名で持つので（engraving には `rw/*/tread.cpp` が3つある）
エンジン内部はマップでは分解できない。ディスク上のオブジェクトで見る:

| | 割合 |
|---|---|
| engraving/dom | 41.4% |
| engraving/rendering/score | 10.3% |
| **engraving/editing** | **6.1%** |
| engraving/rw/read400 + read410 + read460（現行フォーマット） | 9.4% |
| engraving/types | 3.2% |
| engraving/style | 2.6% |
| engraving/compat | 2.0% |
| **engraving/rw/write** | **1.6%** |
| engraving/rw/read206 + read114 + rw/compat | 3.1% |

最大の単一ソース: `harfbuzz-subset.cc`（オブジェクト 2,251KB、**出力 916KB**）、
`dom/score.cpp` 890KB、`rw/read*/tread.cpp` ×3 で 2,458KB、`types/typesconv.cpp` 786KB、
`types/symnames.cpp` 722KB、`editing/edit.cpp` 664KB、`dom/chordlist.cpp` 653KB、
`style/styledef.cpp` 566KB。

### engraving の 68% は妥当か（ライブラリが混ざっていないか）

混ざっていない。`engraving` 配下に third-party は無く（`thirdparty` は framework 側だけ）、
中身は全て MuseScore 自身のコード。行数と出力の比:

| | 行数 | |
|---|---|---|
| engraving のビルド対象 `.cpp` | 239,385 | 333ファイル |
| 同 `.h` | 52,687 | |
| **計** | **292,072** | |

出力 4,872KB に対して **16.7 バイト/行**（`.cpp` だけで割れば 20.3）。
対照として HarfBuzz は `harfbuzz-subset.cc` が引き込む 44,055 行に対し 916KB で
**20.8 バイト/行** —— ほぼ同じ密度。

**つまりコード生成が膨らんでいるのではなく、29万行の譜面組版コードが本当にそれだけある。**
`-Oz` としては 16〜20 バイト/行は普通か、やや良いほうの数字。

### どこから手を付けるべきか

**先に 2 つ訂正。** どちらも「オブジェクトの大きさ」で見て間違えたもので、
**リンカが既に大半を落としているので、判断は出力への寄与で見なければならない。**

| 候補 | 出力バイト | 対 engraving | 対 全体 |
|---|---|---|---|
| **HarfBuzz のサブセッタ** | 328KB | — | **4.6%** |
| `rw`: 現行フォーマット読み込み（必須） | 487KB | 10.0% | 6.8% |
| **`engraving/editing` 全体** | 275KB | 5.7% | 3.9% |
| `rw`: 旧フォーマット読み込み（維持と決定） | 134KB | 2.7% | 1.9% |
| **`rw/write`（保存。ビューアでは不要）** | 128KB | 2.6% | 1.8% |
| **`rendering/single`（パレット用）** | 81KB | 1.7% | 1.1% |
| `rw/compat` | 41KB | 0.8% | 0.6% |

**訂正1: HarfBuzz は「1 スイッチで 13%」ではない。** `harfbuzz-subset.cc` は
`harfbuzz.cc` のほぼ上位集合なので、**同じ 1 ファイルに整形器（必須）とサブセッタが
同居している**。マップのシンボル単位で割ると整形器 592KB・**サブセッタ 328KB**。
つまり埋め込みをやめて得られるのは 920KB ではなく **328KB（全体の 4.6%）**。

**訂正2: `engraving/editing` は削除できない。** 移調がそこに実装されている。
`editing/transpose.cpp` の `Transpose::transpose()` が本体で、
`undoAddElement` / `undoChangeKeySig` / `undoChangeProperty` / `undoTransposeHarmony`
を呼び、aria の view-transpose は `startCmd()` / `endCmd()`（undo スタック）と
`cmdSelectAll()` で囲んでいる。パート譜切替も `Excerpt::createExcerptsFromParts` 経由で
`editing/editexcerpt.cpp` を使う。出力 275KB のうち
**`edit.cpp` 77KB ＋ `cmd.cpp` 41KB ＋ `undo.cpp` 17KB ＋ `transpose.cpp` 8KB = 143KB が
移調の通り道**で、これは外せない。

残る 132KB も素直には落ちない。`textedit.cpp` が残っているのは
`TextBase::startEdit` / `endEdit` という**仮想関数**が vtable から参照されるためで、
`--gc-sections` では消えない（消すには 176 個の `dom/*.cpp` で仮想関数を
`#ifdef` で消す必要がある）。`editchord.cpp` が残っているのは
括弧付き音符という**記譜機能**で、譜面を読むのに実際に使う。

### 落とせるものの実際の順位

| | 出力 | 状態 |
|---|---|---|
| 1. HarfBuzz のサブセッタ | **328KB** | 既定の SVG モードを変えるかの判断待ち |
| 2. `rw/write`（保存経路） | **128KB** | 表示専用なので不要。未着手 |
| 3. `rendering/single`（パレット用描画） | **81KB** | 未着手 |
| 4. FreeType の `TT_RunIns` ＋ gzip | 22KB | `ftoption-min.h` は書いたが未適用 |
| — `engraving/editing` | 275KB | **移調に必要。削除不可**（上記 訂正2） |
| — `rw`: 旧フォーマット読み込み | 134KB | **維持と決定** |
| — `types/symnames` `typesconv` | — | 必須部分のみ残っている（下記） |

1〜4 を全部やっても **559KB raw**、brotli では**その数分の 1**。
engraving 本体の 68% は 29万行のコードそのもので、ここは削るものではない。
**サイズを本気で下げるならコードではなくリソース側**（必須 522KB brotli）か、
機能分割して遅延読み込みする方向になる。

### `symnames.cpp` と `typesconv.cpp` は何で、必須なのか

**`types/symnames.cpp`（6,532行）は 2 本の並んだ配列**で、要素数はどちらも
`SymId::lastSym + 1` = 約 3,061（SMuFL の全シンボル）:

| 配列 | 中身 | 必須か |
|---|---|---|
| `s_symNames` | `constexpr` な `AsciiStringView` の配列。`"noteheadBlack"` 等の SMuFL 名 | **必須。** 譜面が `<sym>noteheadBlack</sym>` と名前で書いてくるので読むのに要るし、SMuFL メタデータからグリフを引くのにも要る。`constexpr` なのでポインタ表＋文字列バイトだけ |
| `s_symUserNames` | `TranslatableString` の配列。`"11 medium diesis down, 1°[17 31] 2°46 down, 1/4-tone down"` のような人間向け説明 | **不要。** インスペクタ・パレット・スクリーンリーダ専用 |

**`types/typesconv.cpp`（3,399行、テーブル 60 本）は enum ↔ XML 文字列の変換表。**
`"left"` `"center"` のような XML の属性値と enum を往復させるもので、
**`.mscx` を読むのに必須**。各要素が `AsciiStringView xml`（必須）＋
`TranslatableString` 2 個（UI 専用）という構造。

`style/styledef.cpp`（2,173行）はスタイルプロパティの定義表で、これも必須。

**だから 4 番は「消せる表」ではなく「必須の表と UI 専用の表が同じ構造体に同居している」
という形の問題**で、UI 専用のほうは `MSCZ_NO_TRANSLATABLE_STRINGS` で既に回収した。
残りを削るには表の表現自体を変えることになり、上流との差分が大きくなる。

FreeType（4.4%）の `TT_RunIns` や gzip を削る話は、この順位では後回しでよい。

### wasm のコードはどこに使われているか

`scripts/wasm-size/payload.sh` の実測（関数本体 4,006KB、11,304 関数）:

| 領域 | バイト | 関数数 |
|---|---|---|
| C とランタイム（うち `__wasm_call_ctors` が 583KB） | 1,171KB | 1,681 |
| `mu::engraving::dom` ほか | 956KB | 4,403 |
| `mu::engraving::rendering` | 592KB | 462 |
| HarfBuzz | 296KB | 1,112 |
| **`mu::engraving::read*`（旧フォーマット読み込み）** | **290KB** | 353 |
| libc++ のテンプレート実体化 | 208KB | 2,022 |
| FreeType | 180KB | 486 |
| 自作コード | 130KB | 133 |
| `muse`（global） | 63KB | 379 |
| **`mu::engraving::write`（保存。表示のみなら不要）** | **60KB** | 73 |
| `muse::draw` | 57KB | 193 |
| zlib | 3KB | 7 |

FreeType + HarfBuzz の 476KB raw は、単体計測の 216KB brotli とちょうど整合する。

注意: LTO は関数を融合するので、**単独の関数名への帰属は目安**。シンボルマップは融合後の
1 関数に名前を 1 つしか持たない（`SvgPaintProvider::endTarget` が 106KB と出るのはそれ）。
領域ごとの合計は信頼できる。

### 依存の棚卸し（2026-11-01 実測）

「機能を削るのではなく、例外/RTTI/iostream/ICU のような依存を切れないか」を測った
（`scripts/wasm-size/payload.sh`）。関数本体 4,008KB・DATA 934KB に対して:

| | バイト | 関数数 | |
|---|---|---|---|
| **ICU 等の unicode ライブラリ** | **0** | 0 | 使っていない。`muse::String` が自前 |
| **例外機構**（`__cxa_*`/unwind/invoke_） | **1,248** | 49 | **既に無効**。切る余地なし |
| **RTTI**（`__dynamic_cast`/type_info） | 6,651 | 193 | +typeinfo 文字列 25KB = 計 32KB |
| **iostream / locale / num_put** | 67,261 | 287 | 実在。ログと SVG 組み立てが呼び元 |
| `std::regex` | 48,309 | 192 | コード/テンポ/強弱の解析で実使用 |
| `std::function` | 18,176 | 398 | |
| pthread | 9,087 | 166 | スレッドは1本も動いていない |
| printf 系 | 7,145 | 8 | |
| malloc（emmalloc） | 1,664 | 5 | 既に最小 |

export している関数は 13 個（`mscz_*`）＋emscripten の内部 6 個だけで、無駄はない。

**つまり挙げた4つのうちサイズに効くのは iostream だけだった。** 一方で、挙げていない
ところに大きいものがあった:

| | バイト |
|---|---|
| `__wasm_call_ctors`（静的初期化） | 583,291（コードの 14.5%） |
| DATA 内の関数シグネチャ文字列（`LOG_TAG` = `CLASSFUNC`） | 119,929（1,249本） |
| ソースパス文字列 | 16,953 |

### 例外は「既に無効」で、それ自体がバグだった

`__cxa_throw` は **import**（JS 側で `abort()`）で、`__cxa_begin_catch` はバイナリに存在しない。
emscripten 既定で catch が無効なので、**`-fno-exceptions` を付けても 1.2KB しか減らない**。

問題は副作用。`global/types/string.cpp` の UTF-8/UTF-16 変換は throw する
`utf8::utf8to16` を try で囲んでいた。native では動くが wasm では throw が
`abort()` に落ちて `ABORT=true` になり、**以後の全呼び出しが失敗する**。実証:

```
不正バイト 0xFF 0xE0 0x80 を text 要素に注入した .mscz
  native : 1013 要素を描画して完了
  wasm   : Aborted(undefined) でモジュール全体が死亡
```

`utf8::replace_invalid` + unchecked 変換（U+FFFD 置換）に変更した。wasm も 1013 要素を
描画する。throw 版は最初の不正バイトで打ち切っていたので**挙動も改善**している。
UTF-16/32 側には utfcpp に相当物が無いので、孤立サロゲートと範囲外コードポイント用の
小さなサニタイザを書いた。

### 静的初期化 583KB の正体

`Item<T>`（enum↔文字列の変換テーブルの要素）が `TranslatableString` を 2 個持ち、その
コンストラクタが `String::fromUtf8` を呼ぶ。つまり**テーブルの全要素で UTF-16 変換と
ヒープ確保が main 前に走る**（`types/typesconv.cpp` だけでテーブル 60 個）。

このプロジェクトには UI が無く、読む側（インスペクタ・パレット・スクリーンリーダ）は
1 つもビルドしていない。`MSCZ_NO_TRANSLATABLE_STRINGS`（既定 ON）でメンバを残したまま
中身を詰めないようにした。全呼び出し箇所はそのままコンパイルでき、テーブルは
ただのポインタになる。

| | raw | brotli |
|---|---|---|
| 変更前 | 4,977KB | 1,327KB |
| **`MSCZ_NO_TRANSLATABLE_STRINGS`** | 4,863KB | **1,297KB**（−30KB） |
| ＋unity build（ビルド時間目的） | 4,464KB | 1,290KB（−6.5KB） |

**brotli では 30KB（2.3%）。** raw の 114KB に対して圧縮後が小さいのは、消えたのが
反復的な初期化コードだからで、圧縮が元々よく効いていた部分。
レイアウトは両デモ譜で 0 差分、1 ページ目の SVG もバイト一致。

**さらに踏み込んで失敗した記録。** 上記の変更ではメンバ `String str` を残しているので、
`String()` の `m_data = std::make_shared<std::u16string>()` が**要素ごとに 1 回**走ったまま
（`symnames.cpp` の配列だけで 3,061 要素）。これを消せば大きいと考えて、マクロ時には
メンバごと持たない別クラス定義にしてみた。結果:

| | raw | brotli | 起動 |
|---|---|---|---|
| メンバを残す（採用） | 4,464KB | 1,290KB | 180〜223ms |
| メンバも消す | 4,487KB（+23KB） | 1,287KB（−3.25KB） | 180〜223ms |

**brotli で 3.25KB しか減らず、起動時間はノイズ以下、raw はむしろ増えた。**
3,000 回のヒープ確保が高いという読みが外れていた（コンパイラは配列初期化を
小さなループにまとめるし、小さい確保 3,000 回は 1ms に届かない）。
クラスを二重定義する保守コストに見合わないので**戻した**。やり直さないこと。

### 次に削れる見込みのあるもの

1. **旧フォーマットの読み込み（`read114` / `read206` / `read302` …）約 290KB raw。**
   対応する最小フォーマットを決めれば落とせる
2. **`rw/write` 約 60KB raw。** 表示専用なので保存経路は不要
3. **`rendering/single` 約 50KB raw。** パレット用の単一要素描画
4. **`__wasm_call_ctors` 583KB。** 静的初期化がインライン展開されたもの。
   中身の調査が要る（engraving の静的テーブルが主と思われる）

### 必須と任意の線引き

最大のリソースは `harmony_to_diagram.xml` の 2.5MB だが、これは**任意**に分類した。
`dom/fret.cpp` が `FretDiagram` のレイアウト時に初めて読む遅延読み込みなので、
フレットダイアグラムの無い譜では一切触られない。

**フォントは同じようには遅延できない。** `FontsDatabase::addFont()` が登録時に
`io::File::exists(path)` を確認し、その登録は起動時に一度だけ行われるため、
起動前に渡っていなければそのファミリは登録されない。後から足すには
`addAdditionalFonts(dir)` の経路が要る（未実装）。

### 採った設計

- **リソースは push。** エンジンはページのレイアウト途中で同期的にフォントを要求するが、
  ブラウザのメインスレッドに同期 fetch は無い。だから JS が先に渡す
  （`mscz_resource_add`）。非同期性は JS 側に閉じ、エンジンは「名前を渡すとバイト列が返る」
  だけを知っている。
- **`-sFILESYSTEM=0`。** MEMFS も含めて一切載せない。Phase 2 でファイルアクセスを
  エンジンの外に出した結果がそのまま効いている。
- **embind ではなく素の C エクスポート。** embind は型レジストリと RTTI を持ち込む。
  実体は整数とポインタの十数個の関数なので `cwrap` で足りる。

### 当てられなかった最適化

**`-fno-rtti` は使えない。** `dynamic_cast` が 37 箇所あり、そのうち
`rendering/score/tlayout.cpp` `tdraw.cpp` `harmonylayout.cpp` は表示経路の中。
使うならエンジン側の改造が先。

### サードパーティの入手経路（重要）

**この環境では HTTPS のダウンロードが必ず末尾 32,768 バイトを失う。** ホストに依らず、
GitHub でも zlib.net でも同じで、欠けた末尾を `Range` で取り直そうとしても
切られた本文全体が返ってくる。したがって `emcc -sUSE_FREETYPE=1` も `-sUSE_ZLIB=1` も使えない。

**apt だけが無傷で通る。** そのため zlib は `apt-get source zlib` で取得している
（Ubuntu パッチ入り = CVE 修正入り）。`provision.sh` が deb-src を有効にする。

もう一点、HarfBuzz の `src/harfbuzz-subset.cc` は `src/harfbuzz.cc` の**相棒ではなく、
ほぼ上位集合**。両方リンクすると重複シンボルの山になる。欠けているのは FreeType 橋渡しの
`hb-ft.cc` だけなので、我々は `harfbuzz-subset.cc + hb-ft.cc` の 1 ライブラリにしている。

なお、FreeType のモジュールを削っても（`ftmodule-min.h`）**レイアウトは動かない**ことを
確認した（goldberg は Qt ビルド比 3% のまま、ディストロ版 FreeType と同値）。

## wasm サイズを小さく保つ方針

**サイズは最優先の制約**なので、Phase 2 の時点から「Phase 3 で積まないものは積まない」形で作る。

### 0. 最大の要因はコードではなくフォント（実測）

`src/engraving` は **14 個の `.qrc` でフォント 13 ファミリをライブラリに埋め込む**
（`src/engraving/data/fonts/*.qrc` → `fonts/`。ビルドログに
`qrc_fonts_Bravura.o` `qrc_fonts_Leland.o` … が並ぶ）。実測した埋め込み対象:

| | サイズ |
|---|---|
| FreeSerif / FreeSans 系 `.ttf`（テキストのフォールバック） | 約 7.5MB |
| bravura（`bravura_metadata.json` 768K を含む） | 2.6MB |
| petaluma | 1.5MB |
| mscore | 1.2MB |
| finalemaestro / musejazz / gootville / finalebroadway / campania | 計 約 1.9MB |
| **leland（MuseScore 4 の既定の音楽フォント）** | **380KB** |
| **edwin（既定のテキストフォント）** | **564KB** |
| smufl（`glyphnames.json` / `ranges.json`、`Smufl::init()` が必要） | 528KB |

**合計で約 15MB。** 既定スタイルの `.mscz` を表示するだけなら leland + edwin + smufl の
**約 1.5MB で足りる**。残りはスコアが明示的に指定したときだけ要るので、
**HTTP で遅延取得**する。つまりここだけで 10MB 以上削れる見込みで、
コード側のどの最適化より効く。

（`fonts/*.sfd` は FontForge のソースで埋め込まれていないため、上表には含めていない。）

### 1. aria のレンダーコアをそのまま持ってこない

`sandbox/engraving/score_render_core.cpp` は**全パーツを `#include` する単一の翻訳単位**で、
`Impl` クラス1つのメンバ関数が各ファイルに散っている（`score_render_runtime.cpp`,
`score_render_edit_commands.cpp`, `score_render_playback.cpp` …）。
**一部のファイルだけ持ってきても成立しない。**

そこで表示に必要な部分だけの薄いファサード（仮 `mscz_web_core.cpp`）を書き、
engraving の API を直接叩く。aria の `score_render_runtime.cpp` /
`score_render_open_save.cpp` / `score_render_pages.cpp` から必要なロジックを移植する形で
「表示機能の再利用」を実現する。これで以下が最初から入らない:

| 外せるもの | 規模 |
|---|---|
| FluidSynth + SoundFont 再生 + リバーブ | 大（再生はスコープ外） |
| MIDI 書き出し | 中 |
| `iex_musicxml`（MusicXML 入出力） | ソース 13MB 相当。`.mscz` / `.mscx` のみ対応なら不要 |
| `score_render_edit_commands.cpp`（編集） | 4753 行 |
| 破損スコア修復 | 中 |

### 2. ビルドスイッチ

- `MUSE_QT_SUPPORT=OFF` / `DRAW_NO_INTERNAL` — Qt を完全に落とす（Phase 2 の目的そのもの）
- `DRAW_NO_QSVGRENDER` — `svgrenderer.cpp`（QSvgRenderer）を落とす
- `ENGRAVING_NO_API=ON` — プラグイン API
- `MUE_BUILD_ENGRAVING_QML / TESTS / DEVTOOLS=OFF`
- `MUE_BUILD_ENGRAVING_PLAYBACK=OFF`

### 3. サードパーティを削る

- **FreeType**: 独自の `ftmodule.h` で TrueType + CFF + SFNT + smooth ラスタライザだけ残し、
  BDF / PCF / Type1 / CID / PFR / Windows FNT / PNG / bzip2 を落とす
- **HarfBuzz**: `HB_TINY` 系の設定。歌詞やタイトルの複雑テキスト整形にしか要らないので、
  落とせるかは要検証（音楽記号は単一グリフなので HarfBuzz 不要）
- **zlib**: `.mscz` は zip なので必須。小さい

### 4. emscripten のリンクフラグ

- `-Oz -flto`、`wasm-opt -Oz --strip-debug --strip-producers`
- `-sMALLOC=emmalloc`、`-sENVIRONMENT=web,worker`、`-sMODULARIZE -sEXPORT_ES6`、`--no-entry`
- `-sFILESYSTEM=0`（または最小の MEMFS のみ）
- **`-fno-rtti` を試す価値がある**。engraving は `EngravingItem::type()` + `isXxx()` の
  独自型システムを持つので `dynamic_cast` 依存が薄い可能性がある。効けば大きい
- 例外も同様に要計測（`-fno-exceptions` が通るか）
- 配信は brotli

### 5. フォントの積み方（上の「0.」の具体策）

登録の実体は `src/engraving/engravingmodule.cpp:170-240` の
`IFontsDatabase::addFont(FontDataKey, path)` で、全ファミリをここで列挙している。

**フォントの読み込みは既に遅延になっている。** `FontsDatabase` は `{key, path}` しか保持せず
(`fontsdatabase.h:49-52`)、実バイトは `fontData()` が要求時にファイルから読む
(`fontsdatabase.cpp:106-114`)。したがって:

- `.qrc` 経由の埋め込みをやめ、フォントを MEMFS 上の実ファイルにすれば、
  wasm バイナリにもプリロードにも入れずに済む
- ただし `addFont()` は登録時に `io::File::exists(path)` を確認する
  (`fontsdatabase.cpp:86`) ので、**取得してから登録**する必要がある。
  つまり「必要なフォントを判定 → JS が fetch して MEMFS に書く → 登録 → レンダリング」
  の2パス構成にする
- `addAdditionalFonts(dir)` がディレクトリ内の `fontslist.json` を読む仕組みを持っており
  (`fontsdatabase.cpp:152`)、後から取得したフォントを足す口として使える
- 実際に使う SMuFL レンジだけにサブセット化する
- `bravura_metadata.json` のような 768KB の JSON も埋め込み対象なので、
  使うフォントの metadata だけに絞る
- 「記号=パス化」の決定がここで効く: 記号を `<defs>` + `<use>` で出すなら
  **ブラウザ側には音楽フォントを一切送らなくて済む**（wasm 側だけがアウトライン取得に必要）

**注意: 既定とフォールバックは Leland ではなく Bravura。**
`setDefaultFont(Font::Type::MusicSymbol, u"Bravura")` と
`m_engravingfonts->setFallbackFont("Bravura")` になっている
（`.mscz` 側のスタイルは通常 Leland を指定するが、フォールバックが Bravura）。
Bravura は metadata を含めて 2.6MB あるので、**我々のビルドではフォールバックを Leland に
変える**。上流との意図的な差異なので、変更したら ENGINE 差分として記録すること。

### 6. 推測せず測る

`emcc --emit-symbol-map` + `wasm-opt --metrics`、または `twiggy` でサイズを帰属させる。
Phase 1 のホストビルドでも `llvm-size` でオブジェクトごとの大きさを見て当たりを付けられる。

## 将来の podium 組み込みについて（メモ）

podium は**完全に PDF 前提**のアーキテクチャ。pdf.js で描画し、pdf-lib で注釈・ブックマーク・
メタデータを **PDF ファイル自体に保存**している。したがって統合コストだけで言えば
「mscz → PDF」のほうが楽（既存のレイアウト・注釈・ページ送り機構がそのまま効く）。
SVG にすると podium 側に新しいビューア経路が必要になる。SVG / PDF のどちらも
`IPaintProvider` 実装なので、Phase 2 以降で PDF 版を足すことは可能。

**ライセンス**: aria / MuseScore は GPL-3.0-**only**、podium は AGPL-3.0。

両者を混ぜること自体は明示的に許可されている。GPLv3 §13 と AGPLv3 §13（第2段）が
相手とのリンク・結合を相互に許可しており、**各パートは元のライセンスのまま**になる。
ただし GPLv3 §13 は「ネットワーク越しの対話に関する **AGPLv3 §13 の特別要件は結合物
そのものに適用される**」と定めている。

したがって:

- **結合物を「GPL-3.0」として公開することはできない。** AGPL §13 の義務が落ちてしまう
- 正しい形は「GPL-3.0-only と AGPL-3.0 の両方を同梱し、結合物には AGPL §13 が及ぶ」
- AGPLv3 → GPLv3 の再ライセンスも不可。GPL-3.0-only なので AGPL への格上げも不可だが、
  **§13 は格上げを要求していない**ので支障はない

**結合したバイナリの公開は明示的に許可されている。** 両 §13 の文言が
"...into a single combined work, **and to convey the resulting work**" となっている。
条件は通常の GPL/AGPL 義務のみ: 両パートの完全な対応ソースを提供し、ライセンス表示を
保持し、結合物について AGPL §13 のソース提供機会を示すこと。

### ディレクトリ分けについて

**1リポジトリ内でディレクトリを分け、各所に LICENSE を置き、トップレベルに一覧を書けば
要件としては十分。** ライセンスはディレクトリ構成を問題にしない。問われるのは
「どのファイルがどのライセンスか明確か」と「受領者が完全な対応ソースを得られるか」だけ。
MuseScore 自身も 1 リポジトリに `thirdparty/` を各種ライセンスのまま同梱している。

**ただしディレクトリを分けても「単なる集積（mere aggregation）」にはならない。**
1つのアプリとしてリンク・呼び出す以上、構成がどうあれ結合著作物。分離の価値は
法的な盾ではなく実務上のもの:

- どのコードがどのライセンスか監査できる
- 各パートの対応ソースを切り出して提供しやすい
- MuseScore 由来ファイルをうっかり AGPL ヘッダで上書きしない

実際に効くのは **MuseScore 由来ファイルのヘッダ
（`SPDX-License-Identifier: GPL-3.0-only` / `MuseScore-CLA-applies`）を
改変・除去しないこと。**

（法務の確認は別途必要。ここはライセンス本文に即した整理）

## 開発環境（WSL2: mscz_dev）

### このチェックアウトでの開発再開

Windows の `G:\work\20261029\podium\podium` は WSL から
`/mnt/g/work/20261029/podium/podium` として使う。ソースはここを直接編集し、
ビルド生成物と ccache は Linux 側のホームディレクトリに置く。

```powershell
wsl -d mscz_dev --cd /mnt/g/work/20261029/podium/podium
```

開いた WSL シェルで:

```bash
# Qt 付きの参照ビルド
bash scripts/build-host.sh

# Qt 非依存のネイティブ版
bash scripts/build-noqt.sh

# WebAssembly の配信用ビルド
bash scripts/build-wasm.sh

# ブラウザで確認: http://localhost:8000
python3 -m http.server 8000 --bind 127.0.0.1 --directory ~/build/mscz-wasm/web
```

生成先はそれぞれ `~/build/mscz-host`、`~/build/mscz-noqt`、
`~/build/mscz-wasm`。Qt 非依存版と wasm のビルドログは生成先と同名の
`.log` ファイル（例: `~/build/mscz-wasm.log`）に残る。
既存の比較スクリプトが参照する `aria/demos` はこのリポジトリに同梱されていない。
比較するときは別途譜面を用意し、`ARIA_DIR` でその `demos` の親を指定する。

#### Windows の Bun + Edge で E2E を実行

`C:\Tools\bun.exe`（確認時 1.4.4）の `Bun.WebView` と既存の Edge を使う。
Windows では WebView2 ではなく CDP 経由で Edge を操作する。
標準の pipe 起動はこの環境で `Chrome process closed the pipe` になるため、
専用の headless Edge を起動し、自動割当されたローカルポートに接続する。
API の仕様は [Bun WebView 公式ドキュメント](https://bun.com/docs/runtime/webview) を参照。

このリポジトリのルートで PowerShell から:

```powershell
# WSL のビルド成果物を Windows 側の検証用ディレクトリへコピー
wsl -d mscz_dev -- bash -lc 'mkdir -p /mnt/g/work/20261029/podium/podium/build/web; cp -r /home/dev/build/mscz-wasm/web/. /mnt/g/work/20261029/podium/podium/build/web/'
& C:\Tools\bun.exe scripts/check-wasm-bun.ts
```

既定では `../sample/*.mscz` をすべて読み込み、ページ数・SVG 描画・
ファイル入力・記号とフォント埋め込みの切替・DPI 変更を検証する。
スクリーンショットと結果 JSON は `build/out/bun-e2e/` に保存する。
`--web DIR`、`--samples DIR`、`--browser EXE` でパスを変更できる。
パス描画モードでは、通常の `drawSymbol` に加え、本文やコードネームの
`drawText` に含まれる音楽記号もパス化する。未同梱の MuseJazz Text の旧コード記号は
`chords_jazz.xml` / `chords_std.xml` の対応に従って同じ意味の記号へ置換し、
`sus` / `add` は選択可能な文字列にする。通常の文章は `<text>` のまま残す。
検証では全ページの `notationText` がゼロであることを要求し、二重登録を含む
すべての console error と未捕捉例外を失敗として扱う。
`--baselines DIR` を渡すと、その中の `<譜面名>.wasm.fingerprint` とも比較する。

初期化時は `GlobalModule::setApplication` でアプリケーションを渡し、標準モジュールの
サービス登録後、import の解決前にファイルシステムとフォントプロバイダを明示的に
差し替える。重複登録が拒否される副作用で実装を選ぶことはしない。
ネイティブの回帰テストは WSL から次のように実行できる（Qt 付きは `mscz-host` に変更）:

```bash
cmake --build ~/build/mscz-noqt --target mscz_svg_symbols_test
~/build/mscz-noqt/src/mscz_svg_symbols_test
```

Windows 側には何も入れない。C++ ツールチェーンと Qt6 は distro 内の apt から、それ以外は
distro 内の **mise** から入る（`mise registry` に cmake / ninja / emsdk / python がある。
gcc と Qt6 は mise に無いので apt）。

### 構築

```powershell
powershell -ExecutionPolicy Bypass -File scripts\wsl\New-MsczDevDistro.ps1
```

| 操作 | コマンド |
|---|---|
| 作り直す（破壊してから再構築） | `... New-MsczDevDistro.ps1 -Force` |
| プロビジョニングだけやり直す | `... New-MsczDevDistro.ps1 -ProvisionOnly` |
| 破棄するだけ | `... New-MsczDevDistro.ps1 -DestroyOnly` |
| 傍受プロキシが無い環境 | `... New-MsczDevDistro.ps1 -CorporateCA Never`（通常は不要、下記参照） |
| シェルを開く | `wsl -d mscz_dev` |
| 1 コマンド実行 | `wsl -d mscz_dev -- bash -lc 'cmake --version'` |

distro は**いつでも捨てて作り直せる**前提。中に大事なものを置かないこと。
`provision.sh` は冪等なので何度流しても同じ状態になる。

- `scripts/wsl/New-MsczDevDistro.ps1` — Windows 側。distro の登録・破棄・CA書き出し
- `scripts/wsl/provision.sh` — distro 内。apt → TLS 判定 → ユーザ作成 → mise → ツール導入
- `scripts/build-host.sh` — distro 内。Phase 1 のホストビルド
- `scripts/bench-build.sh` — distro 内。ビルド時間の条件比較（下記）

`/etc/wsl.conf` で `appendWindowsPath=false` にしてあるので、Windows の PATH は distro に
漏れてこない。

### ビルド時間（2026-11-01 実測）

`.ninja_log` に各ステップの実時間が残るので、まずそれを見た。**コンパイルが 98%、
リンクが 2%**（wasm で 11,876 秒 対 270 秒）。だから LTO リンクをどうにかしても無駄で、
効くのは「重い翻訳単位を 470 個コンパイルしている」ところだけ。

`scripts/bench-build.sh` で条件を直列に測った（`muse_global`、90 TU、基準 29 秒）:

| 条件 | 時間 | 対基準 |
|---|---|---|
| baseline（ソースが `/mnt/c`、`-j22`） | 29 s | — |
| ソースを Linux FS にコピー | 23 s | −21% |
| `-j11` | 31 s | +7% |
| **unity build** | 17 s | **−41%** |
| ccache 1回目（投入） | 37 s | +28% |
| **ccache 2回目** | 1 s | **−97%** |

フルの wasm ビルド（clean から）での確認:

| | 時間 | wasm raw | wasm brotli |
|---|---|---|---|
| baseline | **435 s** | 4,863KB | 1,297KB |
| unity build | **204 s** | 4,464KB | 1,290KB |
| unity + ccache 温 | **88 s** | 同 | 同 |

**unity を既定にした。** 470 TU が **121 TU** になり、出力は変わらない
（両デモ譜のレイアウトがネイティブと完全一致、1ページ目の SVG もバイト一致）。
raw が 399KB 縮むが brotli では 6.5KB しか変わらないので、**サイズの改善ではなく
ビルド時間の改善**として扱う。ccache も見つかれば自動で使う（`provision.sh` が入れる）。

はまりどころ3つ:

- **`/mnt/c` の影響はコンパイルでは 2 割**。リソース読み込みでは 400 倍だったが
  （前述の 978 秒）、コンパイルは CPU 律速なので桁は変わらない。コピー運用の
  陳腐化リスクに見合わないと判断した
- **`-j11` はむしろ遅い。** メモリ 15GB でも 22 並列で詰まってはいない
- **PCH は使えない。** `MuseCreateModule` が `muse_pch` を `REUSE_FROM` する作りで、
  それは aria の `buildscripts/pch` にあり我々は vendor していない。
  `MUSE_COMPILE_USE_PCH` を非 FORCE にすると上流の既定 ON が漏れて configure が
  失敗するので、自前の `MSCZ_USE_PCH` から FORCE している

**正しさの確認に `-Oz -flto` は要らない。** `build-wasm.sh --quick` が最適化なしの
別ディレクトリに作る。サイズを引用する数字は既定ビルドからのみ取ること。

### ベース distro は Ubuntu 26.04 でなければならない

**aria は Qt >= 6.8 を要求する。** `buildscripts/cmake/SetupQt6.cmake` が
`find_package(Qt6 6.8 REQUIRED)` で、`src/framework/global/io/internal/filesystem.cpp` が
Qt 6.8 で入った `QDirListing` を使う（`sandbox/engraving/CMakeLists.txt` にある
`find_package(Qt6 6.3)` は実態より緩い）。

| Ubuntu | apt の Qt6 | |
|---|---|---|
| 24.04 LTS (noble) | 6.4.2 | ビルド不可 |
| 25.04 (plucky) | 6.8.3 | 可 |
| 25.10 (questing) | 6.9.2 | 可 |
| **26.04 LTS (resolute)** | **6.10.2** | **既定。aria の top-level CMakeLists の "Min. required by Qt 6.10" と一致** |

### TLS 傍受プロキシの有無（両対応）

既定の `-CorporateCA Auto` では:

1. `New-MsczDevDistro.ps1` が Windows の `LocalMachine\Root` と `LocalMachine\CA` を
   PEM に書き出す（ローカル処理なので安い）
2. `provision.sh` が **HTTPS を実際に叩いて確かめる**
   - 通れば、書き出した CA は**入れない**（素の trust store のまま）
   - 通らなければ Windows の root を `update-ca-certificates` で導入して再確認

つまり傍受プロキシのある環境でも無い環境でも、同じコマンドで通る。
`-CorporateCA Never` にすれば書き出し自体を行わない。

書き出しは**特定の発行者を探さず machine trust store をまるごと**出すので、どの傍受環境でも
同じ仕組みで動く（書き出し先は `%TEMP%` で、リポジトリには入らない）。

なお **apt は `http://archive.ubuntu.com` と平文で話すので通ってしまい**、失敗はもっと後の
HTTPS 取得で初めて出る。そのため `provision.sh` は apt の直後に HTTPS を検査する。

mise は Rust 製で独自の trust store を持ちうるため、CA 導入時は
`SSL_CERT_FILE` / `SSL_CERT_DIR` / `CURL_CA_BUNDLE` / `REQUESTS_CA_BUNDLE`（emsdk の Python 用）/
`NODE_EXTRA_CA_CERTS` / `CARGO_HTTP_CAINFO` も `/etc/profile.d/10-corporate-ca.sh` で設定する。

### はまりどころ

- **`wsl.exe` の引数はバックスラッシュを落とす。** Git Bash / PowerShell のどちらから呼んでも
  起きる。`wslpath` に Windows パスを渡すのは不可（`C:work20260809...` になる）ので、
  パス変換は PowerShell 側で `/mnt/c/...` を組み立てている。distro に渡す中身は
  **引数ではなくファイル経由**にすること。
- **Git Bash は `/mnt/c/...` を Windows パスに変換してしまう。** `MSYS_NO_PATHCONV=1` を付ける。
- **Ubuntu の `.bashrc` は非対話シェルで早期 return する**ので、`wsl -- bash -lc '...'` では
  そこに書いた `mise activate` が効かない。mise の shims を
  `/etc/profile.d/20-mise.sh` で PATH に入れている。

### Linux ホストビルドに必要な追加フラグ

aria のホストビルドは macOS でしか行われていないため、Linux では
`MUSE_APP_INSTALL_PREFIX` / `MUSE_APP_INSTALL_NAME` が未定義でコンパイルが落ちる
（`muse_framework_config.h` の `#cmakedefine` 経由で
`GlobalConfiguration::resolveAppDataPath()` の Linux 分岐が使う。
`sandbox/engraving/CMakeLists.txt` は Android のときだけ設定している）。
`scripts/build-host.sh` で `-D` して回避している。レンダーコアはリソースを埋め込むので、
この値が指すパスは実際には使われない。

### Windows 側で何かが必要になったら

Windows には mise だけが入っている。何か足すときは mise 経由で入れ、使うときは必ず
`mise exec --` を明示する。

```powershell
mise exec -- <command>
```
