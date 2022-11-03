/**
 * @file transposition_table.hpp
 */
#ifndef KOMORI_TRANSPOSITION_TABLE_HPP_
#define KOMORI_TRANSPOSITION_TABLE_HPP_

#include <vector>

#include "board_key_hand_pair.hpp"
#include "node.hpp"
#include "repetition_table.hpp"
#include "ttentry.hpp"
#include "ttquery.hpp"
#include "typedefs.hpp"

namespace komori::tt {
namespace detail {
/// USI_Hash のうちどの程度を NormalTable に使用するかを示す割合。
constexpr inline double kNormalRepetitionRatio = 0.95;

/**
 * @brief Hashfull（ハッシュ使用率）を計算するために仕様するエントリ数。大きすぎると探索性能が低下する。
 *
 * ## サンプル数の考察（読まなくていい）
 *
 * N 個のエントリのうち K 個が使用中の状況で、この N 個から無作為に n 個のエントリを調べたとき、
 * 使用中のエントリの個数 X は超幾何分布
 *     P(X=k) = (K choose k) * ((N - K) choose (n - k)) / (N choose n)
 * に従う。この確率変数 X は
 *     n * K / N
 * で分散
 *     n * K * (N - K) * (N - n) / N^2 / (N - 1)
 * である。よって、1 < n << N のとき、
 *     Y := X / n
 * とおくと、Y は平均
 *     K / N =: p    # ハッシュ使用率
 * で分散
 *     K * (N - K) * (N - n) / N^2 / (N - 1) / n
 *     = p * (1 - p) * (1 - n/N) / (1 - 1/N) / n
 *     ~ p * (1 - p) / n
 * となる。よって、n=10000 のとき、Y が K/N ± 0.01 の範囲の値を取る確率は少なく見積もってだいたい 95 %ぐらいとなる。
 * 探索中の詰将棋エンジンのハッシュ使用率の値が 1% ずれていても実用上ほとんど影響ないと考えられるので、
 * 実行速度とのバランスを考えて n=10000 というのはある程度妥当な数字だと言える。
 */
constexpr std::size_t kHashfullCalcEntries = 10000;
/// エントリを消すしきい値。
constexpr std::size_t kGcThreshold = Cluster::kSize - 1;
/// GCで消すエントリ数
constexpr std::size_t kGcRemoveElementNum = 6;

/**
 * @brief [begin, end) の範囲内で使用中のエントリのうち最も探索量が小さいエントリを返す
 * @param begin エントリの探索開始位置
 * @param end  探索終了位置（`end` 自体は含まず）
 * @return 見つけたエントリ
 */
inline Entry* GetMinAmountEntry(Entry* begin, Entry* end) noexcept {
  Entry* ret = nullptr;
  for (auto itr = begin; itr != end; itr++) {
    if (!itr->IsNull()) {
      if (ret == nullptr || ret->Amount() > itr->Amount()) {
        ret = itr;
      }
    }
  }

  return ret;
}

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
    // 4N * sizeof(Key) バイト程度が必要になる。（環境依存）
    const auto rep_num_entries = std::max(decltype(rep_bytes){1}, rep_bytes / 4 / sizeof(Key));

    cluster_head_num_ = new_num_entries - Cluster::kSize;
    entries_.resize(new_num_entries);
    entries_.shrink_to_fit();
    rep_table_.SetTableSizeMax(rep_num_entries);
    NewSearch(true);
  }

