#ifndef KOMORI_TTV3_HPP_
#define KOMORI_TTV3_HPP_

#include <cstdint>

#include "mate_len.hpp"
#include "typedefs.hpp"

namespace komori {
namespace ttv3 {
/// 探索量。TTでエントリを消す際の判断に用いる。
using SearchAmount = std::uint32_t;

/**
 * @brief 局面の探索結果を保続するための基本クラス。
 *
 * 1局面の探索結果を置換表に保存する際の単位となるクラス。
 *
 * 実行速度を高めるために可読性や保守性を犠牲にして1クラスに機能を詰め込んでいる。置換表の Look Up は詰将棋探索において
 * 最もよく使う機能であるため、泥臭く高速化することで全体の性能向上につながる。
 *
 * また、実行速度向上と置換表サイズの節約のために 64 バイトに収まるようにデータを詰め込む。
 *
 * ## 実装詳細
 *
 * 他のクラスよりも可読性を犠牲にしているため、普段よりも詳細に関数仕様を記す。
 *
 * ### 初期化
 *
 * 配列により一気にメモリ確保したいので、デフォルトコンストラクト可能にする。
 *
 * エントリを初めて使う際は、`Init()` により初期化を行う。これにより、必要なメンバ変数に初期値設定が行われ、
 * エントリが使える状態になる。エントリがもう必要なくなった場合、`SetNull()` により無効値をセットできる。
 * 無効値がセットされたエントリは `Init()` によりまた上書きして使用することができる。
 *
 * ### 無効値判定
 *
 * 無効値の判定は `hand_` の値が `kNullHand` かどうかを調べることにより行う。無効値の判定は TT のガベージコレクションや
 * 空きエントリの探索など、探索中にとても頻繁に行いたいので、先頭アドレスに無効値判定のための情報を置いている。
 *
 * 以前は `board_key_ == kNullKey` で判定していたが、1/2^64 の確率で誤検知してしまう欠点があった。
 * 合法局面では `hand_` が `kNullHand` と一致することはないので、`hand_` に無効値を格納する方が優れている。
 *
 * デフォルトコンストラクト直後は無効値がセットされる。
 */
class alignas(64) Entry {
 public:
  /// Default constructor(default)
  Entry() noexcept = default;
  /// Copy constructor(default)
  constexpr Entry(const Entry&) noexcept = default;
  /// Move constructor(default)
  constexpr Entry(Entry&&) noexcept = default;
  /// Copy assign operator(default)
  Entry& operator=(const Entry&) noexcept = default;
  /// Move assign operator(default)
  Entry& operator=(Entry&&) noexcept = default;
  /// Destructor(default)
  ~Entry() noexcept = default;

  /**
   * @brief エントリの初期化を行う
   * @param board_key 盤面ハッシュ値
   * @param hand      持ち駒
   * @param depth     探索深さ
   * @param pn        pn
   * @param dn        dn
   * @param amount    探索量
   */
  constexpr void Init(Key board_key, Hand hand, Depth depth, PnDn pn, PnDn dn, SearchAmount amount) noexcept {
    // 高速化のために初期化をサボれるところではサボる

    hand_ = hand;
    amount_ = amount;
    board_key_ = board_key;
    proven_.len = kMaxMateLen16;
    // len を初期化すれば best_move の初期化は不要
    // proven_.best_move = MOVE_NONE;
    disproven_.len = kZeroMateLen16;
    // len を初期化すれば best_move の初期化は不要
    // disproven_.best_move = MOVE_NONE;
    pn_ = pn;
    dn_ = dn;
    // parent_hand に無効値が入っていれば parent_board_key の初期化は不要
    // parent_board_key_ = kNullKey;
    parent_hand_ = kNullHand;
    min_depth_ = static_cast<std::int16_t>(depth);
    repetition_state_ = RepetitionState::kNone;
    secret_ = 0;
  }

  /// エントリに無効値を設定する
  constexpr void SetNull() noexcept { hand_ = kNullHand; }
  /// エントリが未使用状態かを判定する
  constexpr bool IsNull() noexcept { return hand_ == kNullHand; }

 private:
  /// 「千日手の可能性」を表現するための列挙体
  enum class RepetitionState : std::uint8_t {
    kNone,           ///< 千日手は未検出
    kMayRepetition,  ///< 千日手検出済
  };

  Hand hand_{kNullHand};  ///< 現局面の持ち駒（コンストラクト時は無効値をセット）
  SearchAmount amount_;   ///< 現局面の探索量
  Key board_key_;         ///< 盤面ハッシュ値

  struct {
    MateLen16 len;     ///< 詰み手数
    Move16 best_move;  ///< 最善手
  } proven_;           ///< 詰み情報

  struct {
    MateLen16 len;     ///< 不詰手数
    Move16 best_move;  ///< 最善手
  } disproven_;        ///< 不詰情報

  PnDn pn_;  ///< pn値
  PnDn dn_;  ///< dn値

  Key parent_board_key_;              ///< 親局面の盤面ハッシュ値
  Hand parent_hand_;                  ///< 親局面の持ち駒
  std::int16_t min_depth_;            ///< 探索深さ
  RepetitionState repetition_state_;  ///< 現局面が千日手の可能性があるか

  std::uint64_t secret_;  ///< 秘密の値
};

static_assert(sizeof(Entry) <= 64, "The size of `Entry` must be less than or equal to 64 bytes.");
static_assert(alignof(Entry) == 64, "`Entry` must be aligned as 64 bytes.");
static_assert(std::is_default_constructible<Entry>(), "`Entry` must be default constructible");
}  // namespace ttv3
}  // namespace komori

#endif  // KOMORI_TTV3_HPP_
