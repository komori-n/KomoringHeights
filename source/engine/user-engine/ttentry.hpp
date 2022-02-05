#ifndef TTENTRY_HPP_
#define TTENTRY_HPP_

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include "typedefs.hpp"

namespace komori {
// <NodeState>
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
  switch (node_state) {
    case NodeState::kOtherState:
      return os << "kOtherState";
    case NodeState::kMaybeRepetitionState:
      return os << "kMaybeRepetitionState";
    case NodeState::kRepetitionState:
      return os << "kRepetitionState";
    case NodeState::kDisprovenState:
      return os << "kDisprovenState";
    case NodeState::kProvenState:
      return os << "kProvenState";
    case NodeState::kNullState:
      return os << "kNullState";
    default:
      return os << "Unknown(" << static_cast<std::uint32_t>(node_state) << ")";
  }
}

inline constexpr bool IsFinal(NodeState node_state) {
  return node_state == NodeState::kProvenState || node_state == NodeState::kDisprovenState ||
         node_state == NodeState::kRepetitionState;
}

inline constexpr NodeState StripMaybeRepetition(NodeState node_state) {
  return node_state == NodeState::kMaybeRepetitionState ? NodeState::kOtherState : node_state;
}
// </NodeState>

// <SearchedAmount>
/// 大まかな探索量。TreeSize のようなもので、GCのときに削除するノードを選ぶ際に用いる。
using SearchedAmount = std::uint32_t;
/// SearchedAmount の最小値。value=0 は null entry　として用いたいので、1 以上の値にする
inline constexpr SearchedAmount kMinimumSearchedAmount = 1;
/// 未探索節点の SearchedAmount の値
inline constexpr SearchedAmount kFirstSearchAmount = kMinimumSearchedAmount;
// </SearchedAmount>

// <StateAmount>
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

