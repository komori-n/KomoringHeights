/**
 * @file ttquery.hpp
 */
#ifndef KOMORI_TTQUERY_HPP_
#define KOMORI_TTQUERY_HPP_

#include "initial_estimation.hpp"
#include "repetition_table.hpp"
#include "search_result.hpp"
#include "ttentry.hpp"
#include "typedefs.hpp"

namespace komori {
namespace tt {
/**
 * @brief `Query` の読み書きで使用する `Entry` の連続領域を指す構造体
 *
 * 連続した `kSize` 個の `Entry` を持つ。クラスタの読み書きをする際、[`head_entry`, `head_entry + kSize`) の範囲への
 * アクセスが発生する。
 *
 * ## 実装詳細
 *
 * 連続なエントリを管理する。クラスタサイズ（`kSize`）はコンパイル時定数なので、領域へのアクセスはマクロにより
 * ループアンロールできる。（詳しくは `KOMORI_TTQUERY_UNROLL_CLUSTER` を参照）
 * この `kSize` 個のエントリは他のクラスと一部を共有する可能性がある。
 *
 * クラスタサイズが定数なので、構造体内では領域の先頭へのポインタだけを保持する。
 */
struct Cluster {
  /**
   * @brief `Query` で参照する `Entry` の塊（クラスタ）のサイズ。
   *
   * この値を大きくすることで、`Entry` が格納可能な範囲が広がるため、探索済情報が消されづらくなる。一方、この値を
   * 小さくすることで、`Entry` を探す範囲が狭まるので動作速度が向上する。
   */
  static constexpr inline std::size_t kSize{16U};

  /**
   * @brief クラスタの先頭へのポインタ。[`head_entry`, `head_entry + kSize`) の領域を使用する
   *
   * @note ムーブコンストラクト可能にするため `const` はあえて付与しない
   */
  Entry* head_entry;
};

/**
 * @brief Cluster に関する for ループのループをアンロールするマクロ。
 *
 * ttv3query.hpp 内でのみ使用可能。
 * for 文で Cluster::kSize 周回す時間も惜しいので、マクロを用いてゴリ押しでループ展開を行う。
 *
 * @note よくやるテンプレート+lambda式ではなくマクロを用いてループ展開を行う理由は、ループの途中で early return
 * できるようにするため。
 */
#define KOMORI_TTQUERY_UNROLL_CLUSTER(func) \
  do {                                      \
    static_assert(Cluster::kSize == 16);    \
    func(0);                                \
    func(1);                                \
    func(2);                                \
    func(3);                                \
    func(4);                                \
    func(5);                                \
    func(6);                                \
    func(7);                                \
    func(8);                                \
    func(9);                                \
    func(10);                               \
    func(11);                               \
    func(12);                               \
    func(13);                               \
    func(14);                               \
    func(15);                               \
  } while (false)

/**
 * @brief 連続する複数エントリを束ねてまとめて読み書きするためのクラス。
 *
 * 詰将棋エンジンでは置換表を何回も何回も読み書きする。このクラスでは、置換表の読み書きに必要となる
 * 情報（ハッシュ値など）をキャッシュしておき、高速化することが目的である。
 *
 * ## 実装詳細
 *
 * 置換表はクラスタ（`Cluster`）という単位で読み書きを行う。この `Query` クラスでは、局面の情報とクラスタを
 * 内部で持つことで、探索結果を高速に読み書きすることができる。
 *
 * 置換表から結果を読む際、以下の情報を利用して探索結果を復元する。
 *
 * 1. 同一局面（盤面も持ち駒も一致）
 * 2. 優等局面（盤面が一致していて持ち駒が現局面より多い）
 * 3. 劣等局面（盤面が一致していて持ち駒が現局面より少ない）
 *
 * クラスタ内には経路に依存しない探索結果しか保存されていないため、千日手が疑われる局面では千日手テーブルも参照して
 * 局面の状態を求める。
 *
 * クラスタ内には、盤面が同じで持ち駒が異なる局面が大量に書かれる。また、他局面のクラスタとも領域を共有するため、
 * クラスタをすべて使い切ってしまうことがある。このような場合は、探索量（`SearchedAmount`）が最も小さなエントリを消して
 * 上書きを行う。
 */
class Query {
 public:
  /**
   * @brief Query の構築を行う。
   * @param rep_table   千日手テーブル
   * @param head_entry  クラスタの先頭へのポインタ。[head_entry, head_entry + kClusterSize) は読み書きの可能性がある。
   * @param path_key    経路ハッシュ値
   * @param board_key   盤面ハッシュ値
   * @param hand        持ち駒
   * @param depth       探索深さ
   */
  constexpr Query(RepetitionTable& rep_table, Cluster cluster, Key path_key, Key board_key, Hand hand, Depth depth)
      : rep_table_{&rep_table},
        cluster_{cluster},
        path_key_{path_key},
        board_key_{board_key},
        hand_{hand},
        depth_{depth} {};

