#ifndef TRANSPOSITION_TABLE_HPP_
#define TRANSPOSITION_TABLE_HPP_

#include <vector>

#include "ttcluster.hpp"
#include "typedefs.hpp"

namespace komori {

// forward declaration
class CommonEntry;
// forward declaration
class TTCluster;
// forward declaration
class Node;

/**
 * @brief Cluster を LookUp するためのキャッシュするクラス
 *
 * 本クラスでは、 TTCluster を LookUp するための情報を保存保存しておき、同一局面に対する LookUp 呼び出しを簡略化したり、
 * 前回の LookUp 結果を再利用することで高速化することを目的とするクラスである。
 * 詰将棋の探索において、最も時間がかかる処理は置換表の LookUp である。本将棋の AI とは異なり、詰将棋の LookUp 時は
 * 証明駒・反証駒の探索を行わなければならないので、hash による一発表引きだけでは済まないことが処理負荷増加の一因と
 * なっている。
 */
class LookUpQuery {
 public:
  /// キャッシュに使用するため、デフォルトコンストラクタを有効にする
  LookUpQuery() = default;
  LookUpQuery(const LookUpQuery&) = delete;
  LookUpQuery(LookUpQuery&&) noexcept = default;
  LookUpQuery& operator=(const LookUpQuery&) = delete;
  LookUpQuery& operator=(LookUpQuery&&) noexcept = default;
  LookUpQuery(TTCluster* cluster, std::uint32_t hash_high, Hand hand, Depth depth, Key path_key);

  /// Query によるエントリ問い合わせを行う。もし見つからなかった場合は新規作成して cluster に追加する
  CommonEntry* LookUpWithCreation() const;
  /**
   * @brief  Query によるエントリ問い合わせを行う。もし見つからなかった場合はダミーのエントリを返す。
   *
   * ダミーエントリが返されたかどうかは IsStored() により判定可能である。このエントリは次回の LookUp までの間まで
   * 有効である。
   */
  CommonEntry* LookUpWithoutCreation() const;

  /**
   * @brief entry が有効（前回呼び出しから移動していない）場合、それをそのまま帰す。
   *
   * entry が無効の場合、改めて LookUpWithCreation() する。
   */
  CommonEntry* RefreshWithCreation(CommonEntry* entry) const;
  /**
   * @brief entry が有効（前回呼び出しから移動していない）場合、それをそのまま帰す。
   *
   * entry が無効の場合、改めて LookUpWithoutCreation() する。
   */
  CommonEntry* RefreshWithoutCreation(CommonEntry* entry) const;

  /// 調べていた局面が証明駒 `proof_hand` で詰みであることを報告する
  CommonEntry* SetProven(Hand proof_hand, Move16 move, Depth len, SearchedAmount amount) const;
  /// 調べていた局面が反証駒 `disproof_hand` で詰みであることを報告する
  CommonEntry* SetDisproven(Hand disproof_hand, Move16 move, Depth len, SearchedAmount amount) const;
  /// 調べていた局面が千日手による不詰であることを報告する
  CommonEntry* SetRepetition(CommonEntry* entry, SearchedAmount amount) const;
  /// 調べていた局面が勝ちであることを報告する
  template <bool kOrNode>
  CommonEntry* SetWin(Hand hand, Move move, Depth len, SearchedAmount amount) const {
    if constexpr (kOrNode) {
      return SetProven(hand, move, len, amount);
    } else {
      return SetDisproven(hand, move, len, amount);
    }
  }
  /// 調べていた局面が負けであることを報告する
  template <bool kOrNode>
  CommonEntry* SetLose(Hand hand, Move move, Depth len, SearchedAmount amount) const {
    if constexpr (kOrNode) {
      return SetDisproven(hand, move, len, amount);
    } else {
      return SetProven(hand, move, len, amount);
    }
  }
  /// `entry` が cluster に存在するエントリかを問い合わせる。（ダミーエントリのチェックに使用する）
  bool IsStored(CommonEntry* entry) const;
  /// `entry` が有効（前回呼び出しから移動していない）かどうかをチェックする
  bool IsValid(CommonEntry* entry) const;

  /// query 時点の手駒を返す
  Hand GetHand() const { return hand_; }
  std::uint32_t HashHigh() const { return hash_high_; }
  Key PathKey() const { return path_key_; }

 private:
  TTCluster* cluster_;
  std::uint32_t hash_high_;
  Hand hand_;
  Depth depth_;
  Key path_key_;
};

/**
 * @brief 詰将棋探索における置換表本体
 *
 * 高速化のために、直接 LookUp させるのではなく、LookUpQuery を返すことで結果のキャッシュが可能にしている。
 */
class TranspositionTable {
 public:
  struct Stat {
    double hashfull;
    double proven_ratio;
    double disproven_ratio;
    double repetition_ratio;
    double maybe_repetition_ratio;
    double other_ratio;
  };

  explicit TranspositionTable(int gc_hashfull);

  /// ハッシュサイズを `hash_size_mb` （以下）に変更する。以前に保存されていた結果は削除される
  void Resize(std::uint64_t hash_size_mb);
  /// 以前の探索結果をすべて削除し、新たな探索をを始める
  void NewSearch();
  /// GCを実行する
  std::size_t CollectGarbage();

  /// 局面 `n` の LookUp 用の構造体を取得する
  LookUpQuery GetQuery(const Node& n);
  /// 局面 `n` から `move` で進めた局面の、LookUp 用の構造体を取得する
  LookUpQuery GetChildQuery(const Node& n, Move move);

  /// 局面 `n` の最善手を取得する。探索中の場合、MOVE_NONE が返る可能性がある
  Move LookUpBestMove(const Node& n);

  /// ハッシュ使用率を返す（戻り値は千分率）
  int Hashfull() const;
  /// 現在のハッシュの使用状況を取得する
  Stat GetStat() const;

  void Debug() const;

 private:
  /// `board_key` に対応する cluster を返す
  TTCluster& ClusterOf(Key board_key);

  /// 置換表本体。キャッシュラインに乗せるために、実態より少し多めにメモリ確保を行う
  std::vector<uint8_t> tt_raw_{};
  /// 置換表配列にアクセスするためのポインタ
  TTCluster* tt_{nullptr};
  /// 置換表に保存されているクラスタ数
  std::uint64_t num_clusters_{2};
  /// GC で削除したい要素数の割合（千分率）
  int gc_hashfull_;
  /// 前回の GC で用いたしきい値
  SearchedAmount threshold_{kMinimumSearchedAmount};
};
}  // namespace komori
#endif  // TRANSPOSITION_TABLE_HPP_