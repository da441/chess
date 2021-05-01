#pragma once

#include <vector>

using namespace std;

enum Piece {
  PAWN,
  KNIGHT,
  BISHOP,
  ROOK,
  QUEEN,
  KING,
  NONE,
};

enum PieceColour {
  BLACK,
  WHITE,
};

enum Variant {
  VARIANT_NONE,
  VARIANT_ATOMIC,
  VARIANT_HILL,
};

class Square {
public:
  Piece occupancy;
  PieceColour colour;
};

class Coords {
public:
  Coords(int x_coord = 0, int y_coord = 0) {
    x = x_coord;
    y = y_coord;
  }
  bool operator==(const Coords& rhs) const {
    return rhs.x == x && rhs.y == y;
  }

  int x;
  int y;
};

class Move {
public:
  Move(void) {
    from = 0;
    to = 0;
  }
  Move(Coords& move_from, Coords& move_to) {
    from = move_from;
    to = move_to;
  }
  Move(Coords move_from, Coords move_to, int disambiguation) {
    from = move_from;
    to = move_to;
  }
  bool operator==(const Move& rhs) const {
    return rhs.from == from && rhs.to == to;
  }

  Coords from;
  Coords to;
};

#define MOVE_HISTORY_LEN 12

class BoardState {
public:
  BoardState(void);
  BoardState(const BoardState *prev_state, const Move *move, bool enum_moves = true);
  int Evaluate();
  void UpdateEval(int score);
  const Move* find_best_move();
  Square board[8][8];
  bool whites_turn;
  vector<Move> possible_moves;
  const BoardState* previous_state;
  const Move* previous_move;
  uint64_t zobrist_hash;

private:
  bool can_move_to_space(int x, int y);
  bool king_in_check(int x, int y);
  void add_move(Coords& from, Coords& to);
  void add_pawn_moves(int x, int y);
  void add_knight_moves(int x, int y);
  void add_bishop_moves(int x, int y);
  void add_rook_moves(int x, int y);
  void add_king_moves(int x, int y);
  void enumerate_all_moves();
  int evaluate();

  void add_piece(int x, int y, Square& sq);
  void add_piece(int x, int y, Piece piece, PieceColour colour);
  void remove_piece(int x, int y);

  Coords en_passant_available;
  bool castling_rights[4];
  int material[2][6];
  bool moves_enumerated;
  int eval;
  bool evaluated;
  bool endgame_reached;
  int* psts[6];
};
