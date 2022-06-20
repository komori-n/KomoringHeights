#if !defined(USER_ENGINE)
#define USER_ENGINE
#endif

#include "komoring_heights.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "../../mate/mate.h"
#include "children_cache.hpp"
#include "hands.hpp"
#include "move_picker.hpp"
#include "node_history.hpp"
#include "path_keys.hpp"

namespace komori {
namespace {
constexpr std::size_t kSplittedPrintLen = 12;
constexpr int kPostSearchVisitThreshold = 200;
/// PostSearch で詰み局面のはずなのに TT から詰み手順を復元できないとき、追加探索する局面数。
/// この値が大きすぎると一生探索が終わらなくなるので注意。
constexpr std::uint64_t kGcReconstructSearchCount = 100'000;

inline std::uint64_t GcInterval(std::uint64_t hash_mb) {
  const std::uint64_t entry_num = hash_mb * 1024 * 1024 / sizeof(CommonEntry);

  return entry_num / 2 * 3;
}

/// 詰み手数と持ち駒から MateLen を作る
inline MateLen MakeMateLen(Depth depth, Hand hand) {
  return {static_cast<std::uint16_t>(depth), static_cast<std::uint16_t>(std::min(15, CountHand(hand)))};
}

/// PostSearch において、現局面の MateLen および alpha, beta を管理するクラス
class SearchingLen {
 public:
  SearchingLen(bool or_node, Hand or_hand, MateLen alpha, MateLen beta)
      : or_node_{or_node}, alpha_{alpha}, beta_{beta}, mate_len_{or_node ? kMaxMateLen : MakeMateLen(0, or_hand)} {}

  bool Update(Move move, MateLen new_mate_len) {
    if (or_node_ && mate_len_ > new_mate_len) {
      best_move_ = move;
      mate_len_ = new_mate_len;
      beta_ = std::min(beta_, new_mate_len);

      return true;
    } else if (!or_node_ && mate_len_ < new_mate_len) {
      best_move_ = move;
      mate_len_ = new_mate_len;
      alpha_ = std::max(alpha_, new_mate_len);

      return true;
    }

    return false;
  }

  bool IsEnd() const { return alpha_ >= beta_; }
  bool IsLowerBound() const { return mate_len_ >= alpha_; }
  bool IsUpperBound() const { return mate_len_ <= beta_; }
  bool IsExactBound() const { return IsLowerBound() && IsUpperBound(); }

  MateLen GetMateLen() const { return mate_len_; }
  Move GetBestMove() const { return best_move_; }

  MateLen Alpha() const { return alpha_; }
  MateLen Beta() const { return beta_; }

 private:
  const bool or_node_;

  Move best_move_{MOVE_NONE};

