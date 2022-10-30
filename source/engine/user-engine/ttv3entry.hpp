#ifndef KOMORI_TTV3_ENTRY_HPP_
#define KOMORI_TTV3_ENTRY_HPP_

#include <cstdint>

#include "mate_len.hpp"
#include "typedefs.hpp"

namespace komori {
namespace ttv3 {
/// 探索量。TTでエントリを消す際の判断に用いる。
using SearchAmount = std::uint32_t;

namespace detail {
/// 詰み／不詰の探索量のボーナス。これを大きくすることで詰み／不詰エントリが消されづらくなる。
constexpr inline SearchAmount kFinalAmountBonus{1000};
}  // namespace detail

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
 * デフォルトコンストラクト直後は無効値がセットされる。エントリが無効状態のとき、以下の関数のみコールすることができる。
 *
 * - Init()
 * - SetNull()
 * - IsNull()
 * - IsFor(key, hand)
 *
 * 上記以外の関数をコールしたときの挙動は未定義なので注意すること。
 *
 * ### 千日手判定
 *
 * 千日手は局面自体ではなく経路（path）に依存する概念なので、Entry 内で直接千日手状態を保存することはない。代わりに、
 * 「千日手の可能性があるか」のフラグを保持する。このフラグの有無により千日手判定の別処理を行うかどうかを決める。
 *
 * `Init()` 直後は「千日手可能性フラグ」は立っていない。明示的に `SetPossibleRepetition()` をコールすることでのみ
 * 千日手フラグを建てることができる。千日手フラグは `IsPossibleRepetition()` により取得できる。
 *
 * ### 詰み／不詰の保存方法
 *
 * 余詰探索で「n手詰以下 m手詰み以上」という状態を扱いたいので、詰み、不詰、探索中情報を同時に持てるようにする。
 * すなわち、単純に pn/dn を持つのに加え、詰みの上界 `proven_` と不詰の下界 `disproven_` を保持する。これは、例えば
 * n手以下の詰みだと分かっている局面において、さらに探索を延長して n-1 手以下で詰まないことを示す際に用いる。
 *
 * 整理すると、以下のようになる。
 *
 * - `proven_.len` 手以上：詰み
 * - `disproven_.len` 手より大きく `proven_.len` 手未満：不明（探索中）
 * - `disproven_.len` 手以下：不詰
 *
 * `Init()` 直後は、-0手不詰、+∞手詰みで初期化する。こうすることで、任意の非負有限手に対し不明（探索中）の状態に
 * 設定できる。
 *
 * 詰み手順の復元を容易にするために、詰み／不詰局面では最善手を保存する。もし最善手を保存しておかないと、詰み手順の
 * 復元時に追加の探索が必要になる場合がある。
 *
 * ### Look Up
 *
 * 探索結果の取得には関数 `LookUp()` を用いる。`LookUp()` は優等性や劣等性を生かして pn, dn を取得する関数である。
 * 優等局面や劣等局面に対し、以下の性質が成り立つ。
 *
 * 1. 劣等局面は優等局面と比べて詰みを示しづらい（王手回避の合法手が増えるので）
 * 2. 優等局面は劣等局面と比べて不詰を示しづらい（王手の合法手が増えるので）
 * 3. （現局面から見て）劣等局面がn手以下で詰みなら、現局面もn手以下で詰み
 * 4. （現局面から見て）優等局面がn手以上で不詰なら、現局面もn手以上で不詰
 *
 * このように、`hand_` が現局面と一致しない局面の情報から詰み／不詰を導いたり、証明数や反証数の更新をすることができる。
 *
 * なお、1, 2 に関しては活用方法しだいで無限ループに陥る可能性があるので注意。
 *
 * ### 探索量
 *
 * Entry には探索量（amount）が格納されている。これは、置換表領域が不足したときに消すエントリを選ぶ際の基準にする。
 * つまり、複数の削除候補のエントリがある場合、最も探索量の小さいエントリを消すことで削除による情報の損失を抑える。
 *
 * 古い詰将棋エンジンの Small Tree GC に似ているが、「探索量」は厳密に木のサイズをカウントすることはしない。これは、
 * 木のサイズのカウントを行うと、二重カウント問題が発生したときに探索量がオーバーフローしてしまい正しく不要なエントリを
 * 消せなくなってしまうためである。そのため、「探索量」は実際の木のサイズよりも値が小さくなるよう注意する必要がある。
 *
 * また、詰み／不詰局面は他の局面よりも大事なのでなるべく消されづらくしたい。そのため、探索量に
 * 定数（kFinalAmountBonus）を足して実際の探索量よりも大きくなるようにしている。
 *
 * 探索量同士の比較には `AmountComparer` を用いる。`AmountComparer` はエントリの探索量に対し `operator<` に相当する
 * 比較演算子を提供する構造体である。
 */
class alignas(64) Entry {
 public:
  /**
   * @brief `Entry` に対する探索量に基づく比較器。
   *
   * 探索量が小さいエントリがより `less` になるような `<` に相当する演算子を提供する
   */
  struct AmountComparer {
    /**
     * @brief 2つのエントリの探索量が `lhs.amount < rhs.amount` かどうかを判定する
     * @param lhs エントリ1
     * @param rhs エントリ2
     * @return エントリ1の探索量がエントリ2の探索量より少なければ `true`
     */
    constexpr bool operator()(const Entry& lhs, const Entry& rhs) const noexcept { return lhs.amount_ < rhs.amount_; }
  };

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
    proven_.len = kInfiniteMateLen16;
    // len を初期化すれば best_move の初期化は不要
    // proven_.best_move = MOVE_NONE;
    disproven_.len = kMinusZeroMateLen16;
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
  constexpr bool IsNull() const noexcept { return hand_ == kNullHand; }

