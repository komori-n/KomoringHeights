#ifndef TTENTRY_HPP_
#define TTENTRY_HPP_

#include <optional>

#include "typedefs.hpp"

namespace komori {

/// Position の探索情報を格納するための構造体
class TTEntry {
 public:
  TTEntry() = default;
  TTEntry(std::uint32_t hash_high, Hand hand, PnDn pn, PnDn dn, Depth depth);

  static TTEntry WithProofHand(std::uint32_t hash_high, Hand proof_hand);
  static TTEntry WithDisproofHand(std::uint32_t hash_high, Hand disproof_hand);

  /// (hand, depth) に一致しているか、`hand` を証明／反証できる内容なら true
  bool ExactOrDeducable(Hand hand, Depth depth) const;
  /// 探索深さが depth 以下で hand の優等局面なら true（渡された hand よりも詰みに近いはず）
  bool IsSuperiorAndShallower(Hand hand, Depth depth) const;
  /// 探索深さが depth 以下で hand の劣等局面なら true（渡された hand よりも不詰に近いはず）
  bool IsInferiorAndShallower(Hand hand, Depth depth) const;

  /// 現在の証明数
  PnDn Pn() const;
  /// 現在の反証数
  PnDn Dn() const;
  /// 証明数と反証数を更新する。（詰み／不詰の局面では `SetProven()` / `SetDisproven()` を用いる）
  void Update(PnDn pn, PnDn dn, std::uint64_t num_searched);

  /// 詰みかどうか
  bool IsProvenNode() const;
  /// 不詰（千日手含む）かどうか
  bool IsDisprovenNode() const;
  /// 千日手ではない不詰かどうか
  bool IsNonRepetitionDisprovenNode() const;
  /// 千日手による不詰かどうか
  bool IsRepetitionDisprovenNode() const;

  /**
   * @brief 新たに証明駒を与えて、必要ないエントリを消す。このエントリ自体が不必要だと判断されたら true が返る。
   *
   * 探索中に新たに `proof_hand` が証明駒だと判明した時に呼ぶ。`proof_hand` がよりも劣る局面はもう必要ないので、
   * エントリの内容から消す。この操作によってエントリ自体が必要なくなった場合は true を返す。
   *
   * @param proof_hand  証明駒
   * @return bool       エントリ自体が必要なくなったら true、まだ有用なら false
   */
  bool UpdateWithProofHand(Hand proof_hand);
  /**
   * @brief 新たに反証駒を与えて、必要ないエントリを消す。このエントリ自体が不必要だと判断されたら true が返る。
   *
   * @param disproof_hand  反証駒
   * @return bool          エントリ自体が必要なくなったら true、まだ有用なら false
   */
  bool UpdateWithDisproofHand(Hand disproof_hand);

  /**
   * @brief 証明駒 `hand` による詰みを報告する
   *
   * a. エントリが Proven でないとき
   * エントリを Proven だとマークし、証明駒をセットする。
   *
   * b. エントリが Proven のとき
   * 証明駒の配列に `hand` を加える。ただし、配列が full の場合は何もしない。
   *
   * @param hand 証明駒
   */
  void SetProven(Hand hand);
  /// 反証駒 `hand` による不詰を報告する
  void SetDisproven(Hand hand);
  /// 千日手による不詰を報告する
  void SetRepetitionDisproven();

  /// エントリが未探索節点（まだ一度も Update() していない）かどうかを判定する
  bool IsFirstVisit() const;
  /// エントリを削除候補に設定する。次回の GC 時に優先的に削除される
  void MarkDeleteCandidate();
  bool IsMarked() const;

  /**
   * @brief エントリに保存されている持ち駒を返す
   *
   * a. 置換表が Proven のとき
   * `hand` を証明する証明駒を返す。該当する証明駒がなければ hand をそのまま返す。
   *
   * b. 置換表が NonRepetitionDisproven のとき
   * `hand` を反証する反証駒を返す。該当する反証駒がなければ hand をそのまま返す。
   *
   * c. Otherwise
   * `hand` をそのまま返す。
   *
   * @param hand   現局面の持ち駒
   * @return Hand  エントリに保存されている持ち駒
   */
  Hand ProperHand(Hand hand) const;
  /// エントリが Proven で、まだ証明駒を保存できる余地があるなら true
  bool IsWritableNewProofHand() const;
  /// エントリが NonRepetitionDisproven で、まだ反証駒を保存できる余地があるなら true
  bool IsWritableNewDisproofHand() const;

  auto StateGeneration() const { return common_.s_gen; }
  auto Generation() const { return GetGeneration(common_.s_gen); }
  auto HashHigh() const { return common_.hash_high; }

 private:
  /// エントリに保存する証明駒／反証駒の数。sizeof(known_) == sizeof(unknown) になるように設定する
  static inline constexpr std::size_t kTTEntryHandLen = 6;

  /// entry の内容をもとに、持ち駒 `hand` を持っていれば詰みだと言えるなら true、それ以外なら false
  bool DoesProve(Hand hanD) const;
  /// entry の内容をもとに、持ち駒 `hand` を持っていれば不詰だと言えるなら true、それ以外なら false
  bool DoesDisprove(Hand hand) const;

  auto NodeState() const { return GetState(common_.s_gen); }

  /// unknown_ が有効なら true
  bool IsUnknownNode() const;