  MateLen alpha_;
  MateLen beta_;
  MateLen mate_len_;
};

/// 局面 n の最短の考えられる最短の詰み手数を返す
template <bool kAndNodeHasAtLeastOneLegalMove = false>
MateLen MateLenInf(const Node& n) {
  const auto or_node = n.IsOrNode();
  const auto or_hand = n.OrHand();

  if (or_node) {
    const auto or_hand_sup = CountHand(or_hand) + 1;
    return {1, static_cast<std::uint16_t>(std::min(or_hand_sup, 15))};
  } else {
    if constexpr (kAndNodeHasAtLeastOneLegalMove) {
      const auto or_hand_sup = CountHand(or_hand) + 1;
      return {2, static_cast<std::uint16_t>(std::min(or_hand_sup, 15))};
    } else {
      return {0, static_cast<std::uint16_t>(std::min(CountHand(or_hand), 15))};
    }
  }
}

std::vector<std::pair<Move, SearchResult>> ExpandChildren(TranspositionTable& tt, const Node& n) {
  std::vector<std::pair<Move, SearchResult>> ret;
  for (auto&& move : MovePicker{n}) {
    auto query = tt.GetChildQuery(n, move.move);
    auto entry = query.LookUpWithoutCreation();
    SearchResult result = entry->Simplify(n.OrHandAfter(move.move));
    ret.emplace_back(move.move, result);
  }

  return ret;
}

/// n の子局面のうち、詰み手順としてふさわしそうな局面を選んで返す。
/// sizeof(MovePicker) がそこそこ大きいので、もし再帰関数で inline 展開されるとスタックサイズが足りなくなるので
/// inline 展開させないようにする。
__attribute__((noinline)) Move SelectBestMove(TranspositionTable& tt, const Node& n) {
  bool or_node = n.IsOrNode();
  Move nondrop_best_move = MOVE_NONE;
  Move drop_best_move = MOVE_NONE;
  MateLen drop_mate_len = or_node ? kMaxMateLen : kZeroMateLen;
  MateLen nondrop_mate_len = drop_mate_len;
  for (const auto& m2 : MovePicker{n}) {
    auto query = tt.GetChildQuery(n, m2.move);
    auto entry = query.LookUpWithoutCreation();
    if (entry->GetNodeState() != NodeState::kProvenState) {
      continue;
    }

    MateLen child_mate_len = entry->GetMateLen(n.OrHand());
    if (is_drop(m2.move)) {
      if ((or_node && child_mate_len + 1 < drop_mate_len) || (!or_node && child_mate_len + 1 > drop_mate_len)) {
        drop_mate_len = child_mate_len;
        drop_best_move = m2.move;
      }
    } else {
      if ((or_node && child_mate_len + 1 < nondrop_mate_len) || (!or_node && child_mate_len + 1 > nondrop_mate_len)) {
        nondrop_mate_len = child_mate_len;
        nondrop_best_move = m2.move;
      }
    }
  }

  if (nondrop_best_move != MOVE_NONE) {
    return nondrop_best_move;
  }

  return drop_best_move;
}

void SplittedPrint(UsiInfo info, const std::string& header, const std::vector<std::pair<Depth, Move>>& moves) {
  std::size_t size = 0;
  std::ostringstream oss;

  for (const auto& [depth, move] : moves) {
    if (size == 0) {
      oss << header;
    }

    oss << (size == 0 ? " " : ", ") << depth << ": " << move;
    size++;

    if (size == kSplittedPrintLen) {
      info.Set(UsiInfo::KeyKind::kString, oss.str());
      sync_cout << info << sync_endl;
      oss.str("");
      oss.clear(std::stringstream::goodbit);
      size = 0;
    }
  }

  if (size > 0) {
    info.Set(UsiInfo::KeyKind::kString, oss.str());
    sync_cout << info << sync_endl;
  }
}

Score MakeScore(const SearchResult& result, bool root_is_or_node) {
  if (result.IsNotFinal()) {
    return Score::Unknown(result.Pn(), result.Dn());
  } else if (result.GetNodeState() == NodeState::kProvenState) {
    auto mate_len = result.FrontMateLen();
    return Score::Proven(mate_len.len, root_is_or_node);
  } else {
    auto mate_len = result.FrontMateLen();
    return Score::Disproven(mate_len.len, root_is_or_node);
  }
}
}  // namespace

namespace detail {
void SearchMonitor::Init(Thread* thread) {
  thread_ = thread;
}

void SearchMonitor::NewSearch(std::uint64_t gc_interval) {
  start_time_ = std::chrono::system_clock::now();
  depth_ = 0;

  tp_hist_.Clear();
  mc_hist_.Clear();
  hist_idx_ = 0;

  move_limit_ = std::numeric_limits<std::uint64_t>::max();
  limit_stack_ = {};

  gc_interval_ = gc_interval;
  ResetNextGc();
}

void SearchMonitor::Tick() {
  tp_hist_[hist_idx_] = std::chrono::system_clock::now();
  mc_hist_[hist_idx_] = MoveCount();
  hist_idx_++;
}

UsiInfo SearchMonitor::GetInfo() const {
  auto curr_time = std::chrono::system_clock::now();
  auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - start_time_).count();

  const auto move_count = MoveCount();
  std::uint64_t nps = 0;
  if (hist_idx_ >= kHistLen) {
    const auto tp = tp_hist_[hist_idx_];
    const auto mc = mc_hist_[hist_idx_];
    const auto tp_diff = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - tp).count();
    nps = (move_count - mc) * 1000ULL / tp_diff;
  } else {
    if (time_ms > 0) {
      nps = move_count * 1000ULL / time_ms;
    }
  }

