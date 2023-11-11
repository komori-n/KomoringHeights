/**
 * @file regular_table.hpp
 */
#ifndef KOMORI_REGULAR_TABLE_HPP_
#define KOMORI_REGULAR_TABLE_HPP_

#include <algorithm>
#include <shared_mutex>
#include <vector>

#include "ttentry.hpp"
#include "typedefs.hpp"

namespace komori::tt {
/**
 * @brief `[begin_ptr, end_ptr)` の循環配列を指すポインタを管理するクラス。
 *
 * ポインタが範囲外へ移動したら `[begin_ptr, end_ptr)` の範囲内に移し直すだけのクラス。理論的には `Entry*` でなく
 * 一般のイテレータに対して実装可能で、これ自体をイテレータ化（イテレータ要件を満たすようにメンバ定義）することも
 * できるが、実装がとても面倒になるので必要になったら作ることにする。
 */
class CircularEntryPointer {
 public:
  /**
   * @brief コンストラクタ
   * @param curr_ptr  現在のポインタ位置
   * @param begin_ptr 区間の先頭
   * @param end_ptr   区間の末尾
   * @pre curr_ptr ∈ [begin_itr, end_ptr)
   */
  constexpr CircularEntryPointer(Entry* curr_ptr, Entry* begin_ptr, Entry* end_ptr) noexcept
      : curr_ptr_{curr_ptr}, begin_ptr_{begin_ptr}, end_ptr_{end_ptr} {}
  /// Default constructor(default)
  CircularEntryPointer() = default;
  /// Copy constructor(default)
  constexpr CircularEntryPointer(const CircularEntryPointer&) noexcept = default;
  /// Move constructor(default)
  constexpr CircularEntryPointer(CircularEntryPointer&&) noexcept = default;
  /// Copy assignment operator(default)
  constexpr CircularEntryPointer& operator=(const CircularEntryPointer&) noexcept = default;
  /// Move assignment operator(default)
  constexpr CircularEntryPointer& operator=(CircularEntryPointer&&) noexcept = default;
  /// Destructor(default)
  ~CircularEntryPointer() = default;

  /**
   * @brief ポインタを1つ進める
   * @return 新しいポインタ
   */
  constexpr CircularEntryPointer& operator++() noexcept {
    ++curr_ptr_;
    if (curr_ptr_ == end_ptr_) {
      curr_ptr_ = begin_ptr_;
    }

    return *this;
  }

  /**
   * @brief ポインタを1つ戻す
   * @return 新しいポインタ
   */
  constexpr CircularEntryPointer& operator--() noexcept {
    if (curr_ptr_ == begin_ptr_) {
      curr_ptr_ = end_ptr_ - 1;
    } else {
      --curr_ptr_;
    }

    return *this;
  }

  /// ポインタをdereferenceする
  constexpr Entry& operator*() noexcept { return *curr_ptr_; }
  /// ポインタをdereferenceする
  constexpr const Entry& operator*() const noexcept { return *curr_ptr_; }
  /// ポインタのメンバへアクセスする
  constexpr Entry* operator->() noexcept { return curr_ptr_; }
  /// ポインタのメンバへアクセスする
  constexpr const Entry* operator->() const noexcept { return curr_ptr_; }

  /// 生ポインタを取得する（テスト用）
  constexpr Entry* data() const noexcept { return curr_ptr_; }

 private:
  Entry* curr_ptr_;   ///< 現在指している位置
  Entry* begin_ptr_;  ///< 区間の先頭
  Entry* end_ptr_;    ///< 区間の末尾
};

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

/// GC で削除する SearchAmount のしきい値を決めるために見るエントリの数
constexpr std::size_t kGcSamplingEntries = 20000;
}  // namespace detail

/**
 * @brief 経路に依存しない探索結果を記録する置換表。（通常テーブル）
 *
 * このクラスは探索結果を循環配列で管理している。探索結果のデフォルト挿入位置は、`PointerOf()` により
 * 決められる。もし挿入位置が衝突した場合、空きエントリが見つかるまで真後ろのエントリを参照する。
 *
 * エントリの削除はガベージコレクションで行う。これは、探索中に動的にエントリを削除すると、以前保存したエントリに
 * アクセスできなくなる可能性があるためである。
 */
