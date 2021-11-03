#include "proof_hand.hpp"

namespace komori {
void RemoveHand(Hand& hand, PieceType pr) {
  hand = static_cast<Hand>(hand & ~PIECE_BIT_MASK2[pr]);
}

Hand MergeHand(Hand h1, Hand h2) {
  return static_cast<Hand>(h1 + h2);
}

Hand CollectHand(const Position& n) {
  return MergeHand(n.hand_of(BLACK), n.hand_of(WHITE));
}

int CountHand(Hand hand) {
  int count = 0;
  for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
    count += hand_count(hand, pr);
  }
  return count;
}

Hand AfterHand(const Position& n, Move move, Hand before_hand) {
  if (is_drop(move)) {
    auto pr = move_dropped_piece(move);
    if (hand_exists(before_hand, pr)) {
      sub_hand(before_hand, move_dropped_piece(move));
    }
  } else {
    if (auto to_pc = n.piece_on(to_sq(move)); to_pc != NO_PIECE) {
      auto pr = raw_type_of(to_pc);
      add_hand(before_hand, pr);
      // オーバーフローしてしまった場合はそっと戻しておく
      if (before_hand & HAND_BORROW_MASK) {
        sub_hand(before_hand, pr);
      }
    }
  }
  return before_hand;
}

Hand BeforeHand(const Position& n, Move move, Hand after_hand) {
  if (is_drop(move)) {
    auto pr = move_dropped_piece(move);
    add_hand(after_hand, pr);
    // オーバーフローしてしまった場合はそっと戻しておく
    if (after_hand & HAND_BORROW_MASK) {
      sub_hand(after_hand, pr);
    }
  } else {
    auto to_pc = n.piece_on(to_sq(move));
    if (to_pc != NO_PIECE) {
      auto pr = raw_type_of(to_pc);
      if (hand_exists(after_hand, pr)) {
        sub_hand(after_hand, pr);
      }
    }
  }
  return after_hand;
}

Hand RemoveIfHandGivesOtherChecks(const Position& n, Hand disproof_hand) {
  Color us = n.side_to_move();
  Color them = ~n.side_to_move();
  Hand hand = n.hand_of(us);
  Square king_sq = n.king_square(them);
  auto droppable_bb = ~n.pieces();

  for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
    if (!hand_exists(hand, pr)) {
      // 二歩の場合は反証駒を消す必要はない（打てないので）
      if (pr == PAWN && (n.pieces(us, PAWN) & FILE_BB[file_of(king_sq)])) {
        continue;
      }

      if (droppable_bb.test(StepEffect(pr, them, king_sq))) {
        // pr を持っていたら王手ができる -> pr は反証駒から除かれるべき
        RemoveHand(disproof_hand, pr);
      }
    }
  }
  return disproof_hand;
}

Hand AddIfHandGivesOtherEvasions(const Position& n, Hand proof_hand) {
  auto us = n.side_to_move();
  auto them = ~us;
  Hand us_hand = n.hand_of(us);
  Hand them_hand = n.hand_of(them);
  auto king_sq = n.king_square(n.side_to_move());
  auto checkers = n.checkers();

  if (checkers.pop_count() != 1) {
    return proof_hand;
  }

  auto checker_sq = checkers.pop();
  if (!between_bb(king_sq, checker_sq)) {
    return proof_hand;
  }

  // 駒を持っていれば合駒で詰みを防げたかもしれない（合法手が増えるから）
  for (PieceType pr = PIECE_HAND_ZERO; pr < PIECE_HAND_NB; ++pr) {
    if (pr == PAWN) {
      bool double_pawn = true;
      auto bb = between_bb(king_sq, checker_sq);
      while (bb) {
        auto sq = bb.pop();
        if (!(n.pieces(us, PAWN) & FILE_BB[file_of(sq)])) {
          double_pawn = false;
          break;
        }
      }

      if (double_pawn) {
        continue;
      }
    }

    if (!hand_exists(us_hand, pr)) {
      // pr を持っていれば詰みを防げた（かもしれない）
      RemoveHand(proof_hand, pr);
      proof_hand = MergeHand(proof_hand, static_cast<Hand>(hand_exists(them_hand, pr)));
    }
  }

  return proof_hand;
}
}  // namespace komori