  UsiInfo output;
  output.Set(UsiInfo::KeyKind::kSelDepth, depth_)
      .Set(UsiInfo::KeyKind::kTime, time_ms)
      .Set(UsiInfo::KeyKind::kNodes, move_count)
      .Set(UsiInfo::KeyKind::kNps, nps);

  return output;
}

void SearchMonitor::ResetNextGc() {
  next_gc_count_ = MoveCount() + gc_interval_;
}

void SearchMonitor::PushLimit(std::uint64_t move_limit) {
  limit_stack_.push(move_limit_);
  move_limit_ = std::min(move_limit_, move_limit);
}

void SearchMonitor::PopLimit() {
  if (!limit_stack_.empty()) {
    move_limit_ = limit_stack_.top();
    limit_stack_.pop();
  }
}
}  // namespace detail

void KomoringHeights::Init(EngineOption option, Thread* thread) {
  option_ = option;
  tt_.Resize(option_.hash_mb);
  monitor_.Init(thread);
}

UsiInfo KomoringHeights::CurrentInfo() const {
  UsiInfo usi_output = monitor_.GetInfo();
  usi_output.Set(UsiInfo::KeyKind::kHashfull, tt_.Hashfull()).Set(UsiInfo::KeyKind::kScore, score_);

  return usi_output;
}

NodeState KomoringHeights::Search(Position& n, bool is_root_or_node) {
  // <初期化>
  tt_.NewSearch();
  monitor_.NewSearch(GcInterval(option_.hash_mb));
  monitor_.PushLimit(option_.nodes_limit);
  pv_tree_.Clear();
  best_moves_.clear();
  // </初期化>

  // Position より Node のほうが便利なので、探索中は node を用いる
  Node node{n, is_root_or_node};

  // thpn/thdn で反復深化探索を行う
  PnDn thpn = 1;
  PnDn thdn = 1;

  SearchResult result;
  // 既に stop すべき状態でも 1 回は探索を行う（resultに値を入れるため）
  do {
    result = SearchEntry(node, thpn, thdn);
    score_ = MakeScore(result, is_root_or_node);
    if (result.IsFinal() || result.Pn() >= kInfinitePnDn || result.Dn() >= kInfinitePnDn) {
      // 探索が評価値が確定したら break　する
      // is_final だけではなく pn/dn の値を見ているのはオーバーフロー対策のため。
      // pn/dn が kInfinitePnDn を上回たら諦める
      break;
    }

    // 反復深化のしきい値を適当に伸ばす
    thpn = Clamp(thpn, 2 * result.Pn(), kInfinitePnDn);
    thdn = Clamp(thdn, 2 * result.Dn(), kInfinitePnDn);
  } while (!monitor_.ShouldStop());

  // PostSearch だけテストするときに挙動が変わると困るので、本探索が終わったこのタイミングで一旦 GC のカウンタをクリア
  monitor_.ResetNextGc();
  auto info = CurrentInfo();
  info.Set(UsiInfo::KeyKind::kString, ToString(result));
  sync_cout << info << sync_endl;

  if (result.GetNodeState() == NodeState::kProvenState) {
    best_moves_.clear();
    MateLen mate_len;
    for (int i = 0; i < 50; ++i) {
      std::unordered_map<Key, int> visit_count;
      // MateLen::len は unsigned なので、調子に乗って alpha の len をマイナスにするとバグる（一敗）
      mate_len = PostSearch(visit_count, node, kZeroMateLen, kMaxMateLen);
      auto tree_moves = pv_tree_.Pv(node);
      if (tree_moves.size() % 2 == (is_root_or_node ? 1 : 0)) {
        best_moves_ = std::move(tree_moves);
        break;
      }
    }

    sync_cout << "info string mate_len=" << mate_len << sync_endl;

    score_ = Score::Proven(static_cast<Depth>(best_moves_.size()), is_root_or_node);
    PrintYozume(node, best_moves_);

    if (best_moves_.size() % 2 != (is_root_or_node ? 1 : 0)) {
      sync_cout << "info string Failed to detect PV" << sync_endl;
      best_moves_ = TraceBestMove(node);
    }
  } else {
    if (result.GetNodeState() == NodeState::kDisprovenState || result.GetNodeState() == NodeState::kRepetitionState) {
      score_ = Score::Disproven(result.FrontMateLen().len, is_root_or_node);
    }
  }

  monitor_.PopLimit();
  return result.GetNodeState();
}