  /**
   * @brief Default constructor(default)
   *
   * 配列で領域を確保したいためデフォルトコンストラクト可能にしておく。デフォルトコンストラクト状態では
   * メンバ関数の呼び出しは完全に禁止である。そのため、使用する前に必ず引数つきのコンストラクタで初期化を行うこと。
   */
  Query() = default;
  /// Copy constructor(delete). 使うことはないと思われるので封じておく。
  Query(const Query&) = delete;
  /// Move constructor(default)
  constexpr Query(Query&&) noexcept = delete;
  /// Copy assign operator(delete)
  Query& operator=(const Query&) = delete;
  /// Move assign operator(default)
  constexpr Query& operator=(Query&&) noexcept = default;
  /// Destructor
  ~Query() noexcept = default;

  /**
   * @brief `LookUp()` の初期値関数なし版。
   * @tparam kCreateIfNotFound   もしエントリが見つからなかった場合、新規作成するかどうか
   * @param does_have_old_child  unproven old child の結果を使った場合 `true` が書かれる変数
   * @param len 探している詰み手数
   * @return Look Up 結果
   */
  template <bool kCreateIfNotFound>
  SearchResult LookUp(bool& does_have_old_child, MateLen len) const {
    return LookUp<kCreateIfNotFound>(does_have_old_child, len, []() { return std::make_pair(kPnDnUnit, kPnDnUnit); });
  }

  // テンプレート関数のカバレッジは悲しいことになるので取らない
  // LCOV_EXCL_START

  /**
   * @brief クラスタから結果を集めてきて返す関数。
   * @tparam kCreateIfNotFound   もしエントリが見つからなかった場合、新規作成するかどうか
   * @tparam InitialEvalFunc 初期値関数の型
   * @param does_have_old_child  unproven old child の結果を使った場合 `true` が書かれる変数
   * @param len 探している詰み手数
   * @param eval_func 初期値を計算する関数。エントリが見つからなかったときのみ呼ばれる。
   * @return Look Up 結果
   *
   * クラスタを探索して現局面の探索情報を返す関数。
   *
   * 一般に、初期化関数の呼び出しは実行コストがかかる。そのため、初期化関数は `eval_func` に包んで渡す。
   * 必要な時のみ `eval_func` の呼び出しを行うことで、呼び出さないパスの高速化ができる。
   *
   * @note 詰将棋エンジンで最も最適化の効果がある関数。1回1回の呼び出し時間は大したことがないが、探索中にたくさん
   * 呼ばれるため、ループアンローリングなどの小手先の高速化でかなり全体の処理性能向上に貢献できる。
   */
  template <bool kCreateIfNotFound, typename InitialEvalFunc>
  SearchResult LookUp(bool& does_have_old_child, MateLen len, InitialEvalFunc&& eval_func) const {
    MateLen16 len16{len.To16()};
    PnDn pn = 1;
    PnDn dn = 1;
    SearchAmount amount = 1;

    Entry* itr = cluster_.head_entry;
    bool found_exact = false;

    // Doxygen によるドキュメンテーションを無効にする
#if !defined(DOXYGEN_SHOULD_SKIP_THIS)
#define LOOKUP_UNROLL_IMPL(i)                                                                  \
  do {                                                                                         \
    /* `IsFor()` -> `IsNull()` の順で呼び出すことで2%高速化 */                    \
    if (itr->IsFor(board_key_) && !itr->IsNull()) {                                            \
      if (itr->LookUp(hand_, depth_, len16, pn, dn, does_have_old_child)) {                    \
        amount = std::max(amount, itr->Amount());                                              \
        if (pn == 0) {                                                                         \
          return SearchResult::MakeFinal<true>(itr->GetHand(), MateLen::From(len16), amount);  \
        } else if (dn == 0) {                                                                  \
          return SearchResult::MakeFinal<false>(itr->GetHand(), MateLen::From(len16), amount); \
        } else if (itr->IsFor(board_key_, hand_)) {                                            \
          if (itr->IsPossibleRepetition() && rep_table_->Contains(path_key_)) {                \
            return SearchResult::MakeFinal<false, true>(hand_, len, amount);                   \
          }                                                                                    \
                                                                                               \
          found_exact = true;                                                                  \
        }                                                                                      \
      }                                                                                        \
    }                                                                                          \
    itr++;                                                                                     \
  } while (false)

    KOMORI_TTQUERY_UNROLL_CLUSTER(LOOKUP_UNROLL_IMPL);
#undef LOOKUP_UNROLL_IMPL
#endif  // !defined(DOXYGEN_SHOULD_SKIP_THIS)

    if (found_exact) {
      UnknownData unknown_data{false, kNullKey, kNullHand, 0};
      return SearchResult::MakeUnknown(pn, dn, hand_, len, amount, unknown_data);
    }

    const auto [init_pn, init_dn] = std::forward<InitialEvalFunc>(eval_func)();
    pn = std::max(pn, init_pn);
    dn = std::max(dn, init_dn);

    if constexpr (kCreateIfNotFound) {
      // このエントリに対し費やした探索量は `amount` ではなく 1 なので注意。
      CreateNewEntry(hand_, pn, dn, 1);
    }

    const UnknownData unknown_data{true, kNullKey, kNullHand, 0};
    return SearchResult::MakeUnknown(pn, dn, hand_, len, amount, unknown_data);
  }

