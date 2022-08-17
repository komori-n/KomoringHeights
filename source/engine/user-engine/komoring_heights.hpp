#ifndef KOMORI_NEW_KH_HPP_
#define KOMORI_NEW_KH_HPP_

#include <chrono>

#include "../../thread.h"
#include "cc.hpp"
#include "circular_array.hpp"
#include "engine_option.hpp"
#include "tt.hpp"
#include "usi.hpp"

namespace komori {
enum class NodeState {
  kUnknown,
  kProven,
  kDisproven,
  kRepetition,
};

namespace detail {
class SearchMonitor {
 public:
  void Init(Thread* thread) { thread_ = thread; }
  void NewSearch(std::uint64_t gc_interval);

  void Visit(Depth depth) { depth_ = std::max(depth_, depth); }
  void Tick();
  UsiInfo GetInfo() const;
  std::uint64_t MoveCount() const { return thread_->nodes; }
  bool ShouldStop() const { return MoveCount() >= move_limit_ || stop_; }

  bool ShouldGc() const { return MoveCount() >= next_gc_count_; }
  void ResetNextGc();

  void PushLimit(std::uint64_t move_limit);
  void PopLimit();
  void SetStop(bool stop = true) { stop_ = stop; }

 private:
  static constexpr inline std::size_t kHistLen = 16;

  std::atomic_bool stop_{false};

  std::chrono::system_clock::time_point start_time_;
  Depth depth_;

  CircularArray<std::chrono::system_clock::time_point, kHistLen> tp_hist_;
  CircularArray<std::uint64_t, kHistLen> mc_hist_;
  std::size_t hist_idx_;

  std::uint64_t move_limit_;
  std::stack<std::uint64_t> limit_stack_;
  std::uint64_t gc_interval_;
  std::uint64_t next_gc_count_;
  Thread* thread_{nullptr};
};
}  // namespace detail

class KomoringHeights {
 public:
  KomoringHeights() = default;
  KomoringHeights(const KomoringHeights&) = delete;
  KomoringHeights(KomoringHeights&&) = delete;
  KomoringHeights& operator=(const KomoringHeights&) = delete;
  KomoringHeights& operator=(KomoringHeights&&) = delete;
  ~KomoringHeights() = default;

  void Init(EngineOption option, Thread* thread) {
    option_ = option;
    tt_.Resize(option_.hash_mb);
    monitor_.Init(thread);
  }

  void SetStop() { monitor_.SetStop(true); }
  void ResetStop() { monitor_.SetStop(false); }
  void RequestPrint() { print_flag_ = true; }
  UsiInfo CurrentInfo() const;

  const std::vector<Move>& BestMoves() const { return best_moves_; }

  NodeState Search(const Position& n, bool is_root_or_node);

 private:
  tt::SearchResult SearchEntry(Node& n, MateLen len, PnDn thpn = kInfinitePnDn, PnDn thdn = kInfinitePnDn);
  tt::SearchResult SearchImpl(Node& n, PnDn thpn, PnDn thdn, MateLen len, ChildrenCache& cache, bool inc_flag);

  void PrintIfNeeded(const Node& n);

  tt::TranspositionTable tt_;
  EngineOption option_;

  detail::SearchMonitor monitor_;
  Score score_{};
  std::atomic_bool print_flag_{false};

  std::vector<Move> best_moves_;

  // <一時変数>
  // 探索中に使用する一時変数。本当はスタック上に置きたいが、スタックオーバーフローしてしまうのでメンバで持つ。
  std::stack<ChildrenCache> children_cache_{};
  std::stack<MovePicker> pickers_{};
  // </一時変数>
};
}  // namespace komori

#endif  // KOMORI_NEW_KH_HPP_