void KomoringHeights::ShowValues(Position& n, bool is_root_or_node, const std::vector<Move>& moves) {
  auto depth_max = static_cast<Depth>(moves.size());
  Key path_key = 0;
  Node node{n, is_root_or_node};
  for (Depth depth = 0; depth < depth_max; ++depth) {
    path_key = PathKeyAfter(path_key, moves[depth], depth);
    node.DoMove(moves[depth]);
  }

  for (auto&& move : MovePicker{node}) {
    auto query = tt_.GetChildQuery(node, move.move);
    auto entry = query.LookUpWithoutCreation();
    sync_cout << move.move << " " << *entry << sync_endl;
  }

  static_assert(std::is_signed_v<Depth>);
  for (Depth depth = depth_max - 1; depth >= 0; --depth) {
    node.UndoMove(moves[depth]);
  }
}

void KomoringHeights::ShowPv(Position& n, bool is_root_or_node) {
  Node node{n, is_root_or_node};
  std::vector<Move> moves;

  for (;;) {
    auto children = ExpandChildren(tt_, node);
    std::sort(children.begin(), children.end(), [&](const auto& lhs, const auto& rhs) {
      bool or_node = node.IsOrNode();
      if (lhs.second.Phi(or_node) != rhs.second.Phi(or_node)) {
        return lhs.second.Phi(or_node) < rhs.second.Phi(or_node);
      }

      if (lhs.second.Phi(or_node) == 0 && rhs.second.Phi(or_node) == 0) {
        return lhs.second.FrontMateLen() < rhs.second.FrontMateLen();
      }

      if (lhs.second.Delta(or_node) != rhs.second.Delta(or_node)) {
        return lhs.second.Delta(or_node) > rhs.second.Delta(or_node);
      }

      if (lhs.second.Delta(or_node) == 0 && rhs.second.Delta(or_node) == 0) {
        return lhs.second.FrontMateLen() > rhs.second.FrontMateLen();
      }

      return false;
    });

    std::ostringstream oss;
    oss << "[" << node.GetDepth() << "] ";
    for (const auto& child : children) {
      if (child.second.Pn() == 0) {
        oss << child.first << "(+" << child.second.FrontMateLen() << ") ";
      } else if (child.second.Dn() == 0) {
        oss << child.first << "(-" << child.second.FrontMateLen() << ") ";
      } else {
        oss << child.first << "(" << ToString(child.second.Pn()) << "/" << ToString(child.second.Dn()) << ") ";
      }
    }
    sync_cout << oss.str() << sync_endl;

    if (children.empty() || (children[0].second.Pn() == 1 && children[0].second.Dn() == 1)) {
      break;
    }
    auto best_move = children[0].first;
    node.DoMove(best_move);
    moves.emplace_back(best_move);
    if (node.IsRepetition()) {
      break;
    }
  }

  // 高速 1 手詰めルーチンで解ける局面は置換表に登録されていない可能性がある
  if (node.IsOrNode()) {
    if (Move move = Mate::mate_1ply(node.Pos()); move != MOVE_NONE) {
      node.DoMove(move);
      moves.emplace_back(move);
    }
  }

  sync_cout << sync_endl;
  std::ostringstream oss;
  for (const auto& move : moves) {
    oss << move << " ";
  }
  sync_cout << "pv: " << oss.str() << sync_endl;

  sync_cout << node.Pos() << sync_endl;
  for (auto itr = moves.crbegin(); itr != moves.crend(); ++itr) {
    node.UndoMove(*itr);
  }
}

