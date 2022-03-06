#ifndef TTENTRY_HPP_
#define TTENTRY_HPP_

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include "typedefs.hpp"

/**
 * 以下では局面の探索結果を表現するためのデータ構造が定義する。
 *
 * コードの再利用性を高めるために、少し凝ったデータ構造になっている。全体像を以下に示す。
 *
 *                  (union)                inherit               inherit
 * UnknownData    <--------- SearchResult <------- PackedResult <-------- CommonEntry
 * ProvenData     <-|
 * DisprovenData  <-|
 * RepetitionData <-|
 *
 * 以下でデータ構造の概要を簡潔に述べる。
 *
 * # SearchResult
 *
 * 探索結果を格納するデータ構造のうち最も基本的なもの。単一局面の探索結果を格納することができる。
 *
 * SearchResultにはUnknownData, ProvenData, DisprovenData, RepetitionData
 * の4種類の形のデータを格納することができる。格納するデータの種別は NodeState により区別する。 union
 * を用いてデータ構造を無理やり統一させることで、メモリ利用効率を高めることができる。
 *
 * # PackedResult
 *
 * 複数局面の探索結果を格納できるデータ構造。
 *
 * SearchResultでは、4つのデータ構造のサイズが異なるのでメモリ使用に無駄が生じる。そのため、1エントリに
 * 複数個の探索結果を格納することを許容することで、さらにメモリ使用効率を高めることができる。PackedResultはそのための
 * データ構造である。
 *
 * 結果を取り出す際は hand を渡す必要がある。1つのエントリ内に複数個の結果が混在しているため、どのエントリを
 * 取り出したいかを指定する必要があるためである。
 *
 * # CommonEntry
 *
 * 置換表に書くエントリのデータ構造。PackedResultに加えて hash_high を格納できるようになっている。
 */
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
  /// 格納している持ち駒を返す
  constexpr Hand GetHand() const { return hand_; }
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

  Hand FrontHand() const { return entries_[0].hand; }
  Move16 FrontBestMove() const { return entries_[0].move; }
  MateLen FrontMateLen() const { return entries_[0].mate_len; }

  /// 証明駒（反証駒）を追加する。IsFull() の場合、何もしない。
  void Add(Hand hand, Move16 move, MateLen mate_len);
  /// 証明駒（反証駒）の追加を報告する。もう必要ない（下位互換）な局面があればすべて消す。
  /// 削除した結果、エントリ自体が不要になった場合のみ true が返る。
  bool Update(Hand hand);

  void Simplify(Hand hand);

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

class PackedResult;

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
class SearchResult {
 public:
  // union のデータ構造を使いまわしたいので friend 指定しておく
  friend class PackedResult;
  friend std::ostream& operator<<(std::ostream& os, const SearchResult& entry);

  SearchResult() : dummy_{} {};
  /// 通常局面のエントリを作成する。
  constexpr SearchResult(UnknownData&& unknown, SearchedAmount amount = kFirstSearchAmount)
      : s_amount_{NodeState::kOtherState, amount}, unknown_{std::move(unknown)} {}
  /// 証明済局面のエントリを作成する。
  constexpr SearchResult(ProvenData&& proven, SearchedAmount amount = kFirstSearchAmount)
      : s_amount_{NodeState::kProvenState, amount}, proven_{std::move(proven)} {}
  /// 反証済局面のエントリを作成する。
  constexpr SearchResult(DisprovenData&& disproven, SearchedAmount amount = kFirstSearchAmount)
      : s_amount_{NodeState::kDisprovenState, amount}, disproven_{std::move(disproven)} {}
  /// 千日手用のエントリを作成する。
  constexpr explicit SearchResult(RepetitionData&& rep, SearchedAmount amount = kFirstSearchAmount)
      : s_amount_{NodeState::kRepetitionState, amount}, rep_{std::move(rep)} {}
  SearchResult(const SearchResult&) = default;
  SearchResult(SearchResult&&) noexcept = default;
  SearchResult& operator=(const SearchResult&) = default;
  SearchResult& operator=(SearchResult&&) noexcept = default;

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

