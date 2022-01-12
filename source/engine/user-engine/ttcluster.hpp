#ifndef TTCLUSTER_HPP_
#define TTCLUSTER_HPP_

#include <array>

#include "ttentry.hpp"
#include "typedefs.hpp"

namespace komori {
/**
 * @brief  局面の探索結果を格納するデータ構造。
 *
 * CommonEntry が置換表に格納するためのデータ構造であるのに対し、SearchResult は純粋に探索結果を表現することが
 * 役割のクラスである。
 *
 * また、SearchResult は探索結果を格納するためのクラスであるため、コンストラクト後に値の書き換えをすることはできない。
 */
class SearchResult {
 public:
  /// 初期化なしのコンストラクタも有効にしておく
  SearchResult() = default;
  /// CommonEntry からコンストラクトする。これだけだと証明駒／反証駒がわからないので、現在の OrHand も引数で渡す。
  SearchResult(const CommonEntry& entry, Hand hand)
      : state_{entry.GetNodeState()},
        amount_{entry.GetSearchedAmount()},
        hand_{entry.ProperHand(hand)},
        pn_{entry.Pn()},
        dn_{entry.Dn()},
        move_{entry.BestMove(hand)},
        len_{entry.GetSolutionLen(hand)} {}
  /// 生データからコンストラクトする。
  SearchResult(NodeState state,
               SearchedAmount amount,
               PnDn pn,
               PnDn dn,
               Hand hand,
               Move16 move = MOVE_NONE,
               Depth len = kMaxNumMateMoves)
      : state_{state}, amount_{amount}, hand_{hand}, pn_{pn}, dn_{dn}, move_{move}, len_{len} {}

  PnDn Pn() const { return pn_; }
  PnDn Dn() const { return dn_; }
  Hand ProperHand() const { return hand_; }
  NodeState GetNodeState() const { return state_; }
  SearchedAmount GetSearchedAmount() const { return amount_; }
  Move16 BestMove() const { return move_; }
  Depth GetSolutionLen() const { return len_; }

 private:
  NodeState state_;        ///< 局面の状態（詰み／不詰／不明　など）
  SearchedAmount amount_;  ///< 局面に対して探索した局面数
  Hand hand_;              ///< 局面における ProperHand
  PnDn pn_;
  PnDn dn_;

  Move16 move_;
  Depth len_;
};

class RepetitionCluster {
 public:
  // 格納する千日手局面の数。
  static inline constexpr std::size_t kMaxRepetitionClusterSize = 15;

  constexpr void Clear() {
    top_ = 0;
    for (auto& key : keys_) {
      key = kNullKey;
    }
  }
  void Add(Key key);
  bool DoesContain(Key key) const;

 private:
  std::array<Key, kMaxRepetitionClusterSize> keys_;
  std::uint32_t top_;
};

// サイズ&アラインチェック
static_assert(alignof(std::uint64_t) % alignof(RepetitionCluster) == 0);

/**
 * @brief CommonEntry をいくつか集めた構造体。
 *
 * 検索（LookUp）を高速化するために、hash_high の昇順になるようにソートしておく。
 */
class TTCluster {
 public:
  /// 格納するエントリ数。小さいほど検索速度が早くなるが、GC によりエントリが削除される確率が高まる。
  static inline constexpr std::size_t kClusterSize = 128;

  using Iterator = CommonEntry*;
  using ConstIterator = const CommonEntry*;

  constexpr Iterator begin() { return &(data_[0]); }
  constexpr ConstIterator begin() const { return &(data_[0]); }
  constexpr Iterator end() { return begin() + size_; }
  constexpr ConstIterator end() const { return begin() + size_; }
  constexpr std::size_t Size() const { return size_; }
  /// エントリがこのクラスタ内を指しているなら true。LookUpWithoutCreation で実在エントリを返したかどうかを
  /// 判定するのに用いる。
  constexpr bool DoesContain(Iterator entry) const {
    return entry == &kRepetitionEntry || (begin() <= entry && entry < end());
  }
  /// クラスタ内のエントリをすべて削除する。
  constexpr void Clear() {
    size_ = 0;
    rep_.Clear();
  }

  /**
   * @brief 条件に合致するエントリを探す。
   *
   * もし条件に合致するエントリが見つからなかった場合、新規作成して cluster に追加する。
   */
  Iterator LookUpWithCreation(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key) {
    return LookUp<true>(hash_high, hand, depth, path_key);
  }
  /**
   * @brief 条件に合致するエントリを探す。
   *
   * もし条件に合致するエントリが見つからなかった場合、cluster には追加せずダミーの entry を返す。
   * ダミーの entry は、次回の LookUpWithoutCreation() 呼び出しするまでの間だけ有効である。
   */
  Iterator LookUpWithoutCreation(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key) {
    return LookUp<false>(hash_high, hand, depth, path_key);
  }