MateLen KomoringHeights::PostSearch(std::unordered_map<Key, int>& visit_count, Node& n, MateLen alpha, MateLen beta) {
  const bool or_node = n.IsOrNode();
  const auto or_hand = n.OrHand();
  const auto key = n.Pos().key();
  // そもそも beta を下回らなさそうなら early return する
  if (const auto inf = MateLenInf(n); beta < inf) {
    return inf;
  } else if (alpha > beta) {
    return or_node ? alpha : beta;
  }

  SearchingLen searching_len{or_node, or_hand, alpha, beta};
  bool repetition = false;

  PrintIfNeeded(n);

  const auto& probed_range = pv_tree_.Probe(n);
  if (probed_range.min_mate_len == probed_range.max_mate_len) {
    // min == max のとき
    // 詰み手数が確定しているので探索を省ける
    return probed_range.max_mate_len;
  } else if (probed_range.max_mate_len < searching_len.Alpha()) {
    // (min <) max < alpha (<= beta) のとき
    searching_len.Update(probed_range.best_move, probed_range.max_mate_len);
    return probed_range.max_mate_len;
  } else if (searching_len.Beta() < probed_range.min_mate_len) {
    // (alpha <=) beta < min (< max) のとき
    searching_len.Update(probed_range.best_move, probed_range.min_mate_len);
    return probed_range.min_mate_len;
  }

  // <組み合わせ爆発回避（暫定）>
  visit_count[key]++;
  if (visit_count[key] > kPostSearchVisitThreshold) {
    // 何回もこの局面を通っているのに評価値が確定していないのは様子がおかしい。込み入った千日手が絡んだ局面である
    // 可能性が高い。これ以上この局面を調べても意味があまりないため、以前に見つけた上界値・下界値を結果として
    // 返してしまう。

    auto nn = n.HistoryClearedNode();
    std::unordered_map<Key, int> new_visit_count;
    PostSearch(new_visit_count, nn, kZeroMateLen, kMaxMateLen);
  }
  // </組み合わせ爆発回避（暫定）>

  // pv_tree の情報だけでは詰み手数が確定できなかったので、指し手生成が必要
  auto& mp = pickers_.emplace(n, true);
  std::sort(mp.begin(), mp.end(), [](const ExtMove& m1, const ExtMove& m2) { return m1.value < m2.value; });

  // 優先して探索したい手の挿入箇所。[mp.begin(), mp_insert_itr) に先に探索したい手を配置する
  auto mp_insert_itr = mp.begin();
  auto mp_insert_move = [&mp, &mp_insert_itr](const Move move) {
    if (move == MOVE_NONE) {
      return;
    }

    auto itr = std::find_if(mp.begin(), mp.end(), [=](const ExtMove& m) { return m.move == move; });
    if (itr != mp.end()) {
      if (itr >= mp_insert_itr) {
        // itr を mp_insert_itr の位置に持ってきて、それ以外を一つずつ後ろにずらす
        std::rotate(mp_insert_itr, itr, itr + 1);
        mp_insert_itr++;
      }
    }
  };

  mp_insert_move(probed_range.best_move);
  auto tt_move = tt_.LookUpBestMove(n);
  if (or_node && tt_move == MOVE_NONE) {
    auto nn = n.HistoryClearedNode();
    SearchEntry(nn);
    tt_move = tt_.LookUpBestMove(n);
  }
  mp_insert_move(tt_move);

  for (const auto& move : mp) {
    if (monitor_.ShouldStop() || searching_len.IsEnd()) {
      break;
    }

    // 子局面を以前探索したことがあるなら、探索せずに済ませられないか調べてみる
    const auto probed_range_after = pv_tree_.ProbeAfter(n, move.move);
    if (probed_range_after.min_mate_len == probed_range_after.max_mate_len) {
      // 子局面の厳密評価値を知っている
      searching_len.Update(move.move, probed_range_after.max_mate_len + 1);
      continue;
    } else if ((or_node && probed_range_after.min_mate_len + 1 >= searching_len.GetMateLen()) ||
               (!or_node && probed_range_after.max_mate_len + 1 <= searching_len.GetMateLen())) {
      continue;
    }

    if (n.IsRepetitionAfter(move.move)) {
      repetition = true;
      if (or_node) {
        continue;
      } else {
        break;
      }
    }

    if (!or_node && n.IsRepetitionOrSuperiorAfter(move.move)) {
      // AND node で劣等局面に突入する手は無駄合と同じで禁じ手扱いにする
      continue;
    }

    if (or_node && move.move != tt_move) {
      auto result = PostSearchEntry(n, move);
      if (result.GetNodeState() != NodeState::kProvenState) {
        continue;
      }
    }

    // move を選べば詰み
    // これ以降で continue や break したい場合は UndoMove を忘れずにしなければならない
    n.DoMove(move.move);
    if (!or_node) {
      // 無駄合探索
      if (auto capture_move = n.ImmediateCapture(); capture_move != MOVE_NONE) {
        // 現在王手している駒ですぐ取り返して（capture_move）、取った駒を玉方に返しても詰むなら無駄合
        auto useless_result = UselessDropSearchEntry(n, capture_move);
        if (useless_result.GetNodeState() == NodeState::kProvenState) {
          // 無駄合。move を選んではダメ。
          // move は非合法手と同じ扱いでなかったことにする
          goto UNDO_MOVE_AND_CONTINUE;
        }
      }

      // move は無駄合いではなかったので、詰みまで少なくとも 2 手かかる事がわかった
      searching_len.Update(move.move, MateLenInf<true>(n));
      if (searching_len.IsEnd()) {
        goto UNDO_MOVE_AND_CONTINUE;
      }
    }

    {
      const auto child_mate_len = PostSearch(visit_count, n, searching_len.Alpha() - 1, searching_len.Beta() - 1);
      if (child_mate_len >= kMaxMateLen) {
        repetition = true;
      }
      searching_len.Update(move.move, child_mate_len + 1);

      if (tt_move == MOVE_NONE) {
        tt_move = move.move;
      }
    }

  UNDO_MOVE_AND_CONTINUE:
    n.UndoMove(move.move);
  }

  pickers_.pop();

  const auto mate_len = searching_len.GetMateLen();
  const auto best_move = searching_len.GetBestMove();
  Bound bound = BOUND_NONE;
  if (!repetition && searching_len.IsExactBound()) {
    bound = BOUND_EXACT;
  } else if (!repetition && searching_len.IsLowerBound()) {
    bound = BOUND_LOWER;
  } else if ((!repetition || or_node) && searching_len.IsUpperBound()) {
    // (repetition && or_node) でも UPPER BOUND にできる理由
    // 千日手を加味した詰み手数は加味しない詰み手数よりも長いので upper bound として使えるため。
    bound = BOUND_UPPER;
  }

  if (bound != BOUND_NONE) {
    pv_tree_.Insert(n, bound, mate_len, best_move);
  }

  if (repetition && !or_node) {
    return kMaxMateLen;
  }

  return mate_len;
}

