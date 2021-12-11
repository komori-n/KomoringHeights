#ifndef TTCLUSTER_HPP_
#define TTCLUSTER_HPP_

#include <array>

#include "typedefs.hpp"

namespace komori {
/**
 * @brief 通常局面のデータ。(pn, dn, hand, min_depth) を格納する。
 */
class UnknownData {
 public:
  friend std::ostream& operator<<(std::ostream& os, const UnknownData& data);
  constexpr UnknownData(PnDn pn, PnDn dn, Hand hand, Depth depth) : pn_(pn), dn_(dn), hand_(hand), min_depth_(depth) {}

  constexpr PnDn Pn() const { return pn_; }
  constexpr PnDn Dn() const { return dn_; }
  constexpr void UpdatePnDn(PnDn pn, PnDn dn) {
    pn_ = pn;
    dn_ = dn;
  }
  /// 最小距離 `min_depth` を更新する
  constexpr void UpdateDepth(Depth depth) { min_depth_ = std::min(min_depth_, depth); }
  /// 格納している持ち駒が引数に一致していればそのまま返す。一致していなければ kNullHand を返す。
  constexpr Hand ProperHand(Hand hand) const { return hand_ == hand ? hand : kNullHand; }
  /// 格納している持ち駒が引数よりも優等であれば true。
  constexpr bool IsSuperiorThan(Hand hand) const { return hand_is_equal_or_superior(hand_, hand); }
  /// 格納している持ち駒が引数よりも劣等であれば true。
  constexpr bool IsInferiorThan(Hand hand) const { return hand_is_equal_or_superior(hand, hand_); }
  /// infinite loop の可能性があるかどうかの判定に用いる。現在の探索深さ depth が min_depth よりも深いなら true。
  constexpr bool IsOldChild(Depth depth) const { return min_depth_ < depth; }
  constexpr Depth MinDepth() const { return min_depth_; }

 private:
  PnDn pn_, dn_;     ///< 証明数、反証数
  Hand hand_;        ///< （OR nodeから見た）持ち駒
  Depth min_depth_;  ///< 最小距離。infinite loop の検証に用いる
};
std::ostream& operator<<(std::ostream& os, const UnknownData& data);

/**
 * @brief 証明駒または反証駒を格納するデータ構造。
 *
 * 詰み（不詰）の局面では、pn, dn, min_depth は格納する必要がない。そのため、UnknownData と同じ領域に
 * 証明駒（反証駒）を複数個格納することができる。
 *
 * @note 実装の労力を小さくするために、証明済局面と反証済局面をテンプレートパラメータに持つ。
 *
 * @tparam kProven true: 証明済局面, false: 反証済局面
 */
template <bool kProven>
class HandsData {
 public:
  template <bool kProven2>
  friend std::ostream& operator<<(std::ostream& os, const HandsData<kProven2>& data);
  constexpr explicit HandsData(Hand hand) {
    hands_[0] = hand;
    std::fill(hands_.begin() + 1, hands_.end(), kNullHand);
  }

  constexpr PnDn Pn() const { return kProven ? 0 : kInfinitePnDn; }
  constexpr PnDn Dn() const { return kProven ? kInfinitePnDn : 0; }
  /// 証明駒（反証駒）を追加できる余地があるなら
  /// true。手前から順に格納されるので、末尾要素が空かどうか調べるだけでよい。
  constexpr bool IsFull() const { return hands_[kHandsLen - 1] != kNullHand; };

  /// hand を証明（反証）できるならその手を返す。証明（反証）できなければ kNullHand を返す。
  Hand ProperHand(Hand hand) const;
  /// 証明駒（反証駒）を追加する。IsFull() の場合、何もしない。
  void Add(Hand hand);
  /// 証明駒（反証駒）の追加を報告する。もう必要ない（下位互換）な局面があればすべて消す。
  /// 削除した結果、エントリ自体が不要になった場合のみ true が返る。
  bool Update(Hand hand);

 private:
  static inline constexpr std::size_t kHandsLen = sizeof(UnknownData) / sizeof(Hand);
  std::array<Hand, kHandsLen> hands_;  ///< 証明駒（反証駒）
};
template <bool kProven>
std::ostream& operator<<(std::ostream& os, const HandsData<kProven>& data);

/// 証明済局面
using ProvenData = HandsData<true>;
/// 反証済局面
using DisprovenData = HandsData<false>;

/**
 * @brief 千日手局面のデータを格納するデータ構造。中身は見ての通り空で、タグとして用いる。
 */
struct RepetitionData {
  constexpr PnDn Pn() const { return kInfinitePnDn; }
  constexpr PnDn Dn() const { return 0; }
};

std::ostream& operator<<(std::ostream& os, const RepetitionData& data);

/**
 * @brief 局面の探索結果を保存する構造体。
 *
 * 以下の4種類の局面のいずれかを保持する。メモリ量の節約のために、union を用いている。
 * 各エントリにどの種類のデータが格納されているかは、GetNodeState() によって判別できる。
 *
 * - 通常局面（kOtherState or kMaybeRepetitionState）
 *   - pn, dn 等の df-pn の基本的なデータを保持する。
 * - 証明済局面（kProvenState）
 *   - 証明駒を1エントリに複数個保存する。
 * - 反証済局面（kDisprovenState）
 *   - 反証駒を1エントリに複数保存する
 */
class CommonEntry {
 public:
  friend std::ostream& operator<<(std::ostream& os, const CommonEntry& entry);

