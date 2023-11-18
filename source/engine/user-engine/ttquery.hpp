/**
 * @file ttquery.hpp
 */
#ifndef KOMORI_TTQUERY_HPP_
#define KOMORI_TTQUERY_HPP_

#include <optional>
#include <shared_mutex>

#include "board_key_hand_pair.hpp"
#include "mate_len.hpp"
#include "regular_table.hpp"
#include "repetition_table.hpp"
#include "search_result.hpp"
#include "ttentry.hpp"
#include "typedefs.hpp"

namespace komori::tt {
namespace detail {
/// LookUp() 時に加えるノイズの周期。スレッドごとに値を変えることで探索順序がばらつくようにする。
thread_local inline std::uint32_t tt_noise_interval = std::numeric_limits<std::uint32_t>::max();
/// 次にノイズを加えるまでの残り LookUp() 回数。この値が 0 になったらノイズを加える。
// ただし、numeric_limits::max() であれば一生ノイズを加えない。
thread_local inline std::uint32_t tt_noise_timing = std::numeric_limits<std::uint32_t>::max();
}  // namespace detail

/**
 * @brief LookUp() のノイズ周期を初期化する
 * @param thread_id スレッド番号
 */
inline void InitializeTTNoise(std::uint32_t thread_id) {
  // main thread にはノイズを載せない
  if (thread_id != 0) {
    // 各スレッドに少しずつノイズを乗せる。
    static constexpr std::uint32_t kNoise[6] = {7, 6, 5, 4, 3, 2};
    detail::tt_noise_interval += kNoise[(thread_id - 1) % 6];
    detail::tt_noise_timing = thread_id;
  }
}

/**
 * @brief 連続する複数エントリを束ねてまとめて読み書きするためのクラス。
 *
 * 詰将棋エンジンでは置換表を何回も何回も読み書きする。このクラスでは、置換表の読み書きに必要となる
 * 情報（ハッシュ値など）をキャッシュしておき、高速化することが目的である。
 *
 * ## 実装詳細
 *
 * 置換表から結果を読む際、以下の情報を利用して探索結果を復元する。
 *
 * 1. 同一局面（盤面も持ち駒も一致）
 * 2. 優等局面（盤面が一致していて持ち駒が現局面より多い）
 * 3. 劣等局面（盤面が一致していて持ち駒が現局面より少ない）
 */
class Query {
 public:
  /**
   * @brief Query の構築を行う。
   * @param rep_table   千日手テーブル
   * @param initial_entry_pointer 通常テーブルの探索開始位置への循環ポインタ
   * @param path_key    経路ハッシュ値
   * @param board_key   盤面ハッシュ値
   * @param hand        持ち駒
   * @param depth       探索深さ
   */
  constexpr Query(RepetitionTable& rep_table,
                  CircularEntryPointer initial_entry_pointer,
                  Key path_key,
                  Key board_key,
                  Hand hand,
                  Depth depth)
      : rep_table_{&rep_table},
        initial_entry_pointer_{initial_entry_pointer},
        path_key_{path_key},
        board_key_{board_key},
        hand_{hand},
        depth_{depth},
        cached_entry_{&*initial_entry_pointer} {};

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

  /// 盤面ハッシュ値と持ち駒のペアを返す
  constexpr BoardKeyHandPair GetBoardKeyHandPair() const noexcept { return BoardKeyHandPair{board_key_, hand_}; }

  // テンプレート関数のカバレッジは悲しいことになるので取らない
  // LCOV_EXCL_START NOLINTBEGIN