SearchResult KomoringHeights::SearchEntry(Node& n, PnDn thpn, PnDn thdn) {
  ChildrenCache cache{tt_, n, true};
  auto result = SearchImpl(n, thpn, thdn, cache, false);

  auto query = tt_.GetQuery(n);
  query.SetResult(result);

  return result;
}

SearchResult KomoringHeights::PostSearchEntry(Node& n, Move move) {
  auto query = tt_.GetChildQuery(n, move);
  auto entry = query.LookUpWithoutCreation();
  if (entry->IsFinal()) {
    return entry->Simplify(n.OrHandAfter(move));
  } else {
    n.DoMove(move);
    auto move_limit = monitor_.MoveCount() + option_.post_search_count;
    monitor_.PushLimit(move_limit);
    auto result = SearchEntry(n);
    monitor_.PopLimit();
    n.UndoMove(move);

    return result;
  }
}

SearchResult KomoringHeights::UselessDropSearchEntry(Node& n, Move move) {
  // YozumeSearchEntry とは異なり駒のやり取りがあるので、DoMove を省略することはできない
  n.DoMove(move);
  n.StealCapturedPiece();

  auto query = tt_.GetQuery(n);
  auto entry = query.LookUpWithoutCreation();
  SearchResult result;
  if (entry->IsFinal()) {
    result = entry->Simplify(n.OrHandAfter(move));
  } else {
    auto move_limit = monitor_.MoveCount() + option_.post_search_count;
    monitor_.PushLimit(move_limit);
    result = SearchEntry(n);
    monitor_.PopLimit();
  }

  n.UnstealCapturedPiece();
  n.UndoMove(move);

  return result;
}