  /**
   * @brief 保存されている情報が `board_key` のものかどうか判定する
   * @param board_key 盤面ハッシュ値
   * @return エントリが `board_key` のものなら `true`
   * @pre `!IsNull()`
   */
  constexpr bool IsFor(Key board_key) const noexcept { return board_key_ == board_key; }
  /**
   * @brief 保存されている情報が (`board_key`, `hand`) のものかどうか判定する
   * @param board_key 盤面ハッシュ値
   * @param hand      持ち駒
   * @return エントリが (`board_key`, `hand`) のものなら `true`
   *
   * `IsFor(board_key)` の条件に加え、`hand` が一致するかどうかの判定を行う。
   *
   * @note エントリが無効かどうかは `hand` に格納されているので、無効状態でも正しく動作する
   */
  constexpr bool IsFor(Key board_key, Hand hand) const noexcept { return board_key_ == board_key && hand_ == hand; }

  /// 探索量
  constexpr SearchAmount Amount() const noexcept { return amount_; }
  /// 現局面の持ち駒
  constexpr Hand GetHand() const noexcept { return hand_; }

  /**
   * @brief 千日手の可能性ありフラグの設定および pn/dn の再初期化を行う。
   * @pre `!IsNull()`
   */
  constexpr void SetPossibleRepetition() noexcept {
    repetition_state_ = RepetitionState::kPossibleRepetition;
    // 千日手探索中の pn/dn は信用できないのでいったん初期化し直す
    pn_ = dn_ = 1;
  }

  /**
   * @brief 千日手可能性フラグが立っているかどうか判定する。
   * @pre `!IsNull()`
   */
  constexpr bool IsPossibleRepetition() const noexcept {
    return repetition_state_ == RepetitionState::kPossibleRepetition;
  }

  /**
   * @brief 探索結果を書き込む（未解決局面）
   * @param depth  探索深さ
   * @param pn     pn
   * @param dn     dn
   * @param len    探索中の詰み手数
   * @param amount 探索量
   * @pre `IsFor(board_key, hand)` （`board_key`, `hand` は現局面の盤面ハッシュ、持ち駒）
   */
  constexpr void UpdateUnknown(Depth depth, PnDn pn, PnDn dn, MateLen16 len, SearchAmount amount) noexcept {
    const auto depth16 = static_cast<std::int16_t>(depth);
    min_depth_ = std::min(min_depth_, depth16);
    AddAmount(amount);

    // 明らかに詰み／不詰ならデータを更新しない
    if (len < proven_.len && disproven_.len < len) {
      pn_ = pn;
      dn_ = dn;
    }
  }

  /**
   * @brief 探索結果を書き込む（詰み局面）
   * @param len       詰み手数
   * @param best_move 最善手
   * @param amount    探索量
   * @pre `IsFor(board_key, hand)` （`board_key`, `hand` は現局面の盤面ハッシュ、持ち駒）
   * @pre `len` > `disproven_.len`
   */
  constexpr void UpdateProven(MateLen16 len, Move best_move, SearchAmount amount) noexcept {
    AddAmount(SaturatedAdd(amount, detail::kFinalAmountBonus));
    if (len < proven_.len) {
      proven_.len = len;
      proven_.best_move = Move16{best_move};
    }
  }

  /**
   * @brief 探索結果を書き込む（不詰局面）
   * @param len       不詰手数
   * @param best_move 最善手
   * @param amount    探索量
   * @pre `IsFor(board_key, hand)` （`board_key`, `hand` は現局面の盤面ハッシュ、持ち駒）
   * @pre `len` < `proven.len`
   */
  constexpr void UpdateDisproven(MateLen16 len, Move best_move, SearchAmount amount) noexcept {
    AddAmount(SaturatedAdd(amount, detail::kFinalAmountBonus));
    if (len > disproven_.len) {
      disproven_.len = len;
      disproven_.best_move = Move16{best_move};
    }
  }

