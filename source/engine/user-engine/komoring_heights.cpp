#if !defined(USER_ENGINE)
#define USER_ENGINE
#endif

#include "komoring_heights.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>

#include "../../mate/mate.h"
#include "children_cache.hpp"
#include "hands.hpp"
#include "move_picker.hpp"
#include "node_history.hpp"
#include "path_keys.hpp"

namespace komori {
namespace {
constexpr std::int64_t kGcInterval = 3000;
constexpr PnDn kIncreaseDeltaThreshold = 100;

/// TT の使用率が kGcHashfullThreshold を超えたら kGcHashfullRemoveRatio だけ削除する
constexpr int kGcHashfullThreshold = 700;
constexpr int kGcHashfullRemoveRatio = 200;

/// 詰み手数と持ち駒から MateLen を作る
inline MateLen MakeMateLen(Depth depth, Hand hand) {
  return {static_cast<std::uint16_t>(depth), static_cast<std::uint16_t>(std::min(15, CountHand(hand)))};
}

/// Pv探索で現在の最善手および alpha, beta を管理するクラス
class PvMoveLen {
 public:
  PvMoveLen(Node& n, MateLen alpha, MateLen beta)
      : or_node_{n.IsOrNode()}, orig_alpha_{alpha}, orig_beta_{beta}, alpha_{alpha}, beta_{beta} {
    if (or_node_) {
      if (IsTrivialCut()) {
        // どう見ても fail high のときは（下限値の）1 手詰めということにしておく
        mate_len_ = MakeMateLen(1, n.OrHand());
      } else {
        mate_len_ = kMaxMateLen;
      }
    } else {
      mate_len_ = MakeMateLen(0, n.OrHand());
    }
  }

  // 子局面の探索結果が child_mate_len のとき、alpha/beta と mate_len の更新を行う
  void Update(Move move, MateLen child_mate_len) {
    if (or_node_) {
      // child_mate_len 手詰みが見つかったので、この局面は高々 child_mate_len 手詰み。
      beta_ = Min(beta_, child_mate_len);
      if (mate_len_ > child_mate_len) {
        best_move_ = move;
        mate_len_ = child_mate_len;
      }
    } else {
      // child_mate_len 手詰みが見つかったので、この局面は少なくとも child_mate_len 手詰み以上。
      alpha_ = Max(alpha_, child_mate_len);
      if (mate_len_ < child_mate_len) {
        best_move_ = move;
        mate_len_ = child_mate_len;
      }
    }
  }

  bool IsEnd() const { return alpha_ >= beta_; }
  bool IsTrivialCut() const {
    // 詰み手数は非負なので、探索するまでもなく fail high と判定できる
    if (orig_beta_.len == 0) {
      if (or_node_) {
        return true;
      } else {
        if (orig_beta_ <= mate_len_) {
          return true;
        }
      }
    }
    return false;
  }

  bool LessThanAlpha(MateLen mate_len) const { return mate_len < alpha_; }
  bool GreaterThanOrigAlpha() const { return mate_len_ > orig_alpha_; }
  bool LessThanOrigBeta() const { return mate_len_ < orig_beta_; }
  bool GreaterThanBeta(MateLen mate_len) const { return mate_len > beta_; }
  bool IsExactBound() const { return GreaterThanOrigAlpha() && LessThanOrigBeta(); }

  MateLen GetMateLen() const { return mate_len_; }
  Move GetBestMove() const { return best_move_; }

  MateLen Alpha() const { return alpha_; }
  MateLen Beta() const { return beta_; }

 private:
  const bool or_node_;
  Move best_move_{MOVE_NONE};