SearchResult KomoringHeights::SearchImpl(Node& n, PnDn thpn, PnDn thdn, ChildrenCache& cache, bool inc_flag) {
  monitor_.Visit(n.GetDepth());
  PrintIfNeeded(n);

  // 深さ制限。これ以上探索を続けても詰みが見つかる見込みがないのでここで early return する。
  if (n.IsExceedLimit(option_.depth_limit)) {
    return SearchResult{RepetitionData{}};
  }

  // 必要があれば TCA による探索延長をしたいので、このタイミングで現局面の pn/dn を取得する。
  auto curr_result = cache.CurrentResult(n);
  // Threshold Controlling Algorithm(TCA).
  // 浅い結果を参照している場合、無限ループになる可能性があるので少しだけ探索を延長する
  inc_flag = inc_flag || cache.DoesHaveOldChild();
  if (inc_flag && curr_result.IsNotFinal()) {
    if (curr_result.Pn() < kInfinitePnDn) {
      thpn = Clamp(thpn, curr_result.Pn() + 1);
    }

    if (curr_result.Dn() < kInfinitePnDn) {
      thdn = Clamp(thdn, curr_result.Dn() + 1);
    }
  }

  if (monitor_.ShouldGc()) {
    tt_.CollectGarbage();
    monitor_.ResetNextGc();
  }

  while (!monitor_.ShouldStop() && !curr_result.Exceeds(thpn, thdn)) {
    // cache.BestMove() にしたがい子局面を展開する
    // （curr_result.Pn() > 0 && curr_result.Dn() > 0 なので、BestMove が必ず存在する）
    auto best_move = cache.BestMove();
    bool is_first_search = cache.BestMoveIsFirstVisit();
    BitSet64 sum_mask = cache.BestMoveSumMask();
    auto [child_thpn, child_thdn] = cache.ChildThreshold(thpn, thdn);

    n.DoMove(best_move);

    // ChildrenCache をローカル変数として持つとスタックが枯渇する。v0.4.1時点では
    //     sizeof(ChildrenCache) == 10832
    // なので、ミクロコスモスを解く場合、スタック領域が 16.5 MB 必要になる。スマホや低スペックPCでも動作するように
    // したいので、ChildrenCache は動的メモリにより確保する。
    //
    // 確保したメモリは UndoMove する直前で忘れずに解放しなければならない。
    auto& child_cache = children_cache_.emplace(tt_, n, is_first_search, sum_mask, &cache);

    SearchResult child_result;
    if (is_first_search) {
      child_result = child_cache.CurrentResult(n);
      // 新規局面を展開したので、TCA による探索延長をこれ以上続ける必要はない
      inc_flag = false;

      // 子局面を初展開する場合、child_result を計算した時点で threshold を超過する可能性がある
      // しかし、SearchImpl をコールしてしまうと TCA の探索延長によりすぐに返ってこない可能性がある
      // ゆえに、この時点で Exceed している場合は SearchImpl を呼ばないようにする。
      if (child_result.Exceeds(child_thpn, child_thdn)) {
        goto CHILD_SEARCH_END;
      }
    }
    child_result = SearchImpl(n, child_thpn, child_thdn, child_cache, inc_flag);

  CHILD_SEARCH_END:
    // 動的に確保した ChildrenCache の領域を忘れずに開放する
    children_cache_.pop();
    n.UndoMove(best_move);

    cache.UpdateBestChild(child_result);
    curr_result = cache.CurrentResult(n);
  }

  return curr_result;
}