  /**
   * @brief 置換表から結果を集めてきて返す関数。
   * @tparam InitialEvalFunc 初期値関数の型
   * @param does_have_old_child  unproven old child の結果を使った場合 `true` が書かれる変数
   * @param len 探している詰み手数
   * @param eval_func 初期値を計算する関数。エントリが見つからなかったときのみ呼ばれる。
   * @return Look Up 結果
   *
   * 置換表を探索して現局面の探索情報を返す関数。
   *
   * 一般に、初期化関数の呼び出しは実行コストがかかる。そのため、初期化関数は `eval_func` に包んで渡す。
   * 必要な時のみ `eval_func` の呼び出しを行うことで、呼び出さないパスの高速化ができる。
   */
  template <typename InitialEvalFunc>
  SearchResult LookUp(bool& does_have_old_child, MateLen len, InitialEvalFunc&& eval_func) const {
    PnDn pn = 1;
    PnDn dn = 1;
    SearchAmount amount = 1;

    bool found_exact = false;
    BitSet64 sum_mask = BitSet64::Full();

    for (auto itr = initial_entry_pointer_; !itr->IsNull(); ++itr) {
      std::shared_lock lock(*itr);
      // 本来は lock 後にも !itr->Null() のチェックが必要だが、itr->Hand() == kNullHand のとき itr->LookUp() が必ず
      // 失敗するので、このタイミングでのチェックは省略できる。
      if (itr->IsFor(board_key_)) {
        if (itr->LookUp(hand_, depth_, len, pn, dn, does_have_old_child)) {
          amount = std::max(amount, itr->Amount());
          if (pn == 0) {
            return SearchResult::MakeFinal<true>(itr->GetHand(), itr->ProvenLen(), amount);
          } else if (dn == 0) {
            return SearchResult::MakeFinal<false>(itr->GetHand(), itr->DisprovenLen(), amount);
          } else if (itr->GetHand() == hand_) {
            if (itr->IsPossibleRepetition()) {
              if (const auto opt = rep_table_->Contains(path_key_, len)) {
                const auto [depth, table_len] = opt.value();
                return SearchResult::MakeRepetition(hand_, table_len, amount, depth);
              }
            }

            found_exact = true;
            sum_mask = itr->SumMask();
            cached_entry_ = &*itr;
          }
        }
      }
    }

    // LookUp() 結果が final でないとき、スレッドごとに決められた周期で pn, dn へノイズを載せる
    if (detail::tt_noise_timing != std::numeric_limits<std::uint32_t>::max()) {
      if (detail::tt_noise_timing-- == 0) {
        detail::tt_noise_timing = detail::tt_noise_interval;
        // (pn, dn) へノイズを載せる
        pn++;
        dn++;
      }
    }

    if (found_exact) {
      return SearchResult::MakeUnknown(pn, dn, len, amount, sum_mask);
    }

    const auto [init_pn, init_dn] = std::forward<InitialEvalFunc>(eval_func)();
    pn = std::max(pn, init_pn);
    dn = std::max(dn, init_dn);

    return SearchResult::MakeFirstVisit(pn, dn, len, amount);
  }
  // LCOV_EXCL_STOP NOLINTEND

  /**
   * @brief 置換表に保存された現局面の親局面を取得する
   * @param[out] pn 現局面のpn
   * @param[out] dn 現局面のdn
   * @return 現局面の親局面
   */
  std::optional<BoardKeyHandPair> LookUpParent(PnDn& pn, PnDn& dn) const noexcept {
    pn = dn = 1;

    Key parent_board_key = kNullKey;
    Hand parent_hand = kNullHand;
    for (auto itr = initial_entry_pointer_; !itr->IsNull(); ++itr) {
      const std::shared_lock lock(*itr);
      if (itr->IsFor(board_key_)) {
        itr->UpdateParentCandidate(hand_, pn, dn, parent_board_key, parent_hand);
      }
    }

    if (parent_hand == kNullHand) {
      return std::nullopt;
    }

    return BoardKeyHandPair{parent_board_key, parent_hand};
  }

  /**
   * @brief 詰み／不詰手数専用の LookUp()
   * @return pair<最長不詰手数、最短詰み手数>
   *
   * 現局面の最長不詰手数および最短詰み手数を得る関数。LookUp() を単純に使うと、詰み手数と不詰手数を同時に得るのは
   * 難しいため、専用関数として提供する。詰み探索終了後の手順の復元に用いることを想定している。
   */
  std::pair<MateLen, MateLen> FinalRange() const noexcept {
    MateLen disproven_len = kMinus1MateLen;
    MateLen proven_len = kDepthMaxPlus1MateLen;

    for (auto itr = initial_entry_pointer_; !itr->IsNull(); ++itr) {
      const std::shared_lock lock(*itr);

      if (itr->IsFor(board_key_)) {
        itr->UpdateFinalRange(hand_, disproven_len, proven_len);

        if (itr->IsFor(board_key_, hand_) && itr->IsPossibleRepetition()) {
          if (const auto opt = rep_table_->Contains(path_key_, disproven_len)) {
            disproven_len = std::max(disproven_len, opt->second);
          }
        }
      }
    }

    return {MateLen{disproven_len}, MateLen{proven_len}};
  }

