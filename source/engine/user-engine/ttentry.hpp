#ifndef TTENTRY_HPP_
#define TTENTRY_HPP_

#include <array>

#include "typedefs.hpp"

namespace komori {
/// 局面の状態（詰み、厳密な不詰、千日手による不詰、それ以外）を表す型
enum class NodeState : std::uint32_t {
  kOtherState,            ///< 詰みでも不詰でもない探索中局面
  kMaybeRepetitionState,  ///< kOtherState で千日手の可能性がある局面
  kRepetitionState,       ///< 千日手による不詰
  kDisprovenState,        ///< 不詰
  kProvenState,           ///< 詰み
  kNullState,             ///< 無効値
};

inline std::ostream& operator<<(std::ostream& os, NodeState node_state) {
  os << static_cast<std::uint32_t>(node_state);
  return os;
}

inline NodeState StripMaybeRepetition(NodeState node_state) {
  return node_state == NodeState::kMaybeRepetitionState ? NodeState::kOtherState : node_state;
}

/// 大まかな探索量。TreeSize のようなもので、GCのときに削除するノードを選ぶ際に用いる。
using SearchedAmount = std::uint32_t;
inline constexpr SearchedAmount kMinimumSearchedAmount = 2;
/// 何局面読んだら SearchedCmount を進めるか
inline constexpr std::uint64_t kNumSearchedPerAmount = 128;
inline constexpr SearchedAmount Update(SearchedAmount amount, std::uint64_t delta_searched) {
  auto update = delta_searched / kNumSearchedPerAmount;
  // 最低でも 1 は増加させる
  update = std::max(update, std::uint64_t{1});
  return amount + static_cast<SearchedAmount>(update);
}

inline constexpr SearchedAmount ToAmount(std::uint64_t num_searched) {
  return std::max(kMinimumSearchedAmount, static_cast<SearchedAmount>(num_searched / kNumSearchedPerAmount));
}

/**
 * @brief 局面の探索量（SearchedAmount）と局面状態（NodeState）を1つの整数にまとめたもの。
 */
struct StateAmount {
  NodeState node_state : 3;
  SearchedAmount amount : 29;
};

inline constexpr bool operator==(const StateAmount& lhs, const StateAmount& rhs) {
  return lhs.node_state == rhs.node_state && lhs.amount == rhs.amount;
}

inline constexpr bool operator!=(const StateAmount& lhs, const StateAmount& rhs) {
  return !(lhs == rhs);
}

inline constexpr StateAmount kMarkDeleted = {NodeState::kOtherState, 0};
inline constexpr StateAmount kFirstSearch = {NodeState::kOtherState, 1};

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
  constexpr HandsData(Hand hand, Move16 move, Depth len) {
    entries_[0] = {hand, move, len};
    std::fill(entries_.begin() + 1, entries_.end(), Entry{kNullHand, MOVE_NONE, 0});
  }

  constexpr PnDn Pn() const { return kProven ? 0 : kInfinitePnDn; }
  constexpr PnDn Dn() const { return kProven ? kInfinitePnDn : 0; }
  /// 証明駒（反証駒）を追加できる余地があるなら
  /// true。手前から順に格納されるので、末尾要素が空かどうか調べるだけでよい。
  constexpr bool IsFull() const { return entries_[kHandsLen - 1].hand != kNullHand; };

  /// hand を証明（反証）できるならその手を返す。証明（反証）できなければ kNullHand を返す。
  Hand ProperHand(Hand hand) const;
  Move16 BestMove(Hand hand) const;
  Depth GetSolutionLen(Hand hand) const;

  /// 証明駒（反証駒）を追加する。IsFull() の場合、何もしない。
  void Add(Hand hand, Move16 move, Depth len);
  /// 証明駒（反証駒）の追加を報告する。もう必要ない（下位互換）な局面があればすべて消す。
  /// 削除した結果、エントリ自体が不要になった場合のみ true が返る。
  bool Update(Hand hand);