  // <Getter>
  // メソッド名の一部に Get がついている理由は、型名との衝突を避けるため。

  constexpr NodeState GetNodeState() const { return s_amount_.node_state; }
  constexpr SearchedAmount GetSearchedAmount() const { return s_amount_.amount; }
  constexpr StateAmount GetStateAmount() const { return s_amount_; }

  // 厳密には Getter ではないが、利用者から見ると Getter のようなものたち
  PnDn Pn() const;
  PnDn Dn() const;
  PnDn Phi(bool or_node) const { return or_node ? Pn() : Dn(); }
  PnDn Delta(bool or_node) const { return or_node ? Dn() : Pn(); }

  Hand FrontHand() const;
  Move16 FrontBestMove() const;
  MateLen FrontMateLen() const;
  // </Getter>

  /// エントリの中身を空にする。
  constexpr void Clear() { s_amount_ = kNullEntry; }
  /// エントリが空かどうかチェックする
  constexpr bool IsNull() const { return s_amount_ == kNullEntry; }

  constexpr bool IsFinal() const { return ::komori::IsFinal(GetNodeState()); }
  constexpr bool IsMaybeRepetition() const { return s_amount_.node_state == NodeState::kMaybeRepetitionState; }
  /// 通常局面かつまだ初回探索をしていない場合のみ true。
  constexpr bool IsFirstVisit() const { return !IsFinal() && s_amount_.amount == kFirstSearchAmount; }

  bool Exceeds(PnDn thpn, PnDn thdn) const { return Pn() >= thpn || Dn() >= thdn; }

  constexpr void SetSearchedAmount(SearchedAmount amount) { s_amount_.amount = amount; }
  constexpr void UpdateSearchedAmount(SearchedAmount amount) { s_amount_.amount = s_amount_.amount + amount; }
  constexpr void SetMaybeRepetition() { s_amount_.node_state = NodeState::kMaybeRepetitionState; }

 private:
  union {
    std::array<std::uint32_t, sizeof(UnknownData) / sizeof(std::uint32_t)> dummy_;  ///< ダミーエントリ（サイズ確認用）
    UnknownData unknown_;                                                           ///< 通常局面
    ProvenData proven_;                                                             ///< 証明済局面
    DisprovenData disproven_;                                                       ///< 反証済局面
    RepetitionData rep_;                                                            ///< 千日手局面
  };
  StateAmount s_amount_;  ///< ノード状態とこの局面を何手読んだか

  // サイズチェック
  static_assert(sizeof(dummy_) == sizeof(unknown_));
  static_assert(sizeof(dummy_) == sizeof(proven_));
  static_assert(sizeof(dummy_) == sizeof(disproven_));
  static_assert(sizeof(dummy_) >= sizeof(rep_));
};

std::ostream& operator<<(std::ostream& os, const SearchResult& search_result);
std::string ToString(const SearchResult& search_result);

class PackedResult : public SearchResult {
 public:
  template <typename... Args>
  constexpr PackedResult(Args&&... args) : SearchResult{std::forward<Args>(args)...} {}

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

  /// 証明駒 proof_hand をもとにエントリ内容を更新する。エントリ自体が必要なくなった場合、true を返す。
  bool UpdateWithProofHand(Hand proof_hand);
  /// 反証駒 disproof_hand をもとにエントリ内容を更新する。エントリ自体が必要なくなった場合、true を返す。
  bool UpdateWithDisproofHand(Hand disproof_hand);

  SearchResult Simplify(Hand hand) const;

  // FrontXxx を PackedEntry で使用するのは間違った使い方なので、普通には使えないようにしておく

  template <std::nullptr_t Null = nullptr>
  Hand FrontHand() const {
    // もし PackedEntry::FrontHand() をコールしたら static_assert で必ずコンパイルエラーになる
    static_assert(Null == nullptr && false, "PackedEntry::FrontHand() is not supported.");
    return kNullHand;
  }
  template <std::nullptr_t Null = nullptr>
  Move16 FrontBestMove() const {
    static_assert(Null == nullptr && false, "PackedEntry::FrontBestMove() is not supported.");
    return MOVE_NONE;
  }
  template <std::nullptr_t Null = nullptr>
  MateLen FrontMateLen() const {
    static_assert(Null == nullptr && false, "PackedEntry::FrontMateLen() is not supported.");
    return kZeroMateLen;
  }
};