  CommonEntry();
  /// 通常局面のエントリを作成する。
  constexpr CommonEntry(std::uint32_t hash_high, UnknownData&& unknown)
      : hash_high_{hash_high}, s_gen_{kFirstSearch}, unknown_{std::move(unknown)} {}
  /// 証明済局面のエントリを作成する。
  constexpr CommonEntry(std::uint32_t hash_high, std::uint64_t num_searched, ProvenData&& proven)
      : hash_high_{hash_high},
        s_gen_{NodeState::kProvenState, CalcGeneration(num_searched)},
        proven_{std::move(proven)} {}
  /// 反証済局面のエントリを作成する。
  constexpr CommonEntry(std::uint32_t hash_high, std::uint64_t num_searched, DisprovenData&& disproven)
      : hash_high_{hash_high},
        s_gen_{NodeState::kDisprovenState, CalcGeneration(num_searched)},
        disproven_{std::move(disproven)} {}

  /// 千日手用のダミーエントリを作成する。
  constexpr explicit CommonEntry(RepetitionData&& rep)
      : hash_high_{0}, s_gen_{NodeState::kRepetitionState, 0}, rep_{std::move(rep)} {}

  constexpr std::uint32_t HashHigh() const { return hash_high_; }
  constexpr NodeState GetNodeState() const { return s_gen_.node_state; }
  constexpr Generation GetGeneration() const { return s_gen_.generation; }
  constexpr StateGeneration GetStateGeneration() const { return s_gen_; }
  constexpr void UpdateGeneration(std::uint64_t num_searched) { s_gen_.generation = CalcGeneration(num_searched); }

  /// 通常局面かつ千日手の可能性がある場合のみ true。
  constexpr bool IsMaybeRepetition() const { return s_gen_.node_state == NodeState::kMaybeRepetitionState; }
  /// 通常局面かつ千日手の可能性があることを報告する。
  constexpr void SetMaybeRepetition() { s_gen_.node_state = NodeState::kMaybeRepetitionState; }
  /// 通常局面かつまだ初回探索をしていない場合のみ true。
  constexpr bool IsFirstVisit() const { return s_gen_ == kFirstSearch; }

  /// 証明数
  PnDn Pn() const;
  /// 反証数
  PnDn Dn() const;
  /**
   * @brief 置換表に保存されている手のうち、調べたい局面に"ふさわしい"手を返す。
   *        条件に一致する hand がなければ kNullHand を返す。
   *
   * - 通常局面
   *   - 格納している持ち駒が hand に一致していればそれを返す
   * - 証明済局面
   *   - 証明できるならその証明駒を返す
   * - 反証済局面
   *   - 反証できるならその反証駒を返す
   * - 千日手局面
   *   - 常に kNullHand を返す
   *
   * @param hand    調べたい局面の持ち駒
   * @return Hand   条件に一致する hand があればそれを返す。なければ kNullHand を返す。
   */
  Hand ProperHand(Hand hand) const;

  /// 通常局面なら (pn, dn) を更新する。そうでないなら何もしない。
  void UpdatePnDn(PnDn pn, PnDn dn, std::uint64_t num_searched);
  /// 証明駒 proof_hand をもとにエントリ内容を更新する。エントリ自体が必要なくなった場合、true を返す。
  bool UpdateWithProofHand(Hand proof_hand);
  /// 反証駒 disproof_hand をもとにエントリ内容を更新する。エントリ自体が必要なくなった場合、true を返す。
  bool UpdateWithDisproofHand(Hand disproof_hand);

  /// 通常局面ならメンバのポインタを返す。そうでないなら nullptr を返す。
  UnknownData* TryGetUnknown();
  /// 証明済局面ならメンバのポインタを返す。そうでないなら nullptr を返す。
  ProvenData* TryGetProven();
  /// 反証済局面ならメンバのポインタを返す。そうでないなら nullptr を返す。
  DisprovenData* TryGetDisproven();
  /// 千日手局面ならメンバのポインタを返す。そうでないなら nullptr を返す。
  RepetitionData* TryGetRepetition();

 private:
  std::uint32_t hash_high_;  ///< 盤面のハッシュの上位32bit
  StateGeneration s_gen_;    ///< ノード状態と置換表世代
  union {
    std::array<std::uint32_t, sizeof(UnknownData) / sizeof(std::uint32_t)> dummy_;  ///< ダミーエントリ
    UnknownData unknown_;                                                           ///< 通常局面
    ProvenData proven_;                                                             ///< 証明済局面
    DisprovenData disproven_;                                                       ///< 反証済局面
    RepetitionData rep_;                                                            ///< 千日手局面
  };

  // サイズチェック
  static_assert(sizeof(dummy_) == sizeof(unknown_));
  static_assert(sizeof(dummy_) == sizeof(proven_));
  static_assert(sizeof(dummy_) == sizeof(disproven_));
  static_assert(sizeof(dummy_) >= sizeof(rep_));
};

std::ostream& operator<<(std::ostream& os, const CommonEntry& entry);

// サイズ&アラインチェック
static_assert(sizeof(CommonEntry) == sizeof(std::uint32_t) + sizeof(StateGeneration) + sizeof(UnknownData));
static_assert(alignof(std::uint64_t) % alignof(CommonEntry) == 0);

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
  Iterator SetProven(std::uint32_t hash_high, Hand proof_hand, std::uint64_t num_searched);
  /// disproof_hand により詰みであることを報告する。
  Iterator SetDisproven(std::uint32_t hash_high, Hand disproof_hand, std::uint64_t num_searched);
  /// path_key により千日手であることを報告する。
  Iterator SetRepetition(Iterator entry, Key path_key, std::uint64_t num_searched);

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

}  // namespace komori

#endif  // TTCLUSTER_HPP_