std::vector<Move> KomoringHeights::TraceBestMove(Node& n) {
  std::vector<Move> moves;

  for (;;) {
    auto query = tt_.GetQuery(n);
    auto entry = query.LookUpWithoutCreation();
    Move best_move = MOVE_NONE;

    bool should_search = false;
    if (entry->GetNodeState() == NodeState::kProvenState) {
      best_move = n.Pos().to_move(entry->BestMove(n.OrHand()));
      if (best_move != MOVE_NONE && (!n.Pos().pseudo_legal(best_move) || !n.Pos().legal(best_move))) {
        should_search = true;
      }
    } else {
      should_search = true;
    }

    if (should_search) {
      sync_cout << "info string research " << n.GetDepth() << sync_endl;
      auto result = SearchEntry(n);
      if (auto proven = result.TryGetProven()) {
        best_move = n.Pos().to_move(proven->BestMove(n.OrHand()));
      }
    }

    if (best_move == MOVE_NONE || n.IsRepetitionAfter(best_move)) {
      break;
    }

    moves.push_back(best_move);
    n.DoMove(best_move);
  }

  RollBack(n, moves);
  return moves;
}

void KomoringHeights::PrintYozume(Node& n, const std::vector<Move>& pv) {
  std::vector<std::pair<Depth, Move>> yozume;
  std::vector<std::pair<Depth, Move>> unknown;

  Depth leaf_depth = static_cast<Depth>(pv.size()) - 1;
  for (const auto& move : pv) {
    if (n.IsOrNode() && n.GetDepth() < leaf_depth) {
      std::ostringstream oss;

      oss << n.GetDepth() + 1 << ": ";
      oss << move << "(proven)";
      bool should_print = false;
      for (auto&& branch_move : MovePicker{n}) {
        if (move == branch_move.move) {
          continue;
        }

        auto query = tt_.GetChildQuery(n, branch_move.move);
        auto entry = query.LookUpWithoutCreation();

        oss << ", " << branch_move.move;
        switch (entry->GetNodeState()) {
          case NodeState::kProvenState:
            oss << "(proven)";
            yozume.emplace_back(n.GetDepth() + 1, branch_move.move);
            should_print = true;
            break;
          case NodeState::kDisprovenState:
            oss << "(disproven)";
            break;
          case NodeState::kRepetitionState:
            oss << "(rep)";
            break;
          default:
            oss << "(unknown)";
            should_print = true;
            unknown.emplace_back(n.GetDepth() + 1, branch_move.move);
        }
      }

      if (should_print && option_.yozume_print_level >= YozumeVerboseLevel::kAll) {
        auto info = CurrentInfo();
        info.Set(UsiInfo::KeyKind::kDepth, n.GetDepth() + 1);
        info.Set(UsiInfo::KeyKind::kString, oss.str());
        sync_cout << info << sync_endl;
      }
    }

    n.DoMove(move);
  }

  if (option_.yozume_print_level >= YozumeVerboseLevel::kOnlyYozume) {
    if (yozume.empty()) {
      auto info = CurrentInfo();
      info.Set(UsiInfo::KeyKind::kString, "no yozume found");
      sync_cout << info << sync_endl;
    } else {
      SplittedPrint(CurrentInfo(), "yozume:", yozume);
    }
  }

  if (option_.yozume_print_level >= YozumeVerboseLevel::kYozumeAndUnknown) {
    if (unknown.empty()) {
      auto info = CurrentInfo();
      info.Set(UsiInfo::KeyKind::kString, "no unknown branch");
      sync_cout << info << sync_endl;
    } else {
      SplittedPrint(CurrentInfo(), "unknown:", unknown);
    }
  }

  RollBack(n, pv);
}

void KomoringHeights::PrintIfNeeded(const Node& n) {
  if (!print_flag_) {
    return;
  }
  print_flag_ = false;

  auto usi_output = CurrentInfo();

  usi_output.Set(UsiInfo::KeyKind::kDepth, n.GetDepth());
#if defined(KEEP_LAST_MOVE)
  usi_output.Set(UsiInfo::KeyKind::kPv, n.Pos().moves_from_start());
#endif

  sync_cout << usi_output << sync_endl;

  monitor_.Tick();
}
}  // namespace komori
