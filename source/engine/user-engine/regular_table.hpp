/**
 * @file regular_table.hpp
 */
#ifndef KOMORI_REGULAR_TABLE_HPP_
#define KOMORI_REGULAR_TABLE_HPP_

#include <vector>

#include "ttentry.hpp"
#include "ttquery.hpp"
#include "typedefs.hpp"

namespace komori::tt {
namespace detail {
/// TT をファイルへ書き出す最低の探索量。探索量の小さいエントリを書き出さないことでファイルサイズを小さくする。
constexpr inline SearchAmount kTTSaveAmountThreshold = 10;
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
}  // namespace detail

/**
 * @brief 経路に依存しない探索結果を記録する置換表。
 *
 * 通常テーブルは `entries_` という配列で管理している。この配列の一部を切り出してクラスタを作る。
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
 */
class RegularTable {
 public:
  /// Default constructor(default)
  RegularTable() = default;
  /// Copy constructor(delete)
  RegularTable(const RegularTable&) = delete;
  /// Move constructor(default)
  RegularTable(RegularTable&&) noexcept = default;
  /// Copy assign operator(delete)
  RegularTable& operator=(const RegularTable&) = delete;
  /// Move assign operator(default)
  RegularTable& operator=(RegularTable&&) noexcept = default;
  /// Destructor(default)
  ~RegularTable() = default;

  /**
   * @brief 要素数が `num_entries` 個になるようにメモリの確保・解法を行う
   * @param num_entries 要素数
   */
  void Resize(std::uint64_t num_entries) {
    // 通常テーブルに保存する要素数。最低でも `Cluster::kSize + 1` 以上になるようにする
    num_entries = std::max<std::uint64_t>(num_entries, Cluster::kSize + 1);

    cluster_head_num_ = num_entries - Cluster::kSize;
    entries_.resize(num_entries);
    entries_.shrink_to_fit();

    Clear();
  }

  /**
   * @brief 以前の探索結果をすべて消去する。
   */
  void Clear() {
    for (auto&& entry : entries_) {
      entry.SetNull();
    }
  }

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
  double CalculateHashRate() const noexcept {
    // entries_ の最初と最後はエントリ数が若干少ないので、真ん中から kHashfullCalcEntries 個のエントリを調べる
    const std::size_t begin_idx = Cluster::kSize;
    const std::size_t count_range_size = cluster_head_num_ - begin_idx;

    std::size_t used_count = 0;
    std::size_t idx = begin_idx;
    for (std::size_t i = 0; i < detail::kHashfullCalcEntries; ++i) {
      if (!entries_[idx].IsNull()) {
        used_count++;
      }

      // 連続領域をカウントすると偏りが出やすくなってしまうので、大きめの値を足す。
      idx += 334;
      if (idx > cluster_head_num_) {
        idx -= count_range_size;
      }
    }

    return static_cast<double>(used_count) / detail::kHashfullCalcEntries;
  }

  /**
   * @brief 通常テーブルの中で、メモリ使用率が高いクラスタのエントリを間引く
   *
   * `kGcThreshold` 個以上のエントリが使用中のクラスタに対し、エントリの削除を行う。別の言い方をすると、
   * この関数呼び出し終了後は、すべてのクラスタの使用中エントリ数は `kGcThreshold` 個未満にする。
   */
  void CollectGarbage() {
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
      if (used_in_range >= detail::kGcThreshold) {
        // [start_idx, cluster_end(start_idx)) の使用率が高すぎるのでエントリを適当に間引く
        for (std::size_t k = 0; k < detail::kGcRemoveElementNum; ++k) {
          auto start_itr = entries_.begin() + start_idx;
          auto end_itr = entries_.begin() + cluster_end(start_idx);
          auto min_element = detail::GetMinAmountEntry(&*start_itr, &*end_itr);
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
   * @brief エントリをできるだけ手前の方に移動させる（コンパクション）
   */
  void CompactEntries() {
    for (auto& entry : entries_) {
      if (entry.IsNull()) {
        continue;
      }

      auto cluster = ClusterOf(entry.BoardKey());
      if (cluster.head_entry != &entry) {
        decltype(cluster.head_entry) candidate = nullptr;
        if (cluster.head_entry->IsNull()) {
          candidate = cluster.head_entry;
        } else if ((cluster.head_entry + 1)->IsNull()) {
          candidate = cluster.head_entry + 1;
        }

        if (candidate != nullptr) {
          *candidate = entry;
          entry.SetNull();
        }
      }
    }
  }

  /**
   * @brief 置換表の中身をバイナリ出力ストリーム `os` へ出力する
   * @param os バイナリ出力ストリーム
   * @return `os`
   *
   * 現在の通常探索エントリのうち、探索量が detail::kTTSaveAmountThreshold
   * を超えるものをバイナリ出力ストリームへと書き出す。
   * 探索量の小さなエントリを書き出さないことで、出力サイズを小さくすることができる。
   *
   * 書き出す情報は以下のような構造になっている。
   *
   * - 書き出すエントリ数(8 bytes)
   * - エントリ本体(sizeof(Entry) * n bytes)
   *
   * なお、`os` がバイナリモードではない場合、書き込みを行わないので注意。
   */
  std::ostream& Save(std::ostream& os) {
    auto should_save = [](const Entry& entry) {
      return !entry.IsNull() && entry.Amount() > detail::kTTSaveAmountThreshold;
    };
    std::uint64_t used_entries = std::count_if(entries_.begin(), entries_.end(), should_save);
    os.write(reinterpret_cast<const char*>(&used_entries), sizeof(used_entries));

    for (const auto& entry : entries_) {
      if (should_save(entry)) {
        os.write(reinterpret_cast<const char*>(&entry), sizeof(Entry));
      }
    }

    return os;
  }

  /**
   * @brief バイナリ入力ストリーム `in` から置換表エントリを読み込む。
   * @param is バイナリ入力ストリーム
   * @return `is`
   *
   * `Save()` で書き出した情報を置換表へ読み込む。ストリームから読み込んだエントリを挿入するイメージであるため、
   * `Save()` 時の置換表サイズより現在のサイズが小さくても問題なく動作する。
   *
   * @see Save()
   */
  std::istream& Load(std::istream& is) {
    std::uint64_t used_entries{};
    is.read(reinterpret_cast<char*>(&used_entries), sizeof(used_entries));

    for (std::uint64_t i = 0; i < used_entries && is; ++i) {
      Entry entry;
      is.read(reinterpret_cast<char*>(&entry), sizeof(entry));

      auto cluster = ClusterOf(entry.BoardKey());
      auto itr = cluster.head_entry;
      for (std::uint64_t j = 0; j < Cluster::kSize; ++j, ++itr) {
        if (itr->IsNull()) {
          *itr = entry;
          break;
        }
      }
    }

    return is;
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
   * @brief 通常エントリの本体。
   *
   * クエリの初期化時にクラスタを渡す必要があるため、サイズは必ず `Cluster::kSize + 1` 以上。
   */
  std::vector<Entry> entries_;

  /**
   * @brief 現在確保しているエントリにうちクラスタ先頭にできる個数。
   *
   * 必ず1以上で、`entries_.size() - kSize` に一致する。
   */
  std::size_t cluster_head_num_{1};
};
}  // namespace komori::tt

#endif  // KOMORI_REGULAR_TABLE_HPP_