  /**
   * Entry の内容によってデータ構造の使い方を変える。証明／反証済みでない局面の場合、hand, pn, dn, depth を格納する。
   * 一方、証明／反証済み局面の場合、pn, dn, depth は必要ないのでこの領域にも証明駒／反証駒を格納する。
   *
   * Entry が証明／反証済みかどうかは末尾の generation によって区別することができる。
   * 証明済みの場合は kProven、反証済みの kNonRepetitionDisproven、それ以外の場合は置換表の世代（generation）が
   * 格納される。現在のノード種別は IsXxxNode() により判定できる。
   *
   * [0, 4)   hash_high
   * [4, 8) generation
   * [8, 32)
   *   a. known（Proven または NonRepetitionDisproven）
   *     [8, 12)   hand1
   *     [12, 16)  hand2
   *     ...
   *     [28, 32) hand6
   *   b. unknown
   *     [8, 16)  pn
   *     [16, 24) dn
   *     [24, 28)   hand
   *     [28, 32) depth
   */
  union {
    /// known_ か unknown_ か判断できない局面
    struct {
      std::uint32_t hash_high;        ///< board_keyの上位32bit
      komori::StateGeneration s_gen;  ///< 探索世代。古いものから順に上書きされる
      std::array<std::uint32_t, 6> dummy;
    } common_;
    /// 証明済または反証済局面
    struct {
      std::uint32_t hash_high;                  ///< board_keyの上位32bit
      komori::StateGeneration s_gen;            ///< 探索世代。古いものから順に上書きされる
      std::array<Hand, kTTEntryHandLen> hands;  ///< 証明駒または反証駒
    } known_;
    /// 証明済でも反証済でもない局面
    struct {
      std::uint32_t hash_high;        ///< board_keyの上位32bit
      komori::StateGeneration s_gen;  ///< 探索世代。古いものから順に上書きされる
      PnDn pn, dn;                    ///< pn, dn。直接参照禁止。
      Hand hand;                      ///< 攻め方のhand。pn==0なら証明駒、dn==0なら反証駒を表す。
      Depth depth;  ///< 探索深さ。千日手回避のためにdepthが違う局面は別局面として扱う
    } unknown_;
  };

  static_assert(sizeof(common_) == sizeof(unknown_));
  static_assert(sizeof(common_) == sizeof(known_));
};

/**
 * @brief TTEntry のうち、hash の下位ビットが近いものを集めたデータ構造
 *
 * entry は `hash_high` の昇順でソートされた状態で格納する。
 */
class TTCluster {
 public:
  static inline constexpr std::size_t kClusterSize = 128;
  using Iterator = TTEntry*;
  using ConstIterator = const TTEntry*;

  Iterator begin() { return &(data_[0]); }
  ConstIterator begin() const { return &(data_[0]); }
  Iterator end() { return begin() + size_; }
  ConstIterator end() const { return begin() + size_; }

  /// `hash_high` 以上となる最初の entry を返す
  Iterator LowerBound(std::uint32_t hash_high);
  /// `hash_high` より大きい最初の entry を返す
  Iterator UpperBound(std::uint32_t hash_high);
  /// `entry` が cluster 内に存在するアドレスかどうかをチェックする
  bool DoesContain(Iterator entry) const { return begin() <= entry && entry < end(); }
  /// 格納している enyry の個数
  std::size_t Size() const { return size_; }
  /// entry をすべて削除する
  void Clear() { size_ = 0; }
  void Sweep();

  /// 新たな entry をクラスタに追加する。クラスタに空きがない場合は、最も必要なさそうなエントリを削除する
  Iterator Add(TTEntry&& entry);

  /**
   * @brief 条件に合致するエントリを探す。
   *
   * もし条件に合致するエントリが見つからなかった場合、新規作成して cluster に追加する。
   */
  Iterator LookUpWithCreation(std::uint32_t hash_high, Hand hand, Depth depth);
  /**
   * @brief 条件に合致するエントリを探す。
   *
   * もし条件に合致するエントリが見つからなかった場合、cluster には追加せずダミーの entry を返す。
   * ダミーの entry は、次回の LookUpWithoutCreation() 呼び出しするまでの間だけ有効である。
   */
  Iterator LookUpWithoutCreation(std::uint32_t hash_high, Hand hand, Depth depth);

  /**
   * @brief `proof_hand` による詰みを報告する
   *
   * @caution
   * 高速化のために、「hash_high 以外のエントリを消すケースは発生しない」という仮定を置いている。
   * すなわち、既存エントリが kProven に変化するケースは考慮しているが、エントリを増やす操作は考慮していない。
   */
  void SetProven(std::uint32_t hash_high, Hand proof_hand);
  /// `disproof_hand` による不詰を報告する
  void SetDisproven(std::uint32_t hash_high, Hand disproof_hand);

 private:
  /// LookUpWithCreation() と LookUpWithoutCreation() の実装本体。
  template <bool kCreateIfNotExist>
  Iterator LookUp(std::uint32_t hash_high, Hand hand, Depth depth);

  void RemoveLeastUsefulEntry();
  /// size_ == kCluster のとき専用のLowerBound実装
  Iterator LowerBoundAll(std::uint32_t hash_high);
  /// size_ < kCluster のとき専用のLowerBound実装
  Iterator LowerBoundPartial(std::uint32_t hash_high);

  std::size_t size_;
  std::array<TTEntry, kClusterSize> data_;
};

}  // namespace komori

#endif  // TTENTRY_HPP_