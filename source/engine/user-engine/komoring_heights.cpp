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
constexpr std::int64_t kGcInterval = 100'000'000;

constexpr std::size_t kSplittedPrintLen = 12;
constexpr int kPostSearchVisitThreshold = 200;

/// 詰み手数と持ち駒から MateLen を作る
inline MateLen MakeMateLen(Depth depth, Hand hand) {
  return {static_cast<std::uint16_t>(depth), static_cast<std::uint16_t>(std::min(15, CountHand(hand)))};
}

/// Pv探索で現在の最善手および alpha, beta を管理するクラス
class PvMoveLen {
 public:
  PvMoveLen(Node& n, MateLen alpha, MateLen beta)
      : or_node_{n.IsOrNode()}, orig_alpha_{alpha}, orig_beta_{beta}, alpha_{alpha}, beta_{beta}, trivial_cut_{false} {
    // mate_len_ の初期化
    // OR node では+∞、AND node では 0 で初期化する
    if (or_node_) {
      mate_len_ = kMaxMateLen;
      const auto min_mate_len = MakeMateLen(1, n.OrHand());
      if (orig_beta_ < min_mate_len) {
        // どう見ても fail high のときは（下限値の）1 手詰めということにしておく
        Update(MOVE_NONE, min_mate_len);
        trivial_cut_ = true;
      }
    } else {
      mate_len_ = kZeroMateLen;

      alpha_ = Max(alpha_, mate_len_);
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

  bool IsEnd() const { return trivial_cut_ || alpha_ >= beta_; }

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
  bool trivial_cut_;
};

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

bool ShouldContinuePostSearchAndNode(const std::uint64_t move_searched, std::uint64_t alpha_len) {
  return move_searched <= alpha_len * alpha_len * 20000;
}
}  // namespace

namespace detail {
void SearchMonitor::Init(Thread* thread) {
  thread_ = thread;
}

void SearchMonitor::NewSearch() {
  start_time_ = std::chrono::system_clock::now();
  depth_ = 0;

  move_limit_ = std::numeric_limits<std::uint64_t>::max();
  limit_stack_ = {};
  ResetNextGc();
}

UsiInfo SearchMonitor::GetInfo() const {
  auto curr_time = std::chrono::system_clock::now();
  auto time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - start_time_).count();
  time_ms = std::max(time_ms, decltype(time_ms){1});
  auto move_count = MoveCount();
  auto nps = move_count * 1000ULL / time_ms;

  UsiInfo output;
  output.Set(UsiInfo::KeyKind::kSelDepth, depth_)
      .Set(UsiInfo::KeyKind::kTime, time_ms)
      .Set(UsiInfo::KeyKind::kNodes, move_count)
      .Set(UsiInfo::KeyKind::kNps, nps);

