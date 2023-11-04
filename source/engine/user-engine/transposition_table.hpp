/**
 * @file transposition_table.hpp
 */
#ifndef KOMORI_TRANSPOSITION_TABLE_HPP_
#define KOMORI_TRANSPOSITION_TABLE_HPP_

#include <vector>

#include "board_key_hand_pair.hpp"
#include "node.hpp"
#include "regular_table.hpp"
#include "repetition_table.hpp"
#include "ttentry.hpp"
#include "ttquery.hpp"
#include "typedefs.hpp"

namespace komori::tt {
namespace detail {
/// USI_Hash のうちどの程度を RegularTable に使用するかを示す割合。
constexpr inline double kRegularRepetitionRatio = 0.85;

/**
 * @brief 詰将棋エンジンの置換表本体
 * @tparam Query クエリクラス。単体テストしやすいように、外部から注入できるようにテンプレートパラメータにする。
 * @tparam RegularTable 通常テーブル。単体テストしやすいように、外部から注入できるようにテンプレートパラメータにする。
 * @tparam RepetitionTable 千日手テーブル。
 *                         単体テストしやすいように、外部から注入できるようにテンプレートパラメータにする。
 *
 * 置換表は、大きく分けて通常テーブル（RegularTable）と千日手テーブル（RepetitionTable）に分けられる。
 * 通常テーブルは大部分の探索結果を保存する領域で、探索中局面の pn/dn 値および証明済／反証済局面の
 * 詰み手数を保存する。一方、千日手テーブルはその名の通り千日手局面を覚えておくための専用領域である。通常テーブルは
 * 経路に依存しない探索結果を覚えているのに対し、千日手テーブルは経路依存の値を覚えるという棲み分けである。
 *
 * 置換表に対する値の読み書きは、クエリ（Query）クラスにより実現される。置換表の読み書きに必要な情報は
 * （盤面ハッシュや持ち駒など）探索が進んでも不変な値で構成されているので、クエリを使い回すことで
 * 探索性能を向上させることができる。1回1回のクエリ構築はそれほど重い処理ではないように見えるかもしれないが、
 * 実際には同じエントリに何回も読み書きする必要があるので、有意に高速化できる。
 *
 * また、置換表読み書きは詰将棋エンジン内で頻繁に呼び出す処理のため、現局面だけでなく1手進めた局面のクエリの
 * 構築することができる。（`CreateChildQuery()`）
 *
 * このクラスでは、Query, RegularTable, RepetitionTable をテンプレートパラメータ化している。これは、単体テスト時に
 * 外部から注入するためのトリックである。本物のクラスは外部から状態を観測しづらく、Look Up してみなければ
 * 内部変数がどうなっているかが把握できない。単体テストで知りたいのは参照クラスのメンバ関数が正しく呼ばれているかどうか
 * なので、別物に差し替えて中身をチェックできるようにしている。
 */
template <typename Query, typename RegularTable, typename RepetitionTable>
class TranspositionTableImpl {
 public:
  /// Default constructor(default)
  constexpr TranspositionTableImpl() = default;
  /// Copy constructor(delete)
  constexpr TranspositionTableImpl(const TranspositionTableImpl&) = delete;
  /// Move constructor(default)
  constexpr TranspositionTableImpl(TranspositionTableImpl&&) noexcept = delete;
  /// Copy assign operator(delete)
  constexpr TranspositionTableImpl& operator=(const TranspositionTableImpl&) = delete;
  /// Move assign operator(default)
  constexpr TranspositionTableImpl& operator=(TranspositionTableImpl&&) noexcept = delete;
  /// Destructor(default)
  ~TranspositionTableImpl() = default;

  /**
   * @brief 置換表サイズを `hash_size_mb` に変更し、書かれていた内容をすべて消去する。
   * @param hash_size_mb 新しい置換表サイズ（MB）
   * @pre `hash_size_mb >= 1`
   *
   * 置換表サイズが `hash_size_mb` 以下になるようにする。置換表は大きく分けて RegularTable と RepetitionTable に
   * 分けられるが、それらの合計サイズが `hash_size_mb`[MB] を超えないようにする。
   */
  void Resize(std::uint64_t hash_size_mb) {
    const auto new_bytes = hash_size_mb * 1024 * 1024;
    const auto regular_bytes = static_cast<std::uint64_t>(static_cast<double>(new_bytes) * kRegularRepetitionRatio);
    const auto rep_bytes = new_bytes - regular_bytes;
    // 通常テーブルに保存する要素数
    const auto new_num_entries = regular_bytes / sizeof(Entry);
    // 千日手テーブルはキー1個あたり 16 bytes 使用する。
    const auto rep_table_size = std::max(decltype(rep_bytes){1}, rep_bytes / 16);

    regular_table_.Resize(new_num_entries);
    repetition_table_.Resize(rep_table_size);
  }

  /**
   * @brief 新しい探索を始める
   *
   * 探索の開始前に必ず呼び出される。千日手テーブルについては、前回の探索結果を流用するのは難しいのですべて
   * 消してしまう。一方、通常探索エントリは削除にも時間がかかるのでそのまま残しておく。
   *
   * 通常探索エントリを完全に消去したい場合は Clear() を使うこと。
   *
   * @see Clear
   */
  void NewSearch() { repetition_table_.Clear(); }