class RegularTable {
 public:
  static constexpr std::size_t kSizePerEntry = sizeof(Entry);  ///< 1エントリのサイズ(byte)

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
   * @brief 要素数が `num_entries` 個になるようにメモリの確保・解放を行う
   * @param num_entries 要素数
   */
  void Resize(std::uint64_t num_entries) {
    // 通常テーブルに保存する要素数。最低でも 1 以上になるようにする
    num_entries = std::max<std::uint64_t>(num_entries, 1);

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
   * @brief `board_key` に対応する循環領域へのポインタを取得する
   * @param board_key 盤面ハッシュ
   * @return `board_key` に対応する循環領域ポインタ
   *
   * @note 盤面ハッシュ値の下位32ビットをもとに循環領域の先頭位置を決定する
   */
  CircularEntryPointer PointerOf(Key board_key) {
    static_assert(sizeof(Key) == 8);

    // Stockfish の置換表と同じアイデア。少し工夫をすることで mod 演算を回避できる。
    // hash_low が [0, 2^32) の一様分布にしたがうと仮定すると、idx はだいたい [0, entries_.size())
    // の一様分布にしたがう。
    const Key hash_low = board_key & Key{0xffff'ffffULL};
    auto idx = (hash_low * entries_.size()) >> 32;
    auto data = entries_.data();
    return {data + idx, data, data + entries_.size()};
  }

  /**
   * @brief 通常テーブルのメモリ使用率を見積もる。
   * @return メモリ使用率（通常テーブル）
   *
   * `entries_` の中から `kHashfullCalcEntries` 個のエントリをサンプリングしてメモリ使用率を求める。
   */
  double CalculateHashRate() const noexcept {
    std::size_t used_count = 0;
    std::size_t idx = 1;
    for (std::size_t i = 0; i < detail::kHashfullCalcEntries; ++i) {
      {
        if (!entries_[idx].IsNull()) {
          used_count++;
        }
      }

      // 連続領域をカウントすると偏りが出やすくなってしまうので、大きめの値を足す。
      idx += 334;
      if (idx >= entries_.size()) {
        idx -= entries_.size();
      }
    }

    return static_cast<double>(used_count) / detail::kHashfullCalcEntries;
  }

  /**
   * @brief 通常テーブルの中で、メモリ使用率が高いエントリを間引く
   * @param gc_removal_ratio GCで削除する割合
   * @pre 少なくとも1個のエントリが使用中
   * @pre 0 < gc_removal_ratio < 1
   *
   * `entries_` の中から `GcSamplingEntries` 個のエントリの探索量を調べ、下位 `kGcRemovalRatio` のエントリを削除する。
   */
  void CollectGarbage(double gc_removal_ratio) {
    // Amount を kGcSamplingEntries 個だけサンプリングする
    std::size_t counted_num = 0;
    std::size_t idx = 0;
    std::vector<SearchAmount> amounts;
    amounts.reserve(detail::kGcSamplingEntries);

    while (counted_num < detail::kGcSamplingEntries) {
      {
        const std::shared_lock lock(entries_[idx]);
        if (!entries_[idx].IsNull()) {
          amounts.push_back(entries_[idx].Amount());
          counted_num++;
        }
      }

      idx += 334;
      if (idx >= entries_.size()) {
        idx -= entries_.size();
      }
    }

    const std::int64_t gc_removal_pivot =
        std::max<std::int64_t>(static_cast<std::int64_t>(detail::kGcSamplingEntries * gc_removal_ratio), 1);
    auto pivot_itr = amounts.begin() + gc_removal_pivot;
    std::nth_element(amounts.begin(), pivot_itr, amounts.end());
    const SearchAmount max_amount = *std::max_element(amounts.begin(), amounts.end());
    const bool should_cut = (max_amount > std::numeric_limits<SearchAmount>::max() / 8);
    const auto amount_threshold = *pivot_itr;

    // 探索量が amount_threshold を下回っているエントリをすべて削除する
    for (auto&& entry : entries_) {
      const std::lock_guard lock(entry);
      if (!entry.IsNull() && entry.Amount() <= amount_threshold) {
        entry.SetNull();
      } else if (!entry.IsNull() && should_cut) {
        entry.CutAmount();
      }
    }

    // 置換表に歯抜けがあるとエントリにアクセスできないため、コンパクションは必須。
    CompactEntries();
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
   * `Save()` で書き出した情報を置換表へ読み込む。ストリームから読み込んだエントリを挿入するイメージで動作する。
   *
   * @see Save()
   */
  std::istream& Load(std::istream& is) {
    std::uint64_t used_entries{};
    is.read(reinterpret_cast<char*>(&used_entries), sizeof(used_entries));
    const std::uint64_t loop_count = std::min<std::uint64_t>(used_entries, entries_.size() - 1);

    for (std::uint64_t i = 0; i < loop_count; ++i) {
      Entry entry;
      is.read(reinterpret_cast<char*>(&entry), sizeof(entry));

      auto ptr = PointerOf(entry.BoardKey());
      for (; !ptr->IsNull(); ++ptr) {
      }
      *ptr = entry;
    }

    return is;
  }

  /// 通常テーブルに保存可能な要素数。
  std::size_t Capacity() const noexcept { return entries_.size(); }

  // <テスト用>

  /// 通常テーブルの先頭
  auto begin() noexcept { return entries_.data(); }
  /// 通常テーブルの末尾
  auto end() noexcept { return entries_.data() + entries_.size(); }

  /**
   * @brief エントリをできるだけ手前の方に移動させる（コンパクション）
   *
   * GC + コンパクションを同時に単体テストするのは厳しいので、コンパクションだけ行えるようにしておく。
   */
  void CompactEntries() {
    // entries_ の最初の部分が若干コンパクションしきれない可能性があるが目を瞑る
    for (auto&& entry : entries_) {
      const std::lock_guard lock(entry);
      if (entry.IsNull()) {
        continue;
      }

      // できるだけ手前の非 null な位置へ移動する
      for (auto ptr = PointerOf(entry.BoardKey()); &*ptr != &entry; ++ptr) {
        const std::lock_guard lock(*ptr);
        if (ptr->IsNull()) {
          *ptr = entry;
          entry.SetNull();
          break;
        }
      }
    }
  }
  // </テスト用>

 private:
  /**
   * @brief 通常エントリの本体。
   */
  std::vector<Entry> entries_;
};
}  // namespace komori::tt

#endif  // KOMORI_REGULAR_TABLE_HPP_
