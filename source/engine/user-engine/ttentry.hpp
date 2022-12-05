/**
 * @file ttentry.hpp
 */
#ifndef KOMORI_TTENTRY_HPP_
#define KOMORI_TTENTRY_HPP_

#include <cstdint>

#include "bitset.hpp"
#include "hands.hpp"
#include "mate_len.hpp"
#include "typedefs.hpp"

namespace komori::tt {
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
 * 他のクラスよりも可読性を犠牲にしているため、普段よりも仕様を詳細に記す。
 *
 * `Entry` は以下のように 64 bytes で構成されている。キャッシュで悪さをさせないように、64 バイトにアラインさせる。
 *
 * ```
 *                       1      2      3      4      5      6      7      8
 * alignas(64)->      +------+------+------+------+------+------+------+------+
 *                  0 |           hand_           |          amount_          |
 *                    +------+------+------+------+------+------+------+------+
 *                  8 |                      board_key_                       |
 *                    +------+------+------+------+------+------+------+------+
 *                 16 | proven_len_ |disproven_len|          (unused)         |
 *                    +------+------+------+------+------+------+------+------+
 *                 24 |                          pn_                          |
 *                    +------+------+------+------+------+------+------+------+
 *                 32 |                          dn_                          |
 *                    +------+------+------+------+------+------+------+------+
 *                 40 |  min_depth_ | rep  | ==== |        parent_hand_       |
 *                    +------+------+------+------+------+------+------+------+
 *                 48 |                  parent_board_key_                    |
 *                    +------+------+------+------+------+------+------+------+
 *                 56 |                       sum_mask_                       |
 *                    +------+------+------+------+------+------+------+------+
 * ```
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
 * 千日手フラグを立てることができる。千日手フラグは `IsPossibleRepetition()` により取得できる。
 *
 * ### 詰み／不詰の保存方法
 *
 * 余詰探索で「n手詰以下 m手詰み以上」という状態を扱いたいので、詰み、不詰、探索中情報を同時に持てるようにする。
 * すなわち、単純に pn/dn を持つのに加え、詰みの上界 `proven_len_` と不詰の下界 `disproven_len_` を保持する。これは、
 * 例えばn手以下の詰みだと分かっている局面において、さらに探索を延長して n-1 手以下で詰まないことを示す際に用いる。
 *
 * 整理すると、以下のようになる。
 *
 * - `proven_len_` 手以上：詰み
 * - `disproven_len_` 手より大きく `proven_len_` 手未満：不明（探索中）
 * - `disproven_len_` 手以下：不詰
 *
 * `Init()` 直後は、-1手不詰、+∞手詰みで初期化する。こうすることで、任意の非負有限手に対し不明（探索中）の状態に
 * 設定できる。
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
 * ### SumMask
 *
 * 詰将棋探索では、pn/dn の二重カウントによる発散を防ぐために、δ値の和を取るべき箇所を max で代用したい場面がある。
 * SumMask は、現局面の子ノードのうちδ値を和で計算すべき子の集合を表す。この値は UpdateUnknown() で更新される。
 *
 * SumMask のデフォルト値は `BitSet64::Full()` である。すなわち、初期状態はすべての子のδ値を和で計算する。
 * この値は探索部に依存する値なので、このファイル内で初期化を行っているのは本当は良くない。
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
   */
  constexpr void Init(Key board_key, Hand hand) noexcept {
    hand_ = hand;
    amount_ = 1;
    board_key_ = board_key;
    proven_len_ = kDepthMaxPlus1MateLen16;
    disproven_len_ = kMinus1MateLen16;

    pn_ = 1;
    dn_ = 1;
    min_depth_ = static_cast<std::int16_t>(kDepthMax);
    repetition_state_ = RepetitionState::kNone;

    parent_hand_ = kNullHand;
    parent_board_key_ = kNullKey;
    sum_mask_ = BitSet64::Full();
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
  constexpr bool IsFor(Key board_key, Hand hand) const noexcept {
    // hand を先にチェックしたほうが微妙に高速
    return hand_ == hand && board_key_ == board_key;
  }

  /// 探索量
  constexpr SearchAmount Amount() const noexcept { return amount_; }
  /// 現局面の持ち駒
  constexpr Hand GetHand() const noexcept { return hand_; }
  /// 親局面の盤面ハッシュ値
  constexpr Key GetParentBoardKey() const noexcept { return parent_board_key_; }
  /// 親局面の持ち駒
  constexpr Hand GetParentHand() const noexcept { return parent_hand_; }
  /// δ値を和で計算すべき子の集合
  constexpr BitSet64 SumMask() const noexcept { return sum_mask_; }
  /// 盤面ハッシュ値（コンパクション用）
  constexpr Key BoardKey() const noexcept { return board_key_; }

  /// 探索量を小さくする。ただし 0 以下にはならない。
  constexpr void CutAmount() noexcept { amount_ = std::max<SearchAmount>(amount_ / 2, 1); }

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
   * @param amount 探索量
   * @param sum_mask δ値を和で計算する子の集合
   * @param parent_board_key 親局面の盤面ハッシュ値
   * @param parent_hand 親局面の攻め方の持ち駒
   * @pre `IsFor(board_key, hand)` （`board_key`, `hand` は現局面の盤面ハッシュ、持ち駒）
   */
  constexpr void UpdateUnknown(Depth depth,
                               PnDn pn,
                               PnDn dn,
                               SearchAmount amount,
                               BitSet64 sum_mask,
                               Key parent_board_key,
                               Hand parent_hand) noexcept {
    const auto depth16 = static_cast<std::int16_t>(depth);
    min_depth_ = std::min(min_depth_, depth16);
    pn_ = pn;
    dn_ = dn;
    parent_board_key_ = parent_board_key;
    parent_hand_ = parent_hand;
    sum_mask_ = sum_mask;
    amount_ = std::max(amount_, amount);
  }

  /**
   * @brief 探索結果を書き込む（詰み局面）
   * @param len       詰み手数
   * @param amount    探索量
   * @pre `IsFor(board_key, hand)` （`board_key`, `hand` は現局面の盤面ハッシュ、持ち駒）
   * @pre `len` > `disproven_len_`
   */
  constexpr void UpdateProven(MateLen16 len, SearchAmount amount) noexcept {
    KOMORI_PRECONDITION(disproven_len_ < len);
    proven_len_ = std::min(proven_len_, len);
    amount_ = std::max(amount_, SaturatedAdd(amount, detail::kFinalAmountBonus));
  }

  /**
   * @brief 探索結果を書き込む（不詰局面）
   * @param len       不詰手数
   * @param amount    探索量
   * @pre `IsFor(board_key, hand)` （`board_key`, `hand` は現局面の盤面ハッシュ、持ち駒）
   * @pre `len` < `proven_len_`
   */
  constexpr void UpdateDisproven(MateLen16 len, SearchAmount amount) noexcept {
    KOMORI_PRECONDITION(len < proven_len_);
    disproven_len_ = std::max(disproven_len_, len);
    amount_ = std::max(amount_, SaturatedAdd(amount, detail::kFinalAmountBonus));
  }

  /**
   * @brief pn, dn などの探索情報を取得する
   * @param hand   持ち駒
   * @param depth  探索深さ
   * @param len    見つけたい詰み手数
   * @param pn     pn
   * @param dn     dn
   * @param use_old_child unproven old childフラグ
   * @return 引数の値が更新されているか `IsFor(board_key_, hand_)` なエントリが存在すれば `true`
   * @pre `IsFor(board_key)` （`board_key` は現局面の盤面ハッシュ）
   *
   * 置換表の肝の部分。本将棋エンジンとは異なり、優等局面、劣等局面の結果をチラ見しながら pn 値と dn 値を取得する。
   * 外側のループ脱出の判断をできるだけ高速にしたいので、引数の値が更新されたときと現局面に一致するエントリを見つけた
   * ときは `true` を返す。
   *
   * ## 実装詳細
   *
   * 探索中にとても頻繁に呼ばれる関数なので限界まで高速化したい。そのため、シンプルな処理ながら関数の実装は
   * 長めになっている。実装の本体は次の3つの関数に分割されている。
   *
   * - LookUpExact():  現局面とエントリが一致
   * - LookUpInferior(): 現局面が劣等局面
   * - LookUpSuperior(): 現局面が優等局面
   *
   * 細かいことを言うと、
   *     現局面とエントリが一致 <=> 現局面が優等局面 ∧ 現局面が劣等局面
   * が成り立つので LookUpUnknown() はそれほど必要ではないが、実際には優等局面・劣等局面よりも一致局面のほうが
   * 頻繁に現れ、かつ容易に一致局面かどうかの判定ができるため、別処理にしたほうが高速化できる。
   */
  constexpr bool LookUp(Hand hand, Depth depth, MateLen16& len, PnDn& pn, PnDn& dn, bool& use_old_child)
      const noexcept {
    const auto depth16 = static_cast<std::int16_t>(depth);

    // 1. 現局面とエントリが一致
    if (hand_ == hand) {
      return LookUpExact(depth16, len, pn, dn, use_old_child);
    }

    // 2. 現局面が劣等局面
    const bool is_inferior = hand_is_equal_or_superior(hand_, hand);
    if (is_inferior) {
      // 劣等かつ優等な局面は一致局面だけ。つまり 3. の if 文は省略できる。
      return LookUpInferior(depth16, len, pn, dn, use_old_child);
    }

    // 3. 現局面が優等局面
    const bool is_superior = hand_is_equal_or_superior(hand, hand_);
    if (is_superior) {
      return LookUpSuperior(depth16, len, pn, dn, use_old_child);
    }

    // 優等でも劣等でもない局面。何もせずに返る
    return false;
  }

  /**
   * @brief `hand` に対応する親局面を取得する
   * @param hand 現局面の持ち駒
   * @param pn pn
   * @param dn dn
   * @param parent_board_key 親局面の盤面ハッシュ値
   * @param parent_hand 親局面の持ち駒
   */
  constexpr void UpdateParentCandidate(Hand hand, PnDn& pn, PnDn& dn, Key& parent_board_key, Hand& parent_hand) const {
    const bool is_inferior = hand_is_equal_or_superior(hand_, hand);
    const bool is_superior = hand_is_equal_or_superior(hand, hand_);

    if (is_inferior && pn_ > pn) {
      pn = pn_;
      if (parent_hand_ != kNullHand && (parent_hand == kNullHand || pn > dn)) {
        parent_board_key = parent_board_key_;
        parent_hand = ApplyDeltaHand(parent_hand_, hand_, hand);
      }
    }

    if (is_superior && dn_ > dn) {
      dn = dn_;
      if (parent_hand_ != kNullHand && (parent_hand == kNullHand || dn > pn)) {
        parent_board_key = parent_board_key_;
        parent_hand = ApplyDeltaHand(parent_hand_, hand_, hand);
      }
    }
  }

  /**
   * @brief 詰み／不詰手数専用の LookUp()
   * @param hand          現局面の持ち駒
   * @param disproven_len 現在の不詰手数
   * @param proven_len    現在の詰み手数
   * @pre disproven_len < proven_len
   * @pre IsFor(board_key_)
   *
   * 詰み／不詰手数に特化した LookUp。探索結果結果の読み出しに用いる。
   *
   * 以下の 2 つの変数 `disproven_len`、`proven_len` を受け取り、これらを更新して返す。
   *
   * - 高々 `proven_len` 手で詰み
   * - 少なくとも `disproven_len` 手で詰まない
   */
  constexpr void UpdateFinalRange(Hand hand, MateLen16& disproven_len, MateLen16& proven_len) const noexcept {
    const bool is_inferior = hand_is_equal_or_superior(hand_, hand);
    const bool is_superior = hand_is_equal_or_superior(hand, hand_);

    if (is_inferior) {
      disproven_len = std::max(disproven_len, disproven_len_);
    }

    if (is_superior) {
      proven_len = std::min(proven_len, proven_len_);
    }
  }

  // <テスト用>
  // UpdateXxx() や LookUp() など、外部から変数が観測できないとテストの際にかなり不便なので、Getter を用意しておく。

  /// 最小距離
  constexpr Depth MinDepth() const noexcept { return static_cast<Depth>(min_depth_); }
  /// 詰み手数
  constexpr MateLen16 ProvenLen() const noexcept { return proven_len_; }
  /// 不詰手数
  constexpr MateLen16 DisprovenLen() const noexcept { return disproven_len_; }
  /// pn
  constexpr PnDn Pn() const noexcept { return pn_; }
  /// dn
  constexpr PnDn Dn() const noexcept { return dn_; }
  // </テスト用>

 private:
  /**
   * @brief 現局面とエントリが一致しているときの LookUp
   * @param depth16  探索深さ
   * @param len      見つけたい詰み手数
   * @param pn       pn
   * @param dn       dn
   * @return 必ず `true`
   */
  constexpr bool LookUpExact(std::int16_t depth16,
                             MateLen16& len,
                             PnDn& pn,
                             PnDn& dn,
                             bool& use_old_child) const noexcept {
    if (len >= proven_len_) {
      len = proven_len_;
      pn = 0;
      dn = kInfinitePnDn;
    } else if (len <= disproven_len_) {
      len = disproven_len_;
      pn = kInfinitePnDn;
      dn = 0;
    } else {
      min_depth_ = std::min(min_depth_, depth16);
      if (pn < pn_ || dn < dn_) {
        pn = std::max(pn, pn_);
        dn = std::max(dn, dn_);
        if (min_depth_ < depth16) {
          use_old_child = true;
        }
      }
    }

    // 現局面のエントリを見つけたときは必ず `true`
    return true;
  }

  /**
   * @brief 現局面が優等局面のときの LookUp
   * @param depth16  探索深さ
   * @param len      見つけたい詰み手数
   * @param pn       pn
   * @param dn       dn
   * @return pn/dn を更新したら `true`
   */
  constexpr bool LookUpSuperior(std::int16_t depth16,
                                MateLen16& len,
                                PnDn& pn,
                                PnDn& dn,
                                bool& use_old_child) const noexcept {
    if (len >= proven_len_) {
      // 優等局面は高々 `proven_len_` 手詰み。
      len = proven_len_;
      pn = 0;
      dn = kInfinitePnDn;
      return true;
    }

    if (min_depth_ <= depth16 && dn < dn_) {
      dn = dn_;
      if (min_depth_ < depth16) {
        // unproven old child の情報を使ったときはフラグを立てておく
        use_old_child = true;
      }
      return true;
    }
    return false;
  }

  /**
   * @brief 現局面が劣等局面のときの LookUp
   * @param depth16  探索深さ
   * @param len      見つけたい詰み手数
   * @param pn       pn
   * @param dn       dn
   * @return pn/dn を更新したら `true`
   */
  constexpr bool LookUpInferior(std::int16_t depth16,
                                MateLen16& len,
                                PnDn& pn,
                                PnDn& dn,
                                bool& use_old_child) const noexcept {
    // LookUpしたい局面は Entry に保存されている局面の劣等局面
    if (len <= disproven_len_) {
      // 劣等局面は少なくとも `disproven_len_` 手不詰。
      len = disproven_len_;
      pn = kInfinitePnDn;
      dn = 0;
      return true;
    }

    if (min_depth_ <= depth16 && pn < pn_) {
      pn = pn_;
      if (min_depth_ < depth16) {
        // unproven old child の情報を使ったときはフラグを立てておく
        use_old_child = true;
      }

      return true;
    }

    return false;
  }

  /// 「千日手の可能性」を表現するための列挙体
  enum class RepetitionState : std::uint8_t {
    kNone,                ///< 千日手は未検出
    kPossibleRepetition,  ///< 千日手検出済
  };

  Hand hand_{kNullHand};  ///< 現局面の持ち駒（コンストラクト時は無効値をセット）
  SearchAmount amount_;   ///< 現局面の探索量
  Key board_key_;         ///< 盤面ハッシュ値

  MateLen16 proven_len_;     ///< 詰み手数
  MateLen16 disproven_len_;  ///< 不詰手数

  PnDn pn_;  ///< pn値
  PnDn dn_;  ///< dn値

  mutable std::int16_t min_depth_;  ///< 最小探索深さ。`LookUp()` 中に書き換える可能性があるので mutable。
  RepetitionState repetition_state_;  ///< 現局面が千日手の可能性があるか

  Hand parent_hand_;      ///< 親局面の持ち駒
  Key parent_board_key_;  ///< 親局面の盤面ハッシュ値
  BitSet64 sum_mask_{};   ///< δ値を和で計算する子の集合
};

static_assert(sizeof(SearchAmount) == 4, "The size of SearchAmount must be 4.");
static_assert(sizeof(Entry) <= 64, "The size of `Entry` must be less than or equal to 64 bytes.");
static_assert(alignof(Entry) == 64, "`Entry` must be aligned as 64 bytes.");
static_assert(std::is_default_constructible<Entry>(), "`Entry` must be default constructible");
}  // namespace komori::tt

#endif  // KOMORI_TTENTRY_HPP_
