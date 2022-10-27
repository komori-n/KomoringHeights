#ifndef KOMORI_TTV3_TRANSPOSITION_TABLE_HPP_
#define KOMORI_TTV3_TRANSPOSITION_TABLE_HPP_

#include <vector>

#include "node.hpp"
#include "repetition_table.hpp"
#include "ttv3entry.hpp"
#include "ttv3query.hpp"
#include "typedefs.hpp"

namespace komori {
namespace ttv3 {
namespace detail {
/// USI_Hash のうちどの程度を NormalTable に使用するかを示す割合。
constexpr inline double kNormalRepetitionRatio = 0.95;

/**
 * @brief 詰将棋エンジンの置換表本体
 * @tparam Query クエリクラス。単体テストしやすいように、外部から注入できるようにテンプレートパラメータにする。
 *
 * 置換表は、大きく分けて通常テーブル（NormalTable）と千日手テーブル（RepetitionTable）に分けられる。
 * 通常テーブルは大部分の探索結果を保存する領域で、探索中局面の pn/dn 値および証明済／反証済局面の
 * 詰み手数を保存する。一方、千日手テーブルはその名の通り千日手局面を覚えておくための専用領域である。通常テーブルは
 * 経路に依存しない探索結果を覚えているのに対し、千日手テーブルは経路依存の値を覚えるという棲み分けである。
 *
 * ## 実装詳細
 *
 * 詰将棋エンジンは、本将棋エンジンと比べ、探索結果 Look Up の際により多くの局面を調べる必要がある。
 * これは、局面の優等性／劣等性と呼ばれる性質を活かすためで、これがないと探索性能が大きく劣等してしまう。
 *
 * 通常テーブルの実現方法についてはこれまで様々な方法を試してきたが、現在は「クラスタ」という単位で探索結果エントリを
 * 管理している。以下ではクラスタの実現方法について簡単に説明する。
 *
 * まず、通常テーブルは `entries_` という配列で管理している。この配列の一部を切り出してクラスタを作る。
 * クラスタは、`Cluster::kSize` （10~30ぐらい） 個の連続領域に保存されたエントリで構成される。
 * それぞれの局面に対し、盤面ハッシュ値に基づいてクラスタが一意に決まる。（詳しくは `ClusterOf()` を参照）
 * 違う局面同士が同じクラスタに割り当てられる可能性があるし、クラスタ間で領域が重複する可能性もある。
 *
 * ```
 *          0                                                                     m_clusters_          entries_.size()
 * entries_ |                   |<- overlap ->|                                        |                     |
 *                     ^^^^^^^^^^^^^^^^^^^^^^^                                         ^^^^^^^^^^^^^^^^^^^^^^^
 *                     |<- Cluster::kSize  ->|                                         |<- Cluster::kSize -->|
 *                         cluster for n1                                                  cluster for n3
 *                              ^^^^^^^^^^^^^^^^^^^^^^^
 *                              |<- Cluster::kSize  ->|
 *                                 cluster for n2
 * ```
 *
 * クラスタに対する値の読み書きは、クエリ（Query）クラスにより実現される。クラスタの読み書きに必要な情報は
 * 盤面ハッシュや持ち駒などの探索が進んでも不変な値で構成されているので、クエリを使い回すことで
 * 探索性能を向上させることができる。1回1回のクエリ構築はそれほど重い処理ではないように見えるかもしれないが、
 * 実際には同じエントリに何回も読み書きする必要があるので、有意に高速化できる。
 *
 * また、置換表読み書きは詰将棋エンジン内で頻繁に呼び出す処理のため、現局面だけでなく1手進めた局面のクエリの
 * 構築することができる。（`CreateChildQuery()`）
 *
 * このクラスでは、`Query` をテンプレートパラメータ化している。これは、単体テスト時に `Query` をモック化して
 * 外部から注入するためのトリックである。本物の `Query` は外部から状態を観測しづらく、Look Up してみなければ
 * 内部変数がどうなっているかが把握できない。単体テストで知りたいのは `Query` のコンストラクト引数だけであるため、
 * `Query` だけ別物に差し替えて中身をチェックできるようにしている。
 */
template <typename Query>
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
   * 置換表サイズが `hash_size_mb` 以下になるようにする。置換表は大きく分けて NormalTable と RepetitionTable に
   * 分けられるが、それらの合計サイズが `hash_size_mb`[MB] を超えないようにする。
   */
  void Resize(std::uint64_t hash_size_mb) {
    const auto new_bytes = hash_size_mb * 1024 * 1024;
    const auto normal_bytes = static_cast<std::uint64_t>(static_cast<double>(new_bytes) * kNormalRepetitionRatio);
    const auto rep_bytes = new_bytes - normal_bytes;
    // 通常テーブルに保存する要素数。最低でも `Cluster::kSize + 1` 以上になるようにする
    const auto new_num_entries = std::max(static_cast<std::uint64_t>(Cluster::kSize + 1), normal_bytes / sizeof(Entry));
    // 千日手テーブルに保存する要素数。最低でも 1 以上になるようにする
    // 千日手テーブルは `std::unordered_set` により実現されているので、N 個のエントリを保存するためには
    // 2.5N * sizeof(Key) バイトが必要になる。（環境依存）
    // そのため、少し余裕を見て `rep_bytes / sizeof(Key)` を3で割った値を上限として設定する。
    const auto rep_num_entries = std::max(decltype(rep_bytes){1}, rep_bytes / 3 / sizeof(Key));

    cluster_head_num_ = new_num_entries - Cluster::kSize;
    entries_.resize(new_num_entries);
    entries_.shrink_to_fit();
    rep_table_.SetTableSizeMax(rep_num_entries);
    NewSearch();
  }

