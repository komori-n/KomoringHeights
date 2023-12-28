<!-- markdownlint-disable -->

English follows Japanese.

## 変更点

- マルチスレッド探索のサポート
- MultiPV機能のサポート
- シングルスレッド探索性能の改善
- 「不詰」を-4000手詰（内部的な-∞）ではなく-9999手詰と表示するよう修正
- 不詰のとき玉方応手を1手出力
    - 攻方 ▲XXX が不詰のとき、玉方の最善応手のひとつ △YYY を表示する
- コードのクリーンアップ

## Changes

- Add multithreading support
- Add MultiPV feature
- Improve single-thread performance
- Use `mate -9999` instead of `mate -4000` for nomate position
- Clean up code