class CommonEntry : public PackedResult {
 public:
  friend std::ostream& operator<<(std::ostream& os, const CommonEntry& entry);
  template <typename... Args>
  CommonEntry(std::uint32_t hash_high, Args&&... args)
      : hash_high_{hash_high}, PackedResult{std::forward<Args>(args)...} {}
  CommonEntry() = default;

  std::uint32_t HashHigh() const { return hash_high_; }

 private:
  std::uint32_t hash_high_{};  ///< ハッシュの上位32bit。コピーコンストラクト可能にするために const をつけない。
};

std::ostream& operator<<(std::ostream& os, const CommonEntry& entry);
std::string ToString(const CommonEntry& entry);

static_assert(alignof(std::uint64_t) % alignof(CommonEntry) == 0);

// =====================================================================================================================
// implementation
// =====================================================================================================================
// このファイルに定義されたデータ構造は全体の実行速度にかなり影響を与えうるので、
// print 系の関数以外はすべてヘッダにすべて実装する

// --------------------------------------------------------------------------------------------------------------------
// <HandsData>

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

template <bool kProven>
inline Move16 HandsData<kProven>::BestMove(Hand hand) const {
  for (const auto& e : entries_) {
    if (e.hand == kNullHand) {
      break;
    }

    if ((kProven && hand_is_equal_or_superior(hand, e.hand)) ||   // hand を証明できる
        (!kProven && hand_is_equal_or_superior(e.hand, hand))) {  // hand を反証できる
      return e.move;
    }
  }
  return MOVE_NONE;
}

template <bool kProven>
inline MateLen HandsData<kProven>::GetMateLen(Hand hand) const {
  for (const auto& e : entries_) {
    if (e.hand == kNullHand) {
      break;
    }

    if ((kProven && hand_is_equal_or_superior(hand, e.hand)) ||   // hand を証明できる
        (!kProven && hand_is_equal_or_superior(e.hand, hand))) {  // hand を反証できる
      return e.mate_len;
    }
  }
  return {kMaxNumMateMoves, 0};
}

template <bool kProven>
inline void HandsData<kProven>::Add(Hand hand, Move16 move, MateLen mate_len) {
  for (auto& e : entries_) {
    if (e.hand == kNullHand) {
      e = {hand, move, mate_len};
      return;
    }
  }
}

template <bool kProven>
inline bool HandsData<kProven>::Update(Hand hand) {
  std::size_t i = 0;
  for (auto& e : entries_) {
    if (e.hand == kNullHand) {
      break;
    }

    if ((kProven && hand_is_equal_or_superior(e.hand, hand)) ||   // hand で証明できる -> h はいらない
        (!kProven && hand_is_equal_or_superior(hand, e.hand))) {  // hand で反証できる -> h はいらない
      e.hand = kNullHand;
      continue;
    }

    // 手前から詰める
    std::swap(entries_[i], e);
    i++;
  }
  return i == 0;
}

template <bool kProven>
inline void HandsData<kProven>::Simplify(Hand hand) {
  for (const auto& e : entries_) {
    if (e.hand == kNullHand) {
      break;
    }

    if ((kProven && hand_is_equal_or_superior(hand, e.hand)) ||   // hand を証明できる
        (!kProven && hand_is_equal_or_superior(e.hand, hand))) {  // hand を反証できる
      entries_[0] = e;
      return;
    }
  }

  // 見つからなかった
  entries_[0].hand = kNullHand;
}
// </HandsData>
// --------------------------------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------------------------------
// <SearchResult>
inline PnDn SearchResult::Pn() const {
  switch (GetNodeState()) {
    case NodeState::kOtherState:
    case NodeState::kMaybeRepetitionState:
      return unknown_.Pn();
    case NodeState::kProvenState:
      return proven_.Pn();
    case NodeState::kDisprovenState:
      return disproven_.Pn();
    default:
      return rep_.Pn();
  }
}

