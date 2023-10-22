#include <benchmark/benchmark.h>

#include <string>
#include <utility>

#include "../../../thread.h"
#include "../../../usi.h"
#include "node.hpp"

using komori::Node;

namespace {
const std::string kMicroCosmosSfen =
    "g1+P1k1+P+P+L/1p3P3/+R+p2pp1pl/1NNsg+p2+R/+b+nL+P1+p3/1P3ssP1/2P1+Ps2N/4+P1P1L/+B5G1g b - 1";
const std::string kMicroCosmosAnsMoves =
    "4b4a+ 5a5b 7d6b+ 5b6b 7a6a 6b5b 6a5a 5b6b 8d7b+ 6b6c 6e7d 6c7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b P*6c 6b7b 7d8c 7b6c "
    "8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b 2a3a 3b2b 1a2a 2b1b N*2d 2c2d 2a1a 1b2b 1d2d P*2c 3a2a 2b3b "
    "4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b N*3d 4d3d 3a4a 4b3b "
    "2d3d P*3c 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b "
    "N*2d 2c2d 2a3a 3b2b 3d2d P*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b "
    "5a6a 6b5b 4a5a 5b4b 3a4a 4b3b 2a3a 3b2b N*1d 1c1d 1a2a 2b1b 2d1d P*1c 2a1a 1b2b 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b "
    "6a5a 5b6b L*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b N*2d 2c2d 2a3a 3b2b 1d2d L*2c "
    "3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b N*3d 3c3d "
    "3a4a 4b3b 2d3d P*3c 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b "
    "3a4a 4b3b N*2d 2c2d 2a3a 3b2b 3d2d P*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b L*6c 6b7b 7d8c 7b6c 8c8d N*8c "
    "8d7d 6c6b 5a6a 6b5b 4a5a 5b4b N*3d 3c3d 3a4a 4b3b 2d3d L*3c 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c "
    "8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b N*2d 2c2d 2a3a 3b2b 3d2d P*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b "
    "6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b 2a3a 3b2b N*3d 3c3d 7g7f 4e4d "
    "3a2a 2b3b 2d3d P*3c 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b L*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b "
    "3a4a 4b3b N*2d 2c2d 2a3a 3b2b 3d2d L*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c "
    "8d7d 6c6b 5a6a 6b5b 4a5a 5b4b N*3d 3c3d 3a4a 4b3b 2d3d P*3c 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c "
    "8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b N*2d 2c2d 2a3a 3b2b 3d2d P*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b "
    "6a5a 5b6b L*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b 2a3a 3b2b N*1d 1c1d 1a2a 2b1b "
    "2d1d L*1c 2a1a 1b2b 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b "
    "4a5a 5b4b 3a4a 4b3b 2a3a 3b2b 1a2a 2b1b N*2d 2c2d 9i8i 4d4e 2a1a 1b2b 1d2d P*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b "
    "6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b 2a3a 3b2b N*1d 1c1d 1a2a 2b1b "
    "2d1d P*1c 2a1a 1b2b 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b L*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b "
    "4a5a 5b4b 3a4a 4b3b N*2d 2c2d 2a3a 3b2b 1d2d L*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c "
    "8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b N*3d 3c3d 3a4a 4b3b 2d3d P*3c 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b "
    "7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b N*2d 2c2d 2a3a 3b2b 3d2d P*2c 3a2a 2b3b 4a3a 3b4b "
    "5a4a 4b5b 6a5a 5b6b L*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b N*3d 3c3d 3a4a 4b3b 2d3d L*3c "
    "4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b N*2d 2c2d "
    "2a3a 3b2b 3d2d P*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b "
    "4a5a 5b4b 3a4a 4b3b 2a3a 3b2b N*3d 3c3d 8i8h 4e4d 3a2a 2b3b 2d3d P*3c 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b L*6c 6b7b "
    "7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b N*2d 2c2d 2a3a 3b2b 3d2d L*2c 3a2a 2b3b 4a3a 3b4b "
    "5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b N*3d 3c3d 3a4a 4b3b 2d3d P*3c "
    "4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b N*2d 2c2d "
    "2a3a 3b2b 3d2d P*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b L*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b "
    "4a5a 5b4b 3a4a 4b3b 2a3a 3b2b N*1d 1c1d 1a2a 2b1b 2d1d L*1c 2a1a 1b2b 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b "
    "P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b 2a3a 3b2b 1a2a 2b1b N*2d 2c2d 8h7h 4d4e "
    "2a1a 1b2b 1d2d P*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b "
    "4a5a 5b4b 3a4a 4b3b 2a3a 3b2b N*1d 1c1d 1a2a 2b1b 2d1d P*1c 2a1a 1b2b 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b "
    "L*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b N*2d 2c2d 2a3a 3b2b 1d2d L*2c 3a2a 2b3b "
    "4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b N*3d 3c3d 3a4a 4b3b "
    "2d3d P*3c 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b "
    "N*2d 2c2d 2a3a 3b2b 3d2d P*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b L*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b "
    "5a6a 6b5b 4a5a 5b4b N*3d 3c3d 3a4a 4b3b 2d3d L*3c 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c "
    "8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b N*2d 2c2d 2a3a 3b2b 3d2d P*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b "
    "P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b 2a3a 3b2b N*3d 3c3d 7h7g 4e4d 3a2a 2b3b "
    "2d3d P*3c 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b L*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b "
    "N*2d 2c2d 2a3a 3b2b 3d2d L*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b "
    "5a6a 6b5b 4a5a 5b4b N*3d 3c3d 3a4a 4b3b 2d3d P*3c 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c "
    "8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b N*2d 2c2d 2a3a 3b2b 3d2d P*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b "
    "L*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b 2a3a 3b2b N*1d 1c1d 1a2a 2b1b 2d1d L*1c "
    "2a1a 1b2b 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b "
    "3a4a 4b3b 2a3a 3b2b 1a2a 2b1b N*2d 2c2d 7g6g 4d4e 2a1a 1b2b 1d2d P*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b "
    "P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b 2a3a 3b2b N*1d 1c1d 1a2a 2b1b 2d1d P*1c "
    "2a1a 1b2b 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b L*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b "
    "3a4a 4b3b N*2d 2c2d 2a3a 3b2b 1d2d L*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c "
    "8d7d 6c6b 5a6a 6b5b 4a5a 5b4b N*3d 3c3d 3a4a 4b3b 2d3d P*3c 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c "
    "8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b N*2d 2c2d 2a3a 3b2b 3d2d P*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b "
    "6a5a 5b6b L*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b N*3d 3c3d 3a4a 4b3b 2d3d L*3c 4a3a 3b4b "
    "5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b N*2d 2c2d 2a3a 3b2b "
    "3d2d P*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b "
    "3a4a 4b3b 2a3a 3b2b N*3d 3c3d 6g6f 4e4d 3a2a 2b3b 2d3d P*3c 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b L*6c 6b7b 7d8c 7b6c "
    "8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b N*2d 2c2d 2a3a 3b2b 3d2d L*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b "
    "6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b N*3d 3c3d 3a4a 4b3b 2d3d P*3c 4a3a 3b4b "
    "5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b N*2d 2c2d 2a3a 3b2b "
    "3d2d P*2c 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b L*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b "
    "3a4a 4b3b 2a3a 3b2b N*1d 1c1d 1a2a 2b1b 2d1d L*1c 2a1a 1b2b 3a2a 2b3b 4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b "
    "7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b N*2d 2c2d 2a3a 3b2b 1d2d P*2c 3a2a 2b3b 4a3a 3b4b "
    "5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b N*3d 3c3d 3a4a 4b3b 2d3d P*3c "
    "4a3a 3b4b 5a4a 4b5b 6a5a 5b6b P*6c 6b7b 7d8c 7b6c 8c8d N*8c 8d7d 6c6b 5a6a 6b5b 4a5a 5b4b 3a4a 4b3b 2a3a 3b2b "
    "N*1d 1c1d 9c8b 9a8b 1a2a 2b1b P*1c 1b1c 1g2e 3f2e 1h1d 2e1d 3d1d 1c1d L*1f 1d2d S*2e 2d3e 5g4f 3e2f 6f4h 4g4h "
    "4f3f 2f1g S*2h 1g1h 2h1i 1h1i G*2i";

StateInfo g_si;

std::vector<std::string> Split(std::string str, char c) {
  std::vector<std::string> ret;
  std::size_t i = 0;
  std::size_t j = str.find(c, i);
  while (j != std::string::npos) {
    ret.push_back(str.substr(i, j - i));

    i = j + 1;
    j = str.find(c, i);
  }

  ret.push_back(str.substr(i));
  return ret;
}

std::pair<std::unique_ptr<Position>, std::vector<Move>> GetMicrocosmos() {
  auto pos = std::make_unique<Position>();
  pos->set(kMicroCosmosSfen, &g_si, Threads.main());

  std::vector<Move> moves;
  std::deque<StateInfo> st;
  for (const auto& s : Split(kMicroCosmosAnsMoves, ' ')) {
    const auto move16 = USI::to_move16(s);
    const auto move = pos->to_move(move16);
    moves.push_back(move);

    pos->do_move(move, st.emplace_back());
  }

  for (auto itr = moves.rbegin(); itr != moves.rend(); ++itr) {
    pos->undo_move(*itr);
  }

  return {std::move(pos), std::move(moves)};
}

void Node_Microcosmos(benchmark::State& state) {
  const auto [pos, moves] = GetMicrocosmos();
  Node node{*pos, true};
  for (auto _ : state) {
    RollForward(node, moves);
    RollBack(node, moves);
  }
}
}  // namespace

BENCHMARK(Node_Microcosmos);