  MateLen mate_len_;
  const MateLen orig_alpha_;
  const MateLen orig_beta_;
  MateLen alpha_;
  MateLen beta_;
};

std::vector<std::pair<Move, SearchResult>> ExpandChildren(TranspositionTable& tt, const Node& n) {
  std::vector<std::pair<Move, SearchResult>> ret;
  for (auto&& move : MovePicker{n}) {
    auto query = tt.GetChildQuery(n, move.move);
    auto entry = query.LookUpWithoutCreation();
    SearchResult result{*entry, n.OrHandAfter(move.move)};
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

void ExtendThreshold(PnDn& thpn, PnDn& thdn, PnDn pn, PnDn dn, bool or_node) {
  if (or_node) {
    thdn = Clamp(thdn, dn + 1);
    if (kIncreaseDeltaThreshold < pn && pn < kInfinitePnDn) {
      thpn = Clamp(thpn, pn + 1);
    }
  } else {
    thpn = Clamp(thpn, pn + 1);
    if (kIncreaseDeltaThreshold < dn && dn < kInfinitePnDn) {
      thdn = Clamp(thdn, dn + 1);
    }
  }
}
}  // namespace

namespace detail {
void SearchProgress::NewSearch(std::uint64_t max_num_moves, Thread* thread) {
  start_time_ = std::chrono::system_clock::now();
  depth_ = 0;
  thread_ = thread;
  max_num_moves_ = max_num_moves;
}

void SearchProgress::WriteTo(UsiInfo& output) const {
  auto curr_time = std::chrono::system_clock::now();
  auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - start_time_).count();
  time_ms = std::max(time_ms, decltype(time_ms){1});
  auto move_count = MoveCount();
  auto nps = move_count * 1000ULL / time_ms;

  output.Set(UsiInfo::KeyKind::kSelDepth, depth_)
      .Set(UsiInfo::KeyKind::kTime, time_ms)
      .Set(UsiInfo::KeyKind::kNodes, move_count)
      .Set(UsiInfo::KeyKind::kNps, nps);
}
}  // namespace detail

KomoringHeights::KomoringHeights() : tt_{kGcHashfullRemoveRatio} {}

void KomoringHeights::Init(std::uint64_t size_mb, Thread* thread) {
  tt_.Resize(size_mb);
  thread_ = thread;
}

NodeState KomoringHeights::Search(Position& n, bool is_root_or_node) {
  // <初期化>
  tt_.NewSearch();
  progress_.NewSearch(max_search_node_, thread_);
  pv_tree_.Clear();
  gc_timer_.reset();
  last_gc_ = 0;
  best_moves_.clear();
  // </初期化>

  // Position より Node のほうが便利なので、探索中は node を用いる
  Node node{n, is_root_or_node};

  // thpn/thdn で反復深化探索を行う
  PnDn thpn = 1;
  PnDn thdn = 1;
  SearchResult result = SearchEntry(node, thpn, thdn);
  while (!IsFinal(result.GetNodeState()) && !IsSearchStop()) {
    // 反復深化のしきい値を適当に伸ばす
    thpn = Clamp(thpn, 2 * result.Pn(), kInfinitePnDn);
    thdn = Clamp(thdn, 2 * result.Dn(), kInfinitePnDn);
    score_ = Score::Unknown(result.Pn(), result.Dn());

    result = SearchEntry(node, thpn, thdn);
  }

  auto info = Info();
  info.Set(UsiInfo::KeyKind::kString, ToString(result));
  sync_cout << info << sync_endl;

  if (result.GetNodeState() == NodeState::kProvenState) {
    // MateLen::len は unsigned なので、調子に乗って alpha の len をマイナスにするとバグる（一敗）
    auto mate_len = PvSearch(node, kZeroMateLen, kMaxMateLen);
    sync_cout << "info string mate_len=" << mate_len << sync_endl;

    best_moves_ = pv_tree_.Pv(node);
    if (best_moves_.size() % 2 != (is_root_or_node ? 1 : 0)) {
      sync_cout << "info string Failed to detect PV" << sync_endl;
    }
    return NodeState::kProvenState;
  } else {
    if (result.GetNodeState() == NodeState::kDisprovenState || result.GetNodeState() == NodeState::kRepetitionState) {
      score_ = Score::Disproven(result.GetMateLen().len, is_root_or_node);
    }
    return result.GetNodeState();
  }
}

MateLen KomoringHeights::PvSearch(Node& n, MateLen alpha, MateLen beta) {
  Key key = n.Pos().key();
  PvMoveLen pv_move_len{n, alpha, beta};

  if (pv_move_len.IsTrivialCut()) {
    return pv_move_len.GetMateLen();
  }

  if (print_flag_) {
    PrintProgress(n);
    print_flag_ = false;
  }

  // 1手詰チェック
  if (n.IsOrNode() && !n.Pos().in_check()) {
    if (Move move = Mate::mate_1ply(n.Pos()); move != MOVE_NONE) {
      pv_move_len.Update(move, MakeMateLen(1, n.OrHandAfter(move)));
      // 1手詰を見つけたので終わり（最終手余詰は考えない）
      goto PV_END;
    }
  }

  {
    // 置換表にそこそこよい手が書かれているはず
    // それを先に読んで alpha/beta を更新しておくことで、探索が少しだけ高速化される
    auto tt_move = MOVE_NONE;
    if (auto&& proved_entry = pv_tree_.Probe(n)) {
      if (proved_entry->bound == BOUND_EXACT) {
        // 探索したことがある局面なら、その結果を再利用する。
        return proved_entry->mate_len;
      }

      // OR node かつ upper bound かつ 手数が alpha よりも小さいなら、見込み 0 なので探索を打ち切れる
      // 3 番目の条件（entry->mate_len < alpha) は等号を入れてはいけない。もしここで等号を入れて
      // 探索を打ち切ってしまうと、ちょうど alpha 手詰めのときに詰み手順の探索が行われない可能性がある。
      //
      // AND node の場合も同様。
      if ((n.IsOrNode() && proved_entry->bound == BOUND_UPPER && pv_move_len.LessThanAlpha(proved_entry->mate_len)) ||
          (!n.IsOrNode() && proved_entry->bound == BOUND_LOWER &&
           pv_move_len.GreaterThanBeta(proved_entry->mate_len))) {
        pv_move_len.Update(proved_entry->best_move, proved_entry->mate_len);
        goto PV_END;
      }

      // このタイミングで探索を打ち切れない場合でも、最善手だけは覚えて置くと後の探索が楽になる
      tt_move = proved_entry->best_move;
    }

    // pv_tree_ に登録されていなければ、置換表から最善手を読んでくる
    // （n が詰みなので、ほとんどの場合は置換表にエンドが保存されている）
    if (tt_move == MOVE_NONE) {
      tt_move = tt_.LookUpBestMove(n);
    }

    // 優等局面の詰みから現局面の詰みを示した場合、置換表に書いてある最善手を指せないことがある
    // そのときは、子局面の手の中からいい感じの手を選んで優先的に探索する
    if (!n.Pos().pseudo_legal(tt_move) || !n.Pos().legal(tt_move)) {
      tt_move = SelectBestMove(tt_, n);
    }

    auto& mp = pickers_.emplace(n, true);
    std::sort(mp.begin(), mp.end(), [tt_move](const ExtMove& m1, const ExtMove& m2) {
      // tt_move がいちばん手前にくるようにする
      if (m1.move == tt_move) {
        return true;
      } else if (m2.move == tt_move) {
        return false;
      }
      return m1.value < m2.value;
    });

    for (const auto& move : mp) {
      if (IsSearchStop() || pv_move_len.IsEnd()) {
        break;
      }

      if (n.IsRepetitionAfter(move.move) ||                               // 千日手
          (!n.IsOrNode() && n.IsRepetitionOrSuperiorAfter(move.move))) {  // AND node で優等ループにはまっている
        continue;
      }

      auto result = YozumeSearchEntry(n, move);
      if (result.GetNodeState() == NodeState::kProvenState) {
        // move を選べば詰み

        n.DoMove(move.move);
        // 無駄合探索
        bool need_search = true;
        if (auto capture_move = n.ImmidiateCapture(); capture_move != MOVE_NONE) {
          // 現在王手している駒ですぐ取り返して（capture_move）、取った駒を玉方に返しても詰むなら無駄合
          auto useless_result = UselessDropSearchEntry(n, capture_move);
          if (useless_result.GetNodeState() == NodeState::kProvenState) {
            // 無駄合。move を選んではダメ。
            // move は非合法手と同じ扱いでなかったことにする
            need_search = false;
          }
        }

        if (need_search) {
          auto child_mate_len = PvSearch(n, pv_move_len.Alpha() - 1, pv_move_len.Beta() - 1);
          n.UndoMove(move.move);
          pv_move_len.Update(move.move, child_mate_len + 1);

          if (n.IsOrNode() && pv_move_len.LessThanOrigBeta()) {
            // mate_len < orig_beta が確定したので、この局面は高々 mate_len 手詰
            PvTree::Entry entry{BOUND_UPPER, pv_move_len.GetMateLen(), pv_move_len.GetBestMove()};
            pv_tree_.Insert(n, entry);
          } else if (!n.IsOrNode() && pv_move_len.GreaterThanOrigAlpha()) {
            // mate_len > orig_alpha が確定したので、この局面は少なくとも mate_len 手詰以上
            PvTree::Entry entry{BOUND_UPPER, pv_move_len.GetMateLen(), pv_move_len.GetBestMove()};
            pv_tree_.Insert(n, entry);
          }
        } else {
          n.UndoMove(move.move);
        }
      }
    }

    pickers_.pop();
  }

PV_END:
  if (pv_move_len.IsExactBound()) {
    PvTree::Entry entry{BOUND_EXACT, pv_move_len.GetMateLen(), pv_move_len.GetBestMove()};
    pv_tree_.Insert(n, entry);
  }

  return pv_move_len.GetMateLen();
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
      if (node.IsOrNode()) {
        if (lhs.second.Pn() != rhs.second.Pn()) {
          return lhs.second.Pn() < rhs.second.Pn();
        } else if (lhs.second.Pn() == 0 && rhs.second.Pn() == 0) {
          return lhs.second.GetMateLen() < rhs.second.GetMateLen();
        }

        if (lhs.second.Dn() != rhs.second.Dn()) {
          return lhs.second.Dn() > rhs.second.Dn();
        } else if (lhs.second.Dn() == 0 && rhs.second.Dn() == 0) {
          return lhs.second.GetMateLen() > rhs.second.GetMateLen();
        }
        return false;
      } else {
        if (lhs.second.Dn() != rhs.second.Dn()) {
          return lhs.second.Dn() < rhs.second.Dn();
        } else if (lhs.second.Dn() == 0 && rhs.second.Dn() == 0) {
          return lhs.second.GetMateLen() < rhs.second.GetMateLen();
        }

        if (lhs.second.Pn() != rhs.second.Pn()) {
          return lhs.second.Pn() > rhs.second.Pn();
        } else if (lhs.second.Pn() == 0 && rhs.second.Pn() == 0) {
          return lhs.second.GetMateLen() > rhs.second.GetMateLen();
        }
        return false;
      }
    });