inline PnDn SearchResult::Dn() const {
  switch (GetNodeState()) {
    case NodeState::kOtherState:
    case NodeState::kMaybeRepetitionState:
      return unknown_.Dn();
    case NodeState::kProvenState:
      return proven_.Dn();
    case NodeState::kDisprovenState:
      return disproven_.Dn();
    default:
      return rep_.Dn();
  }
}

inline Hand SearchResult::FrontHand() const {
  switch (GetNodeState()) {
    case NodeState::kOtherState:
    case NodeState::kMaybeRepetitionState:
      return unknown_.GetHand();
    case NodeState::kProvenState:
      return proven_.FrontHand();
    case NodeState::kDisprovenState:
      return disproven_.FrontHand();
    default:
      return kNullHand;
  }
}

inline Move16 SearchResult::FrontBestMove() const {
  switch (GetNodeState()) {
    case NodeState::kProvenState:
      return proven_.FrontBestMove();
    case NodeState::kDisprovenState:
      return disproven_.FrontBestMove();
    default:
      return MOVE_NONE;
  }
}

inline MateLen SearchResult::FrontMateLen() const {
  switch (GetNodeState()) {
    case NodeState::kProvenState:
      return proven_.FrontMateLen();
    case NodeState::kDisprovenState:
      return disproven_.FrontMateLen();
    default:
      return kMaxMateLen;
  }
}
// </SearchResult>
// --------------------------------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------------------------------
// <PackedResult>
inline Hand PackedResult::ProperHand(Hand hand) const {
  switch (GetNodeState()) {
    case NodeState::kOtherState:
    case NodeState::kMaybeRepetitionState:
      return unknown_.ProperHand(hand);
    case NodeState::kProvenState:
      return proven_.ProperHand(hand);
    case NodeState::kDisprovenState:
      return disproven_.ProperHand(hand);
    default:
      return kNullHand;
  }
}

inline Move16 PackedResult::BestMove(Hand hand) const {
  switch (GetNodeState()) {
    case NodeState::kProvenState:
      return proven_.BestMove(hand);
    case NodeState::kDisprovenState:
      return disproven_.BestMove(hand);
    default:
      return MOVE_NONE;
  }
}

inline MateLen PackedResult::GetMateLen(Hand hand) const {
  switch (GetNodeState()) {
    case NodeState::kProvenState:
      return proven_.GetMateLen(hand);
    case NodeState::kDisprovenState:
      return disproven_.GetMateLen(hand);
    default:
      return {kMaxNumMateMoves, 0};
  }
}

inline bool PackedResult::UpdateWithProofHand(Hand proof_hand) {
  switch (GetNodeState()) {
    case NodeState::kOtherState:
    case NodeState::kMaybeRepetitionState:
      // proper_hand よりたくさん持ち駒を持っているなら詰み -> もういらない
      return unknown_.IsSuperiorThan(proof_hand);
    case NodeState::kProvenState:
      return proven_.Update(proof_hand);
    default:
      return false;
  }
}

inline bool PackedResult::UpdateWithDisproofHand(Hand disproof_hand) {
  switch (GetNodeState()) {
    case NodeState::kOtherState:
    case NodeState::kMaybeRepetitionState:
      // proper_hand より持ち駒が少ないなら不詰 -> もういらない
      return unknown_.IsInferiorThan(disproof_hand);
    case NodeState::kDisprovenState:
      return disproven_.Update(disproof_hand);
    default:
      return false;
  }
}

inline SearchResult PackedResult::Simplify(Hand hand) const {
  auto ret = static_cast<SearchResult>(*this);
  switch (GetNodeState()) {
    case NodeState::kProvenState:
      ret.proven_.Simplify(hand);
      return ret;
    case NodeState::kDisprovenState:
      ret.disproven_.Simplify(hand);
      return ret;
    default:
      return ret;
  }
}
// </PackedResult>
// --------------------------------------------------------------------------------------------------------------------
}  // namespace komori

#endif  // TTENTRY_HPP_