  /**
   * @brief 探索結果 `result` を置換表に書き込む
   * @param result 探索結果
   * @param parent_key_hand_pair 親局面の盤面ハッシュ値と持ち駒のペア
   * @note 実際の処理は `SetProven()`, `SetDisproven()`, `SetRepetition()`, `SetUnknown()` を参照。
   */
  void SetResult(const SearchResult& result,
                 BoardKeyHandPair parent_key_hand_pair = BoardKeyHandPair{kNullKey, kNullHand}) const noexcept {
    if (result.Pn() == 0) {
      SetFinal<true>(result);
    } else if (result.Dn() == 0) {
      if (result.GetFinalData().IsRepetition()) {
        SetRepetition(result);
      } else {
        SetFinal<false>(result);
      }
    } else {
      SetUnknown(result, parent_key_hand_pair);
    }
  }

 private:
  /**
   * @brief 置換表に `hand` に一致するエントリがあればそれを返し、なければ作って返す
   * @param hand 持ち駒
   * @return 見つけた or 作成したエントリ。`lock()` された状態で返るので、参照が完了したら必ず `unlock()` を呼ぶこと。
   */
  Entry* FindOrCreate(Hand hand) const noexcept {
    if (!cached_entry_->IsNull()) {
      cached_entry_->lock();
      if (cached_entry_->IsFor(board_key_, hand)) {
        return cached_entry_;
      }
      cached_entry_->unlock();
    }

    for (auto itr = initial_entry_pointer_;; ++itr) {
      itr->lock();
      if (itr->IsNull()) {
        itr->Init(board_key_, hand);
        return cached_entry_ = &*itr;
      }

      if (itr->IsFor(board_key_, hand)) {
        return cached_entry_ = &*itr;
      }
      itr->unlock();
    }
  }

  /**
   * @brief 詰みまたは不詰の探索結果 `result` を置換表に書き込む
   * @tparam kIsProven true: 詰み／false: 不詰
   * @param result 探索結果（詰み or 不詰）
   */
  template <bool kIsProven>
  void SetFinal(const SearchResult& result) const noexcept {
    const auto hand = result.GetFinalData().hand;
    auto* const entry = FindOrCreate(hand);
    const auto len = result.Len();
    const auto amount = result.Amount();

    if constexpr (kIsProven) {
      entry->UpdateProven(len, amount);
    } else {
      entry->UpdateDisproven(len, amount);
    }
    entry->unlock();
  }

  /**
   * @brief
   * @param result 探索結果（千日手）
   */
  void SetRepetition(const SearchResult& result) const noexcept {
    auto* const entry = FindOrCreate(hand_);

    entry->SetPossibleRepetition();
    entry->unlock();
    rep_table_->Insert(path_key_, result.GetFinalData().repetition_start, result.Len());
  }

  /**
   * @brief 探索中の探索結果 `result` を置換表に書き込む関数
   * @param result 探索結果（探索中）
   */
  void SetUnknown(const SearchResult& result, BoardKeyHandPair parent_key_hand_pair) const noexcept {
    const auto pn = result.Pn();
    const auto dn = result.Dn();
    const auto amount = result.Amount();
    const auto sum_mask = result.GetUnknownData().sum_mask;
    const auto [parent_board_key, parent_hand] = parent_key_hand_pair;

    auto* const entry = FindOrCreate(hand_);
    entry->UpdateUnknown(depth_, pn, dn, amount, sum_mask, parent_board_key, parent_hand);
    entry->unlock();
  }

  RepetitionTable* rep_table_;                  ///< 千日手テーブル。千日手判定に用いる。
  CircularEntryPointer initial_entry_pointer_;  ///< 通常テーブルの探索開始位置への循環ポインタ
  Key path_key_;                                ///< 現局面の経路ハッシュ値
  Key board_key_;                               ///< 現局面の盤面ハッシュ値
  Hand hand_;                                   ///< 現局面の持ち駒
  Depth depth_;                                 ///< 現局面の探索深さ
  mutable Entry* cached_entry_;                 ///< 前回アクセスしたエントリ
};
}  // namespace komori::tt

#endif  // KOMORI_TTQUERY_HPP_