inline constexpr StateAmount kFirstSearch = {NodeState::kOtherState, kFirstSearchAmount};
inline constexpr StateAmount kNullEntry = {NodeState::kNullState, 0};
// </StateAmount>

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
  constexpr HandsData(Hand hand, Move16 move, MateLen mate_len) {
    entries_[0] = {hand, move, mate_len};
    std::fill(entries_.begin() + 1, entries_.end(), Entry{kNullHand, MOVE_NONE, MateLen{0, HAND_ZERO}});
  }

  constexpr PnDn Pn() const { return kProven ? 0 : kInfinitePnDn; }
  constexpr PnDn Dn() const { return kProven ? kInfinitePnDn : 0; }
  /// 証明駒（反証駒）を追加できる余地があるなら
  /// true。手前から順に格納されるので、末尾要素が空かどうか調べるだけでよい。
  constexpr bool IsFull() const { return entries_[kHandsLen - 1].hand != kNullHand; }

  /// hand を証明（反証）できるならその手を返す。証明（反証）できなければ kNullHand を返す。
  Hand ProperHand(Hand hand) const;
  Move16 BestMove(Hand hand) const;
  MateLen GetMateLen(Hand hand) const;

  /// 証明駒（反証駒）を追加する。IsFull() の場合、何もしない。
  void Add(Hand hand, Move16 move, MateLen mate_len);
  /// 証明駒（反証駒）の追加を報告する。もう必要ない（下位互換）な局面があればすべて消す。
  /// 削除した結果、エントリ自体が不要になった場合のみ true が返る。
  bool Update(Hand hand);

 private:
  struct Entry {
    Hand hand;
    Move16 move;
    MateLen mate_len;

    Entry() = default;
    Entry(Hand hand, Move16 move, MateLen mate_len) : hand{hand}, move{move}, mate_len{mate_len} {}
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
 * 以下の4種類の局面のいずれかを保持する。
 *
 * - 通常局面（kOtherState or kMaybeRepetitionState）
 *   - 探索中の局面。pn, dn 等の基本的なデータを保持する
 * - 証明済局面（kProvenState）
 *   - 証明駒を1エントリに複数個保存する
 * - 反証済局面（kDisprovenState）
 *   - 反証駒を1エントリに複数保存する
 * - 千日手局面（kRepetitionState）
 *   - 千日手局面を保持する。
 *
 * メモリ消費量を切り詰めるために、これらのデータは union に格納する。union の代わりに std::variant を用いると、
 * インデックスの分だけメモリ消費量が増えてしまうのでダメ。
 */
class CommonEntry {
 public:
  friend std::ostream& operator<<(std::ostream& os, const CommonEntry& entry);

  CommonEntry() : dummy_{} {};
  /// 通常局面のエントリを作成する。
  constexpr CommonEntry(UnknownData&& unknown, std::uint32_t hash_high, SearchedAmount amount = kFirstSearchAmount)
      : hash_high_{hash_high}, s_amount_{NodeState::kOtherState, amount}, unknown_{std::move(unknown)} {}
  /// 証明済局面のエントリを作成する。
  constexpr CommonEntry(ProvenData&& proven, std::uint32_t hash_high, SearchedAmount amount = kFirstSearchAmount)
      : hash_high_{hash_high}, s_amount_{NodeState::kProvenState, amount}, proven_{std::move(proven)} {}
  /// 反証済局面のエントリを作成する。
  constexpr CommonEntry(DisprovenData&& disproven, std::uint32_t hash_high, SearchedAmount amount = kFirstSearchAmount)
      : hash_high_{hash_high}, s_amount_{NodeState::kDisprovenState, amount}, disproven_{std::move(disproven)} {}
  /// 千日手用のエントリを作成する。
  constexpr explicit CommonEntry(RepetitionData&& rep,
                                 std::uint32_t hash_high,
                                 SearchedAmount amount = kFirstSearchAmount)
      : hash_high_{hash_high}, s_amount_{NodeState::kRepetitionState, amount}, rep_{std::move(rep)} {}

  /// エントリの中身を空にする。
  constexpr void Clear() { s_amount_ = kNullEntry; }
  /// エントリが空かどうかチェックする
  constexpr bool IsNull() const { return s_amount_ == kNullEntry; }

  // <Getter>
  // メソッド名の一部に Get がついている理由は、型名との衝突を避けるため。

  constexpr std::uint32_t HashHigh() const { return hash_high_; }
  constexpr NodeState GetNodeState() const { return s_amount_.node_state; }
  constexpr SearchedAmount GetSearchedAmount() const { return s_amount_.amount; }
  constexpr StateAmount GetStateAmount() const { return s_amount_; }

  // 厳密には Getter ではないが、利用者から見ると Getter のようなものたち
  PnDn Pn() const;
  PnDn Dn() const;

  // </Getter>

  constexpr bool IsFinal() const { return ::komori::IsFinal(GetNodeState()); }
  constexpr bool IsMaybeRepetition() const { return s_amount_.node_state == NodeState::kMaybeRepetitionState; }
  /// 通常局面かつまだ初回探索をしていない場合のみ true。
  constexpr bool IsFirstVisit() const { return !IsFinal() && s_amount_.amount == kFirstSearchAmount; }

  // <TryGet>
  // データ（XxxData）を返すメソッドたち。
  // 現在が Xxx 状態なら内部データへのポインタを返す。そうでないなら、nullptr を返す。

  UnknownData* TryGetUnknown() { return !IsFinal() ? &unknown_ : nullptr; }
  ProvenData* TryGetProven() { return GetNodeState() == NodeState::kProvenState ? &proven_ : nullptr; }
  DisprovenData* TryGetDisproven() { return GetNodeState() == NodeState::kDisprovenState ? &disproven_ : nullptr; }
  RepetitionData* TryGetRepetition() { return GetNodeState() == NodeState::kRepetitionState ? &rep_ : nullptr; }

  const UnknownData* TryGetUnknown() const { return !IsFinal() ? &unknown_ : nullptr; }
  const ProvenData* TryGetProven() const { return GetNodeState() == NodeState::kProvenState ? &proven_ : nullptr; }
  const DisprovenData* TryGetDisproven() const {
    return GetNodeState() == NodeState::kDisprovenState ? &disproven_ : nullptr;
  }
  const RepetitionData* TryGetRepetition() const {
    return GetNodeState() == NodeState::kRepetitionState ? &rep_ : nullptr;
  }
  // </TryGet>

  // <GetState>
  /**
   * @brief hand に最も"ふさわしい"持ち駒を返す
   *
   * いろいろな用途に使える便利関数。以下のようにしてエントリに保存された持ち駒が現局面と関係ありそうかどうかを
   * 簡単に調べられる。例えば、現局面の攻め方の持ち駒が hand のとき、以下のようにして entry と現局面が関係あるかどうかを
   * 調べることができる。
   *
   * ```
   * if (auto proper_hand = entry->ProperHand(hand); proper_hand != kNullHand) {
   *   // entry には現局面に関する情報が書かれている
   * } else {
   *   // entry と現局面は関係ない
   * }
   * ```
   *
   * ProperHand() は、"ふさわしい"持ち駒があればそれを返し、なければ kNullHand を返す。ここで、
   * "ふさわしい"持ち駒とは以下ような手のことを言う。
   *
   * - 通常局面｜エントリに保存された持ち駒と hand が一致している
   * - 証明済局面｜エントリに保存された持ち駒で hand の詰みを示せる（hand の劣等局面になっている）
   * - 反証済局面｜エントリに保存された持ち駒で hand の不詰を示せる（hand の優等局面になっている）
   * - 千日手局面｜（常に hand を返す）
   */
  Hand ProperHand(Hand hand) const;

  Move16 BestMove(Hand hand) const;
  MateLen GetMateLen(Hand hand) const;
  // </GetState>

  constexpr void SetSearchedAmount(SearchedAmount amount) { s_amount_.amount = amount; }
  constexpr void UpdateSearchedAmount(SearchedAmount amount) { s_amount_.amount = s_amount_.amount + amount; }
  constexpr void SetMaybeRepetition() { s_amount_.node_state = NodeState::kMaybeRepetitionState; }

  /// 証明駒 proof_hand をもとにエントリ内容を更新する。エントリ自体が必要なくなった場合、true を返す。
  bool UpdateWithProofHand(Hand proof_hand);
  /// 反証駒 disproof_hand をもとにエントリ内容を更新する。エントリ自体が必要なくなった場合、true を返す。
  bool UpdateWithDisproofHand(Hand disproof_hand);

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
std::string ToString(const CommonEntry& entry);

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
  friend std::ostream& operator<<(std::ostream& os, const SearchResult& result);

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
        mate_len_{entry.GetMateLen(hand)} {}
  /// 生データからコンストラクトする。
  SearchResult(NodeState state,
               SearchedAmount amount,
               PnDn pn,
               PnDn dn,
               Hand hand,
               Move16 move = MOVE_NONE,
               MateLen mate_len = {kMaxNumMateMoves, HAND_ZERO})
      : state_{state}, amount_{amount}, hand_{hand}, pn_{pn}, dn_{dn}, move_{move}, mate_len_{mate_len} {}

  PnDn Pn() const { return pn_; }
  PnDn Dn() const { return dn_; }
  PnDn Phi(bool or_node) const { return or_node ? pn_ : dn_; }
  PnDn Delta(bool or_node) const { return or_node ? dn_ : pn_; }
  bool IsFinal() const { return Pn() == 0 || Dn() == 0; }
  Hand ProperHand() const { return hand_; }
  NodeState GetNodeState() const { return state_; }
  SearchedAmount GetSearchedAmount() const { return amount_; }
  Move16 BestMove() const { return move_; }
  MateLen GetMateLen() const { return mate_len_; }

  bool Exceeds(PnDn thpn, PnDn thdn) { return pn_ >= thpn || dn_ >= thdn; }

 private:
  NodeState state_;        ///< 局面の状態（詰み／不詰／不明　など）
  SearchedAmount amount_;  ///< 局面に対して探索した局面数
  Hand hand_;              ///< 局面における ProperHand
  PnDn pn_;
  PnDn dn_;

  Move16 move_;
  MateLen mate_len_;
};

std::ostream& operator<<(std::ostream& os, const SearchResult& result);
std::string ToString(const SearchResult& result);

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

inline Hand CommonEntry::ProperHand(Hand hand) const {
  switch (GetNodeState()) {
    case NodeState::kProvenState:
      return proven_.ProperHand(hand);
    case NodeState::kDisprovenState:
      return disproven_.ProperHand(hand);
    case NodeState::kRepetitionState:
      return kNullHand;
    default:
      return unknown_.ProperHand(hand);
  }
}
}  // namespace komori

#endif  // TTENTRY_HPP_