  return output;
}

void SearchMonitor::ResetNextGc() {
  next_gc_count_ = MoveCount() + kGcInterval;
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
  monitor_.NewSearch();
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

  monitor_.DisableGc();
  auto info = CurrentInfo();
  info.Set(UsiInfo::KeyKind::kString, ToString(result));
  sync_cout << info << sync_endl;

  if (result.GetNodeState() == NodeState::kProvenState) {
    best_moves_.clear();
    MateLen mate_len;
    for (int i = 0; i < 50; ++i) {
      // MateLen::len は unsigned なので、調子に乗って alpha の len をマイナスにするとバグる（一敗）
      auto tree_moves = pv_tree_.Pv(node);
      std::unordered_map<Key, int> visit_count;
      mate_len = PostSearch(visit_count, node, kZeroMateLen, kMaxMateLen);
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
  PvMoveLen pv_move_len{n, alpha, beta};
  bool repetition = false;
  const auto orig_move_count = monitor_.MoveCount();

  if (pv_move_len.IsEnd()) {
    return pv_move_len.GetMateLen();
  }

  PrintIfNeeded(n);

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
    auto&& probed_range = pv_tree_.Probe(n);
    if (probed_range.min_mate_len == probed_range.max_mate_len) {
      // 探索したことがある局面なら、その結果を再利用する。
      return probed_range.max_mate_len;
    }

    // OR node かつ upper bound かつ 手数が alpha よりも小さいなら、見込み 0 なので探索を打ち切れる
    // 3 番目の条件（entry->mate_len < alpha) は等号を入れてはいけない。もしここで等号を入れて
    // 探索を打ち切ってしまうと、ちょうど alpha 手詰めのときに詰み手順の探索が行われない可能性がある。
    //
    // AND node の場合も同様。
    if (pv_move_len.LessThanAlpha(probed_range.max_mate_len)) {
      pv_move_len.Update(probed_range.best_move, probed_range.max_mate_len);
      goto PV_END;
    } else if (pv_move_len.GreaterThanBeta(probed_range.min_mate_len)) {
      pv_move_len.Update(probed_range.best_move, probed_range.min_mate_len);
      goto PV_END;
    }

    auto key = n.Pos().key();
    visit_count[key]++;
    if (visit_count[key] > kPostSearchVisitThreshold) {
      // 何回もこの局面を通っているのに評価値が確定していないのは様子がおかしい。込み入った千日手が絡んだ局面である
      // 可能性が高い。これ以上この局面を調べても意味があまりないため、以前に見つけた上界値・下界値を結果として
      // 返してしまう。

      if (n.IsOrNode() && probed_range.max_mate_len < kMaxMateLen) {
        pv_move_len.Update(probed_range.best_move, probed_range.max_mate_len);
        goto PV_END;
      } else if (!n.IsOrNode() && probed_range.min_mate_len > kZeroMateLen) {
        pv_move_len.Update(probed_range.best_move, probed_range.min_mate_len);
        goto PV_END;
      }
    }

    // このタイミングで探索を打ち切れない場合でも、最善手だけは覚えて置くと後の探索が楽になる
    tt_move = probed_range.best_move;

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
      // 『メガロポリス』や『メタ新世界』のように、玉方の応手の組み合わせが指数関数的に増加するような問題の場合、
      // いつまで経っても探索が終わらなくなることがしばしばある。そのため。alpha の大きさを基準にして
      // 度を越して大量の手を探索している場合、適当なところで探索を間引く必要がある。
      if (!n.IsOrNode() && alpha.len > 3 && pv_move_len.GetMateLen().len > 0) {
        const auto move_count = monitor_.MoveCount();
        if (!ShouldContinuePostSearchAndNode(move_count - orig_move_count, alpha.len)) {
          break;
        }
      }

      if (monitor_.ShouldStop() || pv_move_len.IsEnd()) {
        break;
      }

      if (n.IsRepetitionAfter(move.move)) {
        repetition = true;
        if (n.IsOrNode()) {
          continue;
        } else {
          break;
        }
      }

      if (!n.IsOrNode() && n.IsRepetitionOrSuperiorAfter(move.move)) {
        continue;
      }

      auto result = PostSearchEntry(n, move);
      // 置換表に書いてある手（tt_move）なのに詰みを示せなかった
      // これを放置すると PV 探索に失敗する可能性があるので、再探索を行い置換表に書き込む
      if (tt_move == move.move && result.GetNodeState() != NodeState::kProvenState) {
        n.DoMove(move.move);
        auto nn = n.HistoryClearedNode();
        result = SearchEntry(nn);
        n.UndoMove(move.move);
      }

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
          auto child_mate_len = PostSearch(visit_count, n, pv_move_len.Alpha() - 1, pv_move_len.Beta() - 1);
          n.UndoMove(move.move);
          if (child_mate_len >= kMaxMateLen) {
            repetition = true;
            continue;
          }
          pv_move_len.Update(move.move, child_mate_len + 1);

          if (n.IsOrNode() && pv_move_len.LessThanOrigBeta()) {
            // mate_len < orig_beta が確定したので、この局面は高々 mate_len 手詰
            pv_tree_.Insert(n, BOUND_UPPER, pv_move_len.GetMateLen(), pv_move_len.GetBestMove());
          } else if (!n.IsOrNode() && pv_move_len.GreaterThanOrigAlpha()) {
            // mate_len > orig_alpha が確定したので、この局面は少なくとも mate_len 手詰以上
            pv_tree_.Insert(n, BOUND_LOWER, pv_move_len.GetMateLen(), pv_move_len.GetBestMove());
          }
        } else {
          n.UndoMove(move.move);
        }
      }
    }

    pickers_.pop();
  }

PV_END:
  // 千日手が原因で詰み／不詰の判断が狂った可能性がある場合は置換表に exact を書かない
  if (!repetition && pv_move_len.IsExactBound()) {
    pv_tree_.Insert(n, BOUND_EXACT, pv_move_len.GetMateLen(), pv_move_len.GetBestMove());
  }

  if (repetition && !n.IsOrNode()) {
    return kMaxMateLen;
  }

  return pv_move_len.GetMateLen();
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

  if (curr_result.Pn() > 0 && curr_result.Dn() > 10000000) {
    n.GetDepth();
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
}
}  // namespace komori