  /**
   * @brief pn, dn などの探索情報を取得する
   * @param hand   持ち駒
   * @param depth  探索深さ
   * @param len    見つけたい詰み手数
   * @param pn     pn
   * @param dn     dn
   * @param use_old_child unproven old childフラグ
   * @return 引数の値が更新されていれば `true`
   * @pre `IsFor(board_key)` （`board_key` は現局面の盤面ハッシュ）
   *
   * 置換表の肝の部分。本将棋エンジンとは異なり、優等局面、劣等局面の結果をチラ見しながら pn 値と dn 値を取得する。
   * 外側のループ脱出の判断をできるだけ高速にしたいので、引数の値が更新されたかどうかを戻り値として戻す。
   */
  constexpr bool LookUp(Hand hand, Depth depth, MateLen16& len, PnDn& pn, PnDn& dn, bool& use_old_child) noexcept {
    bool update = false;
    const auto depth16 = static_cast<std::int16_t>(depth);
    if (hand_ == hand) {
      // このタイミングで最小距離を更新しておかないと無限ループになる可能性があるので注意
      min_depth_ = std::min(min_depth_, depth16);
    }

    // LookUpしたい局面は Entry に保存されている局面の優等局面
    const bool is_superior = hand_is_equal_or_superior(hand, hand_);
    if (is_superior) {
      if (len >= proven_.len) {
        // 優等局面は高々 `proven_.len` 手詰み。
        len = proven_.len;
        pn = 0;
        dn = kInfinitePnDn;
        return true;
      }

      if (hand_ == hand || min_depth_ <= depth16) {
        if (dn < dn_) {
          update = true;
          dn = dn_;
          if (min_depth_ < depth16) {
            // unproven old child の情報を使ったときはフラグを立てておく
            use_old_child = true;
          }
        }
      }
    }

    // LookUpしたい局面は Entry に保存されている局面の劣等局面
    const bool is_inferior = hand_is_equal_or_superior(hand_, hand);
    if (is_inferior) {
      if (len <= disproven_.len) {
        // 劣等局面は少なくとも `disproven_.len` 手不詰。
        len = disproven_.len;
        pn = kInfinitePnDn;
        dn = 0;
        return true;
      }

      if (hand_ == hand || min_depth_ <= depth16) {
        if (pn < pn_) {
          pn = pn_;
          update = true;
          if (min_depth_ < depth16) {
            // unproven old child の情報を使ったときはフラグを立てておく
            use_old_child = true;
          }
        }
      }
    }

    return update;
  }

  // <テスト用>
  // UpdateXxx() や LookUp() など、外部から変数が観測できないとテストの際にかなり不便なので、Getter を用意しておく。

  /// 最小距離
  constexpr Depth MinDepth() const noexcept { return static_cast<Depth>(min_depth_); }
  /// 詰み手数
  constexpr MateLen16 ProvenLen() const noexcept { return proven_.len; }
  /// 不詰手数
  constexpr MateLen16 DisprovenLen() const noexcept { return disproven_.len; }
  /// pn
  constexpr PnDn Pn() const noexcept { return pn_; }
  /// dn
  constexpr PnDn Dn() const noexcept { return dn_; }
  // </テスト用>

 private:
  /**
   * @brief 探索量に `amount` を加える
   * @param amount 追加する探索量
   *
   * @note 加算方法を変える可能性が高い（例えば上限値を変えるなど）ので、関数化しておく。
   */
  constexpr void AddAmount(SearchAmount amount) noexcept { amount_ = SaturatedAdd(amount_, amount); }

  /// 「千日手の可能性」を表現するための列挙体
  enum class RepetitionState : std::uint8_t {
    kNone,                ///< 千日手は未検出
    kPossibleRepetition,  ///< 千日手検出済
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
  std::int16_t min_depth_;            ///< 最小探索深さ
  RepetitionState repetition_state_;  ///< 現局面が千日手の可能性があるか

  std::uint64_t secret_;  ///< 秘密の値
};

static_assert(sizeof(Entry) <= 64, "The size of `Entry` must be less than or equal to 64 bytes.");
static_assert(alignof(Entry) == 64, "`Entry` must be aligned as 64 bytes.");
static_assert(std::is_default_constructible<Entry>(), "`Entry` must be default constructible");
}  // namespace ttv3
}  // namespace komori

#endif  // KOMORI_TTV3_ENTRY_HPP_