  /**
   * @brief 以前の探索結果をすべて消去する。
   */
  void NewSearch() {
    for (auto& entry : entries_) {
      entry.SetNull();
    }
    rep_table_.Clear();
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

    auto cluster = ClusterOf(board_key);
    return {rep_table_, cluster, path_key, board_key, hand, depth};
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

    auto cluster = ClusterOf(board_key);
    return {rep_table_, cluster, path_key, board_key, hand, depth};
  }

  /**
   * @brief 生のハッシュ値からクエリを構築する
   * @param board_key 盤面ハッシュ値
   * @param or_hand  持ち駒
   * @param path_key 経路ハッシュ値（default: kNullKey）
   * @return クエリ
   * @note 二重カウント検出用
   */
  Query BuildQueryByKey(Key board_key, Hand or_hand, Key path_key = kNullKey) {
    auto cluster = ClusterOf(board_key);
    const auto depth = kDepthMax;
    return {rep_table_, cluster, path_key, board_key, or_hand, depth};
  }

  /**
   * @brief 置換表使用率（千分率）を計算する
   * @return 置換表使用率（千分率）
   * @note 未実装
   */
  std::int32_t Hashfull() const {
    // not implemented
    return 0;
  }

  /**
   * @brief ガベージコレクションを実行する
   * @return 削除されたエントリ数
   * @note 未実装
   */
  std::size_t CollectGarbage() {
    // not implemented
    return 0;
  }

  // <テスト用>
  // 外部から内部変数を観測できないと厳しいので、直接アクセスできるようにしておく。
  // ただし、書き換えられてしまうと面倒なので、必ずconstをつけて渡す。

  /// 通常テーブルの先頭
  constexpr auto begin() const noexcept { return entries_.cbegin(); }
  /// 通常テーブルの末尾
  constexpr auto end() const noexcept { return entries_.cend(); }
  // </テスト用>

 private:
  /**
   * @brief `board_key` に対応するクラスタを取得する
   * @param board_key 盤面ハッシュ
   * @return `board_key` に対応するクラスタ
   *
   * @note クラスタの下位32ビットをもとにクラスタの位置を決定する
   */
  Cluster ClusterOf(Key board_key) {
    static_assert(sizeof(Key) == 8);

    // Stockfish の置換表と同じアイデア。少し工夫をすることで mod 演算を回避できる。
    // hash_low が [0, 2^32) の一様分布にしたがうと仮定すると、idx はだいたい [0, cluster_head_num_)
    // の一様分布にしたがう。
    const Key hash_low = board_key & Key{0xffff'ffffULL};
    auto idx = (hash_low * cluster_head_num_) >> 32;
    return {&entries_[idx]};
  }

  /**
   * @brief 通常エントリの本体。
   *
   * クエリの初期化時にクラスタを渡す必要があるため、サイズは必ず `Cluster::kSize + 1` 以上。
   */
  std::vector<Entry> entries_ = std::vector<Entry>(Cluster::kSize + 1);
  /**
   * @brief 現在確保しているエントリにうちクラスタ先頭にできる個数。
   *
   * 必ず1以上で、`entries_.size() - kSize` に一致する。
   */
  std::size_t cluster_head_num_{1};
  /// 千日手テーブル
  RepetitionTable rep_table_{};
};
}  // namespace detail

/**
 * @brief 置換表の本体。詳しい実装は `detail::TranspositionTableImpl` を参照。
 */
using TranspositionTable = detail::TranspositionTableImpl<Query>;
}  // namespace ttv3
}  // namespace komori

#endif  // KOMORI_TTV3_TRANSPOSITION_TABLE_HPP_
