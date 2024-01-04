![Make CI (MSYS2 for Windows)](https://github.com/komori-n/KomoringHeights/workflows/Make%20CI%20(MSYS2%20for%20Windows)/badge.svg?event=push)
![Make CI (MinGW for Windows)](https://github.com/komori-n/KomoringHeights/workflows/Make%20CI%20(MinGW%20for%20Windows)/badge.svg?event=push)
![Make CI (for Ubuntu Linux)](https://github.com/komori-n/KomoringHeights/workflows/Make%20CI%20(for%20Ubuntu%20Linux)/badge.svg?event=push)
![Make CI (for Mac)](https://github.com/komori-n/KomoringHeights/workflows/Make%20CI%20(for%20Mac)/badge.svg?event=push)
![NDK CI (for Android)](https://github.com/komori-n/KomoringHeights/workflows/NDK%20CI%20(for%20Android)/badge.svg?event=push)

weak df-pnブランチ（開発中断）

df-pn+ではなくweak df-pnを使ったらどうなるか試してみた。
最適化は不十分なのでまだ高速化できる余地があるかも。

- メリット
  - `PnDn` を `uint64_t` から `uint16_t` にできて、メモリ消費量を大幅に減らせる
  - δ値の計算や合駒の遅延展開、二重カウント対策をすべてなくせるのでコードがシンプルになる
- デメリット
  - やや嘘解法っぽさがある（特に子の個数をδに加えているあたり）
  - シングルスレッド性能・マルチスレッド性能が少し落ちる？

---

# KomoringHeights

[English](README.en.md)

KomoringHeights は、df-pn+アルゴリズムを用いた[やねうら王](https://github.com/yaneurao/YaneuraOu/)ベースの詰将棋エンジンです。
局面の優劣関係、厳密な千日手検出、局面の合流検出と二重カウント回避など、詰将棋特有の探索技法が実装されており、
詰み／不詰の判定を高速に行うことができます。

KomoringHeights 本体は `source/engine/user-engine` 以下に格納されています。
それ以外はほぼすべてやねうら王由来のファイルとなっています。

## How to use

[Releases](https://github.com/komori-n/KomoringHeights/releases) からお使いのOSに合ったバイナリをダウンロードしてください。
KomoringHeightsを動かすには、[将棋所](http://shogidokoro.starfree.jp/)、[ShogiGUI](http://shogigui.siganus.com/)、
[Electron将棋](https://sunfish-shogi.github.io/electron-shogi/)、 [ShogiDroid](http://shogidroid.siganus.com/)などのUSIプロトコルに対応したGUIを利用してください。

USIプロトコルの検討機能（`go ...`）および詰将棋解答機能（`go mate ...`）の両方に対応しています。
使用するGUIソフトの仕様に応じて使い分けてください。

詳細なエンジンオプションについては [EngineOptions](source/engine/user-engine/docs/EngineOptions.txt) を参照してください。

## How to Build

ソースからビルドするには以下のコマンドを使用します。

```sh
git clone https://github.com/komori-n/KomoringHeights.git
cd KomoringHeights/source
make normal TARGET_CPU=AVX2 COMPILER=clang++
```

お使いのCPUに合わせて `TARGET_CPU=` の部分を書き換えてください。
`TARGET_CPU` に指定可能な文字列の一覧は `source/Makefile` を参照してください。

また、ビルドには C++17 対応のコンパイラが必要になります。
GCC 11、Clang 14 より新しいコンパイラのみ動作確認をしています。
これらより古い環境ではサポート対象外となるのでご注意ください。

## References

詰将棋エンジンを作る上で参考にした文献等については[References](source/engine/user-engine/docs/refs.md)を参照してください。

## 開発者向けドキュメント

Doxygenから自動生成したドキュメントは以下を参照してください。

- <https://komori-n.github.io/komoring-heights-docs/index.html>

## Contributing

バグの報告や機能要望などはIssueへお願いします。
Pull Requestも大歓迎です。Pull Requestを作成する場合、事前に `.pre-commit-config.yaml` の pre-commit チェックが通ることを確認してください。

## ライセンス

Licensed under GPLv3.

KomoringHeights実装本体は `source/engine/user-engine` に格納されています。
それ以外のファイルについては [やねうら王](https://github.com/yaneurao/YaneuraOu/) のライセンスを併せてご確認ください。

KomoringHeightsを使用してコンピュータ将棋大会等へ参加する場合、改変の有無にかかわらず事前に許可を取る必要はありません。
もし使用していただける場合、アピール文書等で言及していただけると嬉しいです。