  /**
   * @brief 以前の探索結果をすべて消去する。
   */
  void NewSearch(bool force_clean = false) {
    if (force_clean || Hashfull() >= 50) {
      for (auto& entry : entries_) {
        entry.SetNull();
      }
      rep_table_.Clear();
    }
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
   * @param key_hand_pair 盤面ハッシュ値と持ち駒のペア
   * @param path_key 経路ハッシュ値（default: kNullKey）
   * @return クエリ
   * @note 二重カウント検出用
   */
  Query BuildQueryByKey(BoardKeyHandPair key_hand_pair, Key path_key = kNullKey) {
    const auto [board_key, hand] = key_hand_pair;
    auto cluster = ClusterOf(board_key);
    const auto depth = kDepthMax;
    return {rep_table_, cluster, path_key, board_key, hand, depth};
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
    const auto normal_hash_rate = GetNormalTableHashRate();
    const auto rep_hash_rate = rep_table_.HashRate();
    // 通常テーブル : 千日手テーブル = kNormalRepetitionRatio : 1 - kNormalRepetitionRatio
    const auto hash_rate = kNormalRepetitionRatio * normal_hash_rate + (1 - kNormalRepetitionRatio) * rep_hash_rate;
    return static_cast<std::int32_t>(1000 * hash_rate);
  }

  /**
   * @brief ガベージコレクションを実行する
   * @return 削除されたエントリ数
   *
   * 通常テーブルおよび千日手テーブルのうち、必要なさそうなエントリの削除を行う。
   */
  void CollectGarbage() {
    rep_table_.CollectGarbage();
    RemoveUnusedEntries();
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
   * @brief 通常テーブルのメモリ使用率を見積もる。
   * @return メモリ使用率（通常テーブル）
   *
   * `entries_` の中からいくつかのエントリをサンプリングしてメモリ使用率を求める。
   *
   * @note `entries_` の最初と最後の `Cluster::kSize` 個の要素はクラスタ同士のオーバーラップが小さいので、
   * 他の領域と比べて使用される確率が低い。
   */
  double GetNormalTableHashRate() const noexcept {
    // entries_ の最初と最後はエントリ数が若干少ないので、真ん中から kHashfullCalcEntries 個のエントリを調べる
    const std::size_t begin_idx = Cluster::kSize;
    const std::size_t count_range_size = cluster_head_num_ - begin_idx;

    std::size_t used_count = 0;
    std::size_t idx = begin_idx;
    for (std::size_t i = 0; i < kHashfullCalcEntries; ++i) {
      if (!entries_[idx].IsNull()) {
        used_count++;
      }

      // 連続領域をカウントすると偏りが出やすくなってしまうので、大きめの値を足す。
      idx += 334;
      if (idx > cluster_head_num_) {
        idx -= count_range_size;
      }
    }

    return static_cast<double>(used_count) / kHashfullCalcEntries;
  }

  /**
   * @brief 通常テーブルの中で、メモリ使用率が高いクラスタのエントリを間引く
   *
   * `kGcThreshold` 個以上のエントリが使用中のクラスタに対し、エントリの削除を行う。別の言い方をすると、
   * この関数呼び出し終了後は、すべてのクラスタの使用中エントリ数は `kGcThreshold` 個未満にする。
   */
  void RemoveUnusedEntries() {
    // start_idx から始まる cluster の末尾のインデックスを返す
    auto cluster_end = [&](std::ptrdiff_t start_idx) {
      return static_cast<std::ptrdiff_t>(std::min(entries_.size(), start_idx + Cluster::kSize));
    };
    // idx から Cluster::kSize 個の要素のうち使用中であるものの個数を数える
    auto count_used = [&](std::ptrdiff_t start_idx) {
      const auto end_idx = cluster_end(start_idx);
      const auto used_count = std::count_if(entries_.begin() + start_idx, entries_.begin() + end_idx,
                                            [](const Entry& entry) { return !entry.IsNull(); });
      return static_cast<std::size_t>(used_count);
    };

    std::ptrdiff_t start_idx = 0;
    auto used_in_range = count_used(0);
    do {
      if (used_in_range >= kGcThreshold) {
        // [start_idx, cluster_end(start_idx)) の使用率が高すぎるのでエントリを適当に間引く
        for (std::size_t k = 0; k < kGcRemoveElementNum; ++k) {
          auto start_itr = entries_.begin() + start_idx;
          auto end_itr = entries_.begin() + cluster_end(start_idx);
          auto min_element = GetMinAmountEntry(&*start_itr, &*end_itr);
          min_element->SetNull();
        }
        start_idx = cluster_end(start_idx);
        used_in_range = count_used(start_idx);
      } else {
        // しゃくとり法で used_in_range の更新をしておく
        if (!entries_[start_idx].IsNull()) {
          used_in_range--;
        }

        if (!entries_[cluster_end(start_idx)].IsNull()) {
          used_in_range++;
        }
        start_idx++;
      }
    } while (cluster_end(start_idx) < static_cast<std::ptrdiff_t>(entries_.size()));
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
}  // namespace komori::tt

#endif  // KOMORI_TRANSPOSITION_TABLE_HPP_