  /// proof_hand により詰みであることを報告する。
  Iterator SetProven(std::uint32_t hash_high, Hand proof_hand, Move16 len, Depth depth, SearchedAmount amount);
  /// disproof_hand により詰みであることを報告する。
  Iterator SetDisproven(std::uint32_t hash_high, Hand disproof_hand, Move16 len, Depth depth, SearchedAmount amount);
  /// path_key により千日手であることを報告する。
  Iterator SetRepetition(Iterator entry, Key path_key, SearchedAmount amount);

  /// GCを実行する
  std::size_t CollectGarbage(SearchedAmount th_amount);

 private:
  static const CommonEntry kRepetitionEntry;

  /// LookUpWithCreation() と LookUpWithoutCreation() の実装本体。
  template <bool kCreateIfNotExist>
  Iterator LookUp(std::uint32_t hash_high, Hand hand, Depth depth, Key path_key);
  /// path_key による千日手かどうかを判定する。千日手の場合、そのエントリを返す。そうでない場合、nullptr を返す。
  Iterator CheckRepetition(Key path_key);

  /// 新たな entry をクラスタに追加する。クラスタに空きがない場合は、最も必要なさそうなエントリを削除する
  Iterator Add(CommonEntry&& entry);
  /// Cluster の中で最も必要ないエントリを1つ削除する。
  void RemoveOne();

  // <LowerBound関連>
  // cluster の中で hash_high 以上のエントリの最も手前の物を返す関数たち。KomoringHeights で最も重い処理。
  // 表引きを早くするために、2種類のループアンローリング版関数を使い分けて実装している。

  /// `hash_high` 以上となる最初の entry を返す
  Iterator LowerBound(std::uint32_t hash_high);
  /// `hash_high` より大きい最初の entry を返す
  Iterator UpperBound(std::uint32_t hash_high);
  /// size_ == kClusterSize のとき専用のLowerBound実装
  Iterator LowerBoundAll(std::uint32_t hash_high);
  /// size_ < kClusterSize のとき専用のLowerBound実装
  Iterator LowerBoundPartial(std::uint32_t hash_high);
  // </LowerBound関連>

  std::array<CommonEntry, kClusterSize> data_;  ///< エントリ本体
  RepetitionCluster rep_;  ///< 千日手情報。CommonEntry に押し込むとメモリを大量に消費するので別の場所に入れる
  std::uint32_t size_;  ///< クラスタ内のエントリ数
};

inline TTCluster::Iterator TTCluster::LowerBound(std::uint32_t hash_high) {
  if (size_ == kClusterSize) {
    return LowerBoundAll(hash_high);
  } else {
    return LowerBoundPartial(hash_high);
  }
}

inline TTCluster::Iterator TTCluster::UpperBound(std::uint32_t hash_high) {
  if (hash_high != 0xffff'ffff) {
    return LowerBound(hash_high + 1);
  } else {
    return end();
  }
}

/// NOLINTNEXTLINE(readability-function-size)
inline TTCluster::Iterator TTCluster::LowerBoundAll(std::uint32_t hash_high) {
  // ちょうど 7 回二分探索すれば必ず答えが見つかる
  constexpr std::size_t kLoopCnt = 7;
  static_assert(kClusterSize == 1 << kLoopCnt);

  auto curr = begin();
#define UNROLL_IMPL(i)                     \
  do {                                     \
    auto half = 1 << (kLoopCnt - 1 - (i)); \
    auto mid = curr + half - 1;            \
    if (mid->HashHigh() < hash_high) {     \
      curr = mid + 1;                      \
    }                                      \
  } while (false)

  UNROLL_IMPL(0);
  UNROLL_IMPL(1);
  UNROLL_IMPL(2);
  UNROLL_IMPL(3);
  UNROLL_IMPL(4);
  UNROLL_IMPL(5);
  UNROLL_IMPL(6);

#undef UNROLL_IMPL

  return curr;
}

/// NOLINTNEXTLINE(readability-function-size)
inline TTCluster::Iterator TTCluster::LowerBoundPartial(std::uint32_t hash_high) {
  auto len = Size();

  auto curr = begin();
#define UNROLL_IMPL()                  \
  do {                                 \
    if (len == 0) {                    \
      return curr;                     \
    }                                  \
                                       \
    auto half = len / 2;               \
    auto mid = curr + half;            \
    if (mid->HashHigh() < hash_high) { \
      len -= half + 1;                 \
      curr = mid + 1;                  \
    } else {                           \
      len = half;                      \
    }                                  \
  } while (false)

  // 高々 8 回二分探索すれば必ず答えが見つかる
  static_assert(kClusterSize < (1 << 8));
  UNROLL_IMPL();
  UNROLL_IMPL();
  UNROLL_IMPL();
  UNROLL_IMPL();
  UNROLL_IMPL();
  UNROLL_IMPL();
  UNROLL_IMPL();
  UNROLL_IMPL();

#undef UNROLL_IMPL

  return curr;
}

}  // namespace komori

#endif  // TTCLUSTER_HPP_