  /**
   * @brief 詰み／不詰手数専用の LookUp()
   * @return pair<最長不詰手数、最短詰み手数>
   *
   * 現局面の最長不詰手数および最短詰み手数を得る関数。LookUp() を単純に使うと、詰み手数と不詰手数を同時に得るのは
   * 難しいため、専用関数として提供する。詰み探索終了後の手順の復元に用いることを想定している。
   */
  constexpr std::pair<MateLen, MateLen> FinalRange() const noexcept {
    MateLen16 disproven_len = kMinusZeroMateLen16;
    MateLen16 proven_len = kInfiniteMateLen16;

    // 頻繁に呼ばれる関数ではないのでアンローリングせずに普通に for 文で回す
    for (std::size_t i = 0; i < Cluster::kSize; ++i) {
      auto itr = cluster_.head_entry + i;
      if (itr->IsFor(board_key_) && !itr->IsNull()) {
        itr->UpdateFinalRange(hand_, disproven_len, proven_len);
      }
    }

    return {MateLen::From(disproven_len), MateLen::From(proven_len)};
  }
  // LCOV_EXCL_STOP

  /**
   * @brief 探索結果 `result` をクラスタに書き込む
   * @param result 探索結果
   * @note 実際の処理は `SetProven()`, `SetDisproven()`, `SetRepetition()`, `SetUnknown()` を参照。
   */
  void SetResult(const SearchResult& result) const noexcept {
    if (result.Pn() == 0) {
      SetFinal<true>(result);
    } else if (result.Dn() == 0) {
      if (result.GetFinalData().is_repetition) {
        SetRepetition(result);
      } else {
        SetFinal<false>(result);
      }
    } else {
      SetUnknown(result);
    }
  }

 private:
  /**
   * @brief クラスタの中から (`board_key_`, `hand`) に一致するエントリを探す
   * @param hand 持ち駒
   * @return エントリが見つかった場合、それを返す。見つからなかった場合、`nullptr` を返す。
   */
  Entry* FindEntry(Hand hand) const noexcept {
    Entry* itr = cluster_.head_entry;

#if !defined(DOXYGEN_SHOULD_SKIP_THIS)
#define FIND_ENTRY_IMPL(i)              \
  do {                                  \
    if (itr->IsFor(board_key_, hand)) { \
      return itr;                       \
    }                                   \
    itr++;                              \
  } while (false)

    KOMORI_TTQUERY_UNROLL_CLUSTER(FIND_ENTRY_IMPL);
#undef CREATE_ENTRY_IMPL
#endif  // !defined(DOXYGEN_SHOULD_SKIP_THIS)

    return nullptr;
  }