 private:
  struct Entry {
    Hand hand;
    Move16 move;
    std::int16_t len;

    Entry() = default;
    Entry(Hand hand, Move16 move, Depth len) : hand{hand}, move{move}, len{static_cast<std::int16_t>(len)} {}
  };

  static inline constexpr std::size_t kHandsLen = sizeof(UnknownData) / sizeof(Entry);
  std::array<Entry, kHandsLen> entries_;  ///< 証明駒（反証駒）
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
      : hash_high_{hash_high}, s_amount_{kFirstSearch}, unknown_{std::move(unknown)} {}
  /// 証明済局面のエントリを作成する。
  constexpr CommonEntry(std::uint32_t hash_high, SearchedAmount amount, ProvenData&& proven)
      : hash_high_{hash_high}, s_amount_{NodeState::kProvenState, amount}, proven_{std::move(proven)} {}
  /// 反証済局面のエントリを作成する。
  constexpr CommonEntry(std::uint32_t hash_high, SearchedAmount amount, DisprovenData&& disproven)
      : hash_high_{hash_high}, s_amount_{NodeState::kDisprovenState, amount}, disproven_{std::move(disproven)} {}

  /// 千日手用のダミーエントリを作成する。
  constexpr explicit CommonEntry(RepetitionData&& rep)
      : hash_high_{0}, s_amount_{NodeState::kRepetitionState, 0}, rep_{std::move(rep)} {}

  /// エントリの中身を空にする
  constexpr void Clear() { s_amount_.node_state = NodeState::kNullState; }
  /// エントリが空かどうかチェックする
  constexpr bool IsNull() const { return s_amount_.node_state == NodeState::kNullState; }

  constexpr std::uint32_t HashHigh() const { return hash_high_; }
  constexpr NodeState GetNodeState() const { return s_amount_.node_state; }
  constexpr SearchedAmount GetSearchedAmount() const { return s_amount_.amount; }
  constexpr StateAmount GetStateAmount() const { return s_amount_; }
  constexpr void UpdateSearchedAmount(SearchedAmount amount) { s_amount_.amount = std::max(s_amount_.amount, amount); }

  /// 通常局面かつ千日手の可能性がある場合のみ true。
  constexpr bool IsMaybeRepetition() const { return s_amount_.node_state == NodeState::kMaybeRepetitionState; }
  /// 通常局面かつ千日手の可能性があることを報告する。
  constexpr void SetMaybeRepetition() { s_amount_.node_state = NodeState::kMaybeRepetitionState; }
  /// 通常局面かつまだ初回探索をしていない場合のみ true。
  constexpr bool IsFirstVisit() const { return s_amount_ == kFirstSearch; }

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
  Move16 BestMove(Hand hand) const;
  Depth GetSolutionLen(Hand hand) const;

  /// 通常局面なら (pn, dn) を更新する。そうでないなら何もしない。
  void UpdatePnDn(PnDn pn, PnDn dn, SearchedAmount amount);
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
  StateAmount s_amount_;     ///< ノード状態とこの局面を何手読んだか
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
static_assert(sizeof(CommonEntry) == sizeof(std::uint32_t) + sizeof(StateAmount) + sizeof(UnknownData));
static_assert(alignof(std::uint64_t) % alignof(CommonEntry) == 0);

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

template <bool kProven>
inline Hand HandsData<kProven>::ProperHand(Hand hand) const {
  for (const auto& e : entries_) {
    if (e.hand == kNullHand) {
      break;
    }

    if ((kProven && hand_is_equal_or_superior(hand, e.hand)) ||   // hand を証明できる
        (!kProven && hand_is_equal_or_superior(e.hand, hand))) {  // hand を反証できる
      return e.hand;
    }
  }
  return kNullHand;
}
}  // namespace komori

#endif  // TTENTRY_HPP_