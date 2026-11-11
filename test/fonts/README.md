# PDF test font

`NotoSansJP-test.otf` is a test-only subset of Noto Sans JP Regular from
Rostrum revision `b5a00adda871d441ce6274e760c0ea67f7045fb7`,
`lib/NotoSansJP-Regular.otf`. See `OFL.txt`. It includes Latin characters,
kana and the Japanese characters needed by `test/pdf.musicxml`, with unused
glyphs to verify that PDF export subsets again. It is not a runtime asset.

Reproduce with fontTools (replace `SOURCE.otf` with the upstream font):

```sh
mise exec -- python -c "from fontTools import subset; f=subset.load_font('SOURCE.otf', subset.Options()); s=subset.Subsetter(); s.populate(unicodes=list(range(32,592))+list(range(0x3040,0x3100))+list(map(ord,'日本語の歌詞'))); s.subset(f); f.save('test/fonts/NotoSansJP-test.otf')"
```