    std::ostringstream oss;
    oss << "[" << node.GetDepth() << "] ";
    for (const auto& child : children) {
      if (child.second.Pn() == 0) {
        oss << child.first << "(+" << child.second.GetMateLen() << ") ";
      } else if (child.second.Dn() == 0) {
        oss << child.first << "(-" << child.second.GetMateLen() << ") ";
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

UsiInfo KomoringHeights::Info() const {
  UsiInfo usi_output{};
  progress_.WriteTo(usi_output);
  usi_output.Set(UsiInfo::KeyKind::kHashfull, tt_.Hashfull()).Set(UsiInfo::KeyKind::kScore, score_);

  return usi_output;
}

SearchResult KomoringHeights::YozumeSearchEntry(Node& n, Move move) {
  auto query = tt_.GetChildQuery(n, move);
  auto entry = query.LookUpWithoutCreation();
  if (entry->IsFinal()) {
    return {*entry, n.OrHandAfter(move)};
  } else {
    n.DoMove(move);
    progress_.StartYozumeSearch(yozume_node_count_);
    auto result = SearchEntry(n);
    progress_.EndYozumeSearch();
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
    result = {*entry, n.OrHandAfter(move)};
  } else {
    progress_.StartYozumeSearch(yozume_node_count_);
    result = SearchEntry(n);
    progress_.EndYozumeSearch();
  }

  n.UnstealCapturedPiece();
  n.UndoMove(move);

  return result;
}

SearchResult KomoringHeights::SearchEntry(Node& n, PnDn thpn, PnDn thdn) {
  ChildrenCache cache{tt_, n, true};
  auto move_count_org = progress_.MoveCount();
  auto result = SearchImpl(n, thpn, thdn, cache, false);

  auto query = tt_.GetQuery(n);
  result.UpdateSearchedAmount(progress_.MoveCount() - move_count_org);
  query.SetResult(result);

  return result;
}

SearchResult KomoringHeights::SearchImpl(Node& n, PnDn thpn, PnDn thdn, ChildrenCache& cache, bool inc_flag) {
  progress_.Visit(n.GetDepth());

  if (print_flag_) {
    PrintProgress(n);
    print_flag_ = false;
  }

  // 深さ制限。これ以上探索を続けても詰みが見つかる見込みがないのでここで early return する。
  if (n.IsExceedLimit(max_depth_)) {
    return {NodeState::kRepetitionState, kMinimumSearchedAmount, kInfinitePnDn, 0, n.OrHand()};
  }

  // 必要があれば TCA による探索延長をしたいので、このタイミングで現局面の pn/dn を取得する。
  auto curr_result = cache.CurrentResult(n);
  // Threshold Controlling Algorithm(TCA).
  // 浅い結果を参照している場合、無限ループになる可能性があるので少しだけ探索を延長する
  inc_flag = inc_flag || cache.DoesHaveOldChild();
  if (inc_flag && !curr_result.IsFinal()) {
    ExtendThreshold(thpn, thdn, curr_result.Pn(), curr_result.Dn(), n.IsOrNode());
  }

  if (gc_timer_.elapsed() > last_gc_ + kGcInterval) {
    if (tt_.Hashfull() >= kGcHashfullThreshold) {
      tt_.CollectGarbage();
    }
    last_gc_ = gc_timer_.elapsed();
  }

  while (!IsSearchStop() && !curr_result.Exceeds(thpn, thdn)) {
    // cache.BestMove() にしたがい子局面を展開する
    // （curr_result.Pn() > 0 && curr_result.Dn() > 0 なので、BestMove が必ず存在する）
    auto best_move = cache.BestMove();
    bool is_first_search = cache.BestMoveIsFirstVisit();
    auto [child_thpn, child_thdn] = cache.ChildThreshold(thpn, thdn);

    // 子局面を何局面分探索したのかを見るために探索前に move count を取得しておく
    auto move_count_org = progress_.MoveCount();
    n.DoMove(best_move);

    // ChildrenCache をローカル変数として持つとスタックが枯渇する。v0.4.1時点では
    //     sizeof(ChildrenCache) == 10832
    // なので、ミクロコスモスを解く場合、スタック領域が 16.5 MB 必要になる。スマホや低スペックPCでも動作するように
    // したいので、ChildrenCache は動的メモリにより確保する。
    //
    // 確保したメモリは UndoMove する直前で忘れずに解放しなければならない。
    auto& child_cache = children_cache_.emplace(tt_, n, is_first_search);

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

    cache.UpdateBestChild(child_result, progress_.MoveCount() - move_count_org);
    curr_result = cache.CurrentResult(n);
  }

  return curr_result;
}

void KomoringHeights::PrintProgress(const Node& n) const {
  auto usi_output = Info();

  usi_output.Set(UsiInfo::KeyKind::kDepth, n.GetDepth());
#if defined(KEEP_LAST_MOVE)
  usi_output.Set(UsiInfo::KeyKind::kPv, n.Pos().moves_from_start());
#endif

  sync_cout << usi_output << sync_endl;
}

bool KomoringHeights::IsSearchStop() const {
  return progress_.IsStop() || stop_;
}
}  // namespace komori