  /**
   * @brief 以前の探索結果をすべて消去する。
   */
  void Clear() {
    regular_table_.Clear();
    repetition_table_.Clear();
  }

  /**
   * @brief 局面 `n` のエントリを読み書きするためのクエリを返す
   * @param n 現局面
   * @return クエリ
   */
  Query BuildQuery(const Node& n) {
    const auto board_key = n.Pos().state()->board_key();
    const auto path_key = n.GetPathKey();
    const auto hand = n.OrHand();
    const auto depth = n.GetDepth();

    auto cluster = regular_table_.PointerOf(board_key);
    return {repetition_table_, cluster, path_key, board_key, hand, depth};
  }

  /**
   * @brief 局面 `n` を `move` で 1 手進めたエントリを読み書きするためのクエリを返す
   * @param n 現局面
   * @param move 次の手
   * @return クエリ
   *
   * `BuildQuery()` と比較して、`n.DoMove()` により局面を動かさなくてもクエリを構築できるため高速に動作する。
   */
  Query BuildChildQuery(const Node& n, Move move) {
    const auto board_key = n.Pos().board_key_after(move);
    const auto path_key = n.PathKeyAfter(move);
    const auto hand = n.OrHandAfter(move);
    const auto depth = n.GetDepth() + 1;

    auto cluster = regular_table_.PointerOf(board_key);
    return {repetition_table_, cluster, path_key, board_key, hand, depth};
  }

  /**
   * @brief 生のハッシュ値からクエリを構築する
   * @param key_hand_pair 盤面ハッシュ値と持ち駒のペア
   * @param path_key 経路ハッシュ値（default: kNullKey）
   * @return クエリ
   * @note 二重カウント検出用
   */
  Query BuildQueryByKey(BoardKeyHandPair key_hand_pair, Key path_key = kNullKey) {
    const auto [board_key, hand] = key_hand_pair;
    auto cluster = regular_table_.PointerOf(board_key);
    const auto depth = kDepthMax;
    return {repetition_table_, cluster, path_key, board_key, hand, depth};
  }

  /**
   * @brief 置換表使用率（千分率）を計算する
   * @return 置換表使用率（千分率）
   *
   * 置換表全体のメモリ使用率を計算する。置換表は通常テーブルと千日手テーブルに分かれているため、それぞれの
   * メモリ使用率を重み付けして足し合わせることで全体のメモリ使用量を見積もる。
   *
   * 通常テーブルのメモリ使用率を計算する際、置換表のサンプリングを行う。そのため、結果の計算にはそこそこの
   * 計算コストがかかる。1秒に1回程度なら問題ないが、それ以上の頻度で呼び出すと性能劣化につながるので注意すること。
   */
  std::int32_t Hashfull() const {
    const auto regular_hash_rate = regular_table_.CalculateHashRate();
    const auto rep_hash_rate = repetition_table_.HashRate();
    // 通常テーブル : 千日手テーブル = kRegularRepetitionRatio : 1 - kRegularRepetitionRatio
    const auto hash_rate = kRegularRepetitionRatio * regular_hash_rate + (1 - kRegularRepetitionRatio) * rep_hash_rate;
    return static_cast<std::int32_t>(1000 * hash_rate);
  }

  /**
   * @brief ガベージコレクションを実行する
   * @return 削除されたエントリ数
   *
   * 通常テーブルおよび千日手テーブルのうち、必要なさそうなエントリの削除を行う。
   */
  void CollectGarbage(double gc_removal_ratio) { regular_table_.CollectGarbage(gc_removal_ratio); }

  /**
   * @brief 置換表の中身をバイナリ出力ストリーム `os` へ出力する
   * @param os バイナリ出力ストリーム
   * @return `os`
   *
   * 千日手テーブルについては書き出しを行わない。なぜなら、探索開始局面が同一でなければ再利用しづらい情報であり、
   * 多くの詰将棋では千日手テーブルなしでもそれほど時間がかからず詰み手順を復元できるためである。
   */
  std::ostream& Save(std::ostream& os) { return regular_table_.Save(os); }

  /**
   * @brief バイナリ入力ストリーム `in` から置換表エントリを読み込む。
   * @param is バイナリ入力ストリーム
   * @return `is`
   */
  std::istream& Load(std::istream& is) { return regular_table_.Load(is); }

  /**
   * @brief  置換表に保存可能な要素数の概算値を取得する。
   * @return 置換表に保存可能な要素数（概算値）
   */
  std::uint64_t Capacity() const noexcept { return regular_table_.Capacity(); }

  // <テスト用>
  // 外部から内部変数を観測できないと厳しいので、直接アクセスできるようにしておく。

  /// 通常テーブルを直接取得する
  auto& GetRegularTable() { return regular_table_; }
  /// 千日手テーブルを直接取得する
  auto& GetRepetitionTable() { return repetition_table_; }
  // </テスト用>

 private:
  /// 通常テーブル
  RegularTable regular_table_{};
  /// 千日手テーブル
  RepetitionTable repetition_table_{};
};
}  // namespace detail

/**
 * @brief 置換表の本体。詳しい実装は `detail::TranspositionTableImpl` を参照。
 */
using TranspositionTable = detail::TranspositionTableImpl<Query, RegularTable, RepetitionTable>;
}  // namespace komori::tt

#endif  // KOMORI_TRANSPOSITION_TABLE_HPP_