  /**
   * @brief クラスタから持ち駒 `hand` の書き込み用のエントリを1つ選び (`pn`, `dn`) を保存する
   * @param hand 持ち駒
   * @param pn pn
   * @param dn dn
   * @param amount 探索量
   * @return 新たに作成したエントリ
   *
   * クラスタ内に空きがある場合、それを用いて新規エントリを作る。もしクラスタ内に空きがないならば、探索量が最も小さい
   * エントリを消して新規エントリを作る。
   *
   * 作成するエントリの持ち駒を `hand_` を直接使わずに `hand` を引数として受け取っている理由は、
   * 詰み／不詰エントリを書き込むときに使用したいため。
   */
  Entry* CreateNewEntry(Hand hand, PnDn pn, PnDn dn, SearchAmount amount) const noexcept {
    Entry* itr = cluster_.head_entry;
    Entry* min_amount_entry = cluster_.head_entry;
    SearchAmount min_amount = std::numeric_limits<SearchAmount>::max();
    // LCOV_EXCL_START
#if !defined(DOXYGEN_SHOULD_SKIP_THIS)
#define CREATE_ENTRY_IMPL(i)                               \
  do {                                                     \
    if (itr->IsNull()) {                                   \
      itr->Init(board_key_, hand, depth_, pn, dn, amount); \
      return itr;                                          \
    }                                                      \
    if (itr->Amount() < min_amount) {                      \
      min_amount_entry = itr;                              \
      min_amount = itr->Amount();                          \
    }                                                      \
    itr++;                                                 \
  } while (false)

    KOMORI_TTQUERY_UNROLL_CLUSTER(CREATE_ENTRY_IMPL);
#undef CREATE_ENTRY_IMPL
#endif  // !defined(DOXYGEN_SHOULD_SKIP_THIS)
    // LCOV_EXCL_STOP

    min_amount_entry->Init(board_key_, hand, depth_, pn, dn, amount);
    return min_amount_entry;
  }

  /**
   * @brief 詰みまたは不詰の探索結果 `result` をクラスタに書き込む
   * @tparam kIsProven true: 詰み／false: 不詰
   * @param result 探索結果（詰み or 不詰）
   */
  template <bool kIsProven>
  void SetFinal(const SearchResult& result) const noexcept {
    const auto hand = result.GetHand();
    auto entry = FindEntry(hand);
    if (entry == nullptr) {
      entry = CreateNewEntry(hand, 1, 1, 1);
    }

    const auto len = result.Len();
    const auto amount = result.Amount();

    if constexpr (kIsProven) {
      entry->UpdateProven(len.To16(), amount);
    } else {
      entry->UpdateDisproven(len.To16(), amount);
    }
  }

  /**
   * @brief 千日手の探索結果 `result` をクラスタに書き込む関数
   * @param result 探索結果（千日手）
   */
  void SetRepetition(const SearchResult& /* result */) const noexcept {
    auto entry = FindEntry(hand_);
    if (entry == nullptr) {
      entry = CreateNewEntry(hand_, 1, 1, 1);
    }

    entry->SetPossibleRepetition();
    rep_table_->Insert(path_key_);
  }

  /**
   * @brief 探索中の探索結果 `result` をクラスタに書き込む関数
   * @param result 探索結果（探索中）
   */
  void SetUnknown(const SearchResult& result) const noexcept {
    const auto pn = result.Pn();
    const auto dn = result.Dn();
    const auto len = result.Len();
    const auto amount = result.Amount();

    if (auto entry = FindEntry(hand_)) {
      entry->UpdateUnknown(depth_, pn, dn, len.To16(), amount);
    } else {
      CreateNewEntry(hand_, pn, dn, amount);
    }
  }

  RepetitionTable* rep_table_;  ///< 千日手テーブル。千日手判定に用いる。
  Cluster cluster_;             ///< クラスタ
  Key path_key_;                ///< 現局面の経路ハッシュ値
  Key board_key_;               ///< 現局面の盤面ハッシュ値
  Hand hand_;                   ///< 現局面の持ち駒
  Depth depth_;                 ///< 現局面の探索深さ
};

// このヘッダ外では使えないようにしておく
#undef KOMORI_TTQUERY_UNROLL_CLUSTER

}  // namespace tt
}  // namespace komori

#endif  // KOMORI_TTQUERY_HPP_
