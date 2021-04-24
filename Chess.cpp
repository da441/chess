#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cassert>

using namespace std;

class Timer {
public:
  Timer() : beg_(clock_::now()) {}
  void reset() { beg_ = clock_::now(); }
  double elapsed() const {
    return chrono::duration_cast<second_>
      (clock_::now() - beg_).count();
  }

private:
  typedef chrono::high_resolution_clock clock_;
  typedef chrono::duration<double, ratio<1> > second_;
  chrono::time_point<clock_> beg_;
};

enum Piece {
  NONE,
  PAWN,
  KNIGHT,
  BISHOP,
  ROOK,
  QUEEN,
  KING,
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

static Variant variant = VARIANT_NONE;

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

// Vector is O(n) to insert at the front, so let's reserve insertion space
class MoveVector {
public:
  MoveVector(void) {
    v = vector<Move>(15);
    v.reserve(50);
    m_front = 15;
    m_size = 0;
  }
  void emplace_back(Coords& from, Coords& to) {
    v.emplace_back(from, to);
    m_size++;
  }
  void emplace_front(Coords& from, Coords& to) {
    if (m_front > 0) {
      m_front--;
      v[m_front] = Move(from, to);
    } else {
      v.emplace(v.begin(), from, to);
    }
    m_size++;
  }
  Move& get(int i) {
    return v[m_front + i];
  }
  int size(void) {
    return m_size;
  }
  void clear(void) {
    m_size = 0;
  }
private:
  vector<Move> v;
  int m_front;
  int m_size;
};

static bool within_bounds(int x, int y)
{
  return x >= 0 && x < 8 && y >= 0 && y < 8;
}

class BoardState {
public:
  BoardState(void);
  BoardState(BoardState& previous_state, Move& move);
  int Evaluate(bool eval_for_white);

  Square board[8][8];
  bool whites_turn;
  MoveVector possible_moves;

private:
  bool can_move_to_space(int x, int y);
  bool king_in_check(int x, int y);
  void add_move(Coords& from, Coords& to);
  void add_pawn_moves(int x, int y);
  void add_knight_moves(int x, int y);
  void add_bishop_moves(int x, int y);
  void add_rook_moves(int x, int y);
  void add_king_moves(int x, int y);
  void enumerate_all_moves(void);

  Coords en_passant_available;
  bool can_castle_q_side[2];
  bool can_castle_k_side[2];
  int num_pieces_remaining;
};

static const Piece back_rank[] = {
  ROOK,
  KNIGHT,
  BISHOP,
  QUEEN,
  KING,
  BISHOP,
  KNIGHT,
  ROOK,
};

BoardState::BoardState(void)
  : whites_turn(true)
  , en_passant_available(0, 0)
  , can_castle_q_side{ true, true }
  , can_castle_k_side{ true, true }
  , num_pieces_remaining(32)
{
  memset(board, 0, sizeof(board));
  for (int x = 0; x < 8; x++) {
    board[1][x].occupancy = PAWN;
    board[1][x].colour = WHITE;
    board[6][x].occupancy = PAWN;
    board[6][x].colour = BLACK;
  }
  PieceColour c = WHITE;
  for (int y = 0; y <= 7; y += 7) {
    for (int x = 0; x < 8; x++) {
      board[y][x].occupancy = back_rank[x];
      board[y][x].colour = c;
    }
    c = BLACK;
  }

  enumerate_all_moves();
}

BoardState::BoardState(BoardState& previous_state, Move& move)
  : can_castle_q_side{ previous_state.can_castle_q_side[0],
                       previous_state.can_castle_q_side[1] }
  , can_castle_k_side{ previous_state.can_castle_k_side[0],
                       previous_state.can_castle_k_side[1] }
  , en_passant_available{ -1, -1 }
  , num_pieces_remaining(previous_state.num_pieces_remaining)
{
  memcpy(board, previous_state.board, sizeof(board));

  if (board[move.from.y][move.from.x].occupancy == PAWN) {
    int pawn_displacement = move.to.y - move.from.y;
    if (pawn_displacement > 1 || pawn_displacement < -1) {
      // Mark a double-moving pawn as able to be captured en passant
      en_passant_available =
        Coords(move.from.x, move.from.y + pawn_displacement / 2);
    } else if (move.from.x != move.to.x &&
      board[move.to.y][move.to.x].occupancy == NONE) {
      // If a pawn moved diagonally to an unoccupied square, it's en passant
      board[move.to.y - pawn_displacement][move.to.x].occupancy = NONE;
    }
  }

  // Castling
  if (board[move.from.y][move.from.x].occupancy == KING) {
    int king_displacement = move.to.x - move.from.x;
    if (king_displacement > 1 || king_displacement < -1) {
      int king_move_direction = king_displacement / 2;
      int rook_old_x = move.to.x;
      while (rook_old_x % 7 != 0)
        rook_old_x += king_move_direction;
      board[move.from.y][move.from.x + king_move_direction] =
        board[move.from.y][rook_old_x];
      board[move.from.y][rook_old_x].occupancy = NONE;
    }
  }

  // If a king or rook moves, it can no longer castle
  bool w = previous_state.whites_turn;
  if (can_castle_q_side[w] || can_castle_k_side[w]) {
    if (move.from == Coords(4, !w * 7)) {
      can_castle_q_side[w] = false;
      can_castle_k_side[w] = false;
    } else if (move.from == Coords(0, !w * 7)) {
      can_castle_q_side[w] = false;
    } else if (move.from == Coords(7, !w * 7)) {
      can_castle_k_side[w] = false;
    }
  }

  switch (variant) {
  case VARIANT_ATOMIC:
    if (board[move.to.y][move.to.x].occupancy != NONE) {
      for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
          if (!within_bounds(move.to.x + dx, move.to.y + dy))
            continue;
          switch (board[move.to.y + dy][move.to.x + dx].occupancy) {
          case NONE:
            break;
          case PAWN:
            if (dx || dy)
              break;
          default:
            board[move.to.y + dy][move.to.x + dx].occupancy = NONE;
            num_pieces_remaining--;
            break;
          }
        }
      }
    } else {
      board[move.to.y][move.to.x] = board[move.from.y][move.from.x];
    }
    board[move.from.y][move.from.x].occupancy = NONE;
    break;
  default:
    num_pieces_remaining -= (board[move.to.y][move.to.x].occupancy != NONE);
    board[move.to.y][move.to.x] = board[move.from.y][move.from.x];
    board[move.from.y][move.from.x].occupancy = NONE;
    break;
  }

  // Queen a pawn that made its way to the end
  if (board[move.to.y][move.to.x].occupancy == PAWN && move.to.y % 7 == 0)
    board[move.to.y][move.to.x].occupancy = QUEEN;

  whites_turn = !previous_state.whites_turn;

  enumerate_all_moves();
}

bool BoardState::can_move_to_space(int x, int y) {
  return within_bounds(x, y) && (board[y][x].occupancy == NONE ||
    (board[y][x].colour == BLACK) == whites_turn);
}

bool BoardState::king_in_check(int x, int y) {
  // check diagonals
  for (int i = -1; i <= 1; i += 2) {
    for (int j = -1; j <= 1; j += 2) {
      int m = 1;
      while (can_move_to_space(x + i * m, y + j * m)) {
        if (board[y + j * m][x + i * m].occupancy == BISHOP ||
            board[y + j * m][x + i * m].occupancy == QUEEN)
          return true;
        if (board[y + j * m][x + i * m].occupancy != NONE)
          break;
        m++;
      }
    }
  }
  // check orthogonals
  for (int i = -1; i <= 1; i += 2) {
    int m = 1;
    while (can_move_to_space(x + i * m, y)) {
      if (board[y][x + i * m].occupancy == ROOK ||
          board[y][x + i * m].occupancy == QUEEN)
        return true;
      if (board[y][x + i * m].occupancy != NONE)
        break;
      m++;
    }
    m = 1;
    while (can_move_to_space(x, y + i * m)) {
      if (board[y + i * m][x].occupancy == ROOK ||
          board[y + i * m][x].occupancy == QUEEN)
        return true;
      if (board[y + i * m][x].occupancy != NONE)
        break;
      m++;
    }
  }
  // check pawn captures
  const int enemy_pawn_move_direction = whites_turn ? -1 : 1;
  for (int i = -1; i <= 1; i += 2) {
    if (can_move_to_space(x + i, y - enemy_pawn_move_direction) &&
        board[y - enemy_pawn_move_direction][x + i].occupancy == PAWN)
      return true;
  }
  // check for adjacent king
  for (int i = -1; i <= 1; i++) {
    for (int j = -1; j <= 1; j++) {
      if (i == 0 && j == 0)
        continue;
      if (can_move_to_space(x + i, y + j) &&
          board[y + j][x + i].occupancy == KING)
        return true;
    }
  }
  return false;
}

void BoardState::add_move(Coords& from, Coords& to)
{
  if (board[to.y][to.x].occupancy == NONE)
    possible_moves.emplace_back(from, to);
  else
    possible_moves.emplace_front(from, to);
}

void BoardState::add_pawn_moves(int x, int y) {
  const int pawn_move_direction = whites_turn ? 1 : -1;
  Coords from(x, y);
  if (board[y + pawn_move_direction][x].occupancy == NONE) {
    Coords to(x, y + pawn_move_direction);
    add_move(from, to);
    if (y == (7 + pawn_move_direction) % 7 &&
      board[y + 2 * pawn_move_direction][x].occupancy == NONE) {
      Coords to(x, y + 2 * pawn_move_direction);
      add_move(from, to);
    }
  }
  for (int i = -1; i <= 1; i += 2) {
    if (within_bounds(x + i, y + pawn_move_direction) &&
        (
          (board[y + pawn_move_direction][x + i].occupancy != NONE &&
            (board[y + pawn_move_direction][x + i].colour == BLACK) == whites_turn)
          ||
          (en_passant_available.x == x + i &&
           en_passant_available.y == y + pawn_move_direction)
          )
        ) {
      Coords to(x + i, y + pawn_move_direction);
      add_move(from, to);
    }
  }
}

void BoardState::add_knight_moves(int x, int y) {
  Coords from(x, y);
  for (int d2 = -2; d2 <= 2; d2 += 4) {
    for (int d1 = -1; d1 <= 1; d1 += 2) {
      if (can_move_to_space(x + d2, y + d1)) {
        Coords to(x + d2, y + d1);
        add_move(from, to);
      }
      if (can_move_to_space(x + d1, y + d2)) {
        Coords to(x + d1, y + d2);
        add_move(from, to);
      }
    }
  }
}

void BoardState::add_bishop_moves(int x, int y) {
  Coords from(x, y);
  for (int i = -1; i <= 1; i += 2) {
    for (int j = -1; j <= 1; j += 2) {
      int m = 1;
      while (can_move_to_space(x + i * m, y + j * m)) {
        Coords to(x + i * m, y + j * m);
        add_move(from, to);
        if (board[y + j * m][x + i * m].occupancy != NONE)
          break;
        m++;
      }
    }
  }
}

void BoardState::add_rook_moves(int x, int y) {
  Coords from(x, y);
  for (int i = -1; i <= 1; i += 2) {
    int m = 1;
    while (can_move_to_space(x + i * m, y)) {
      Coords to(x + i * m, y);
      add_move(from, to);
      if (board[y][x + i * m].occupancy != NONE)
        break;
      m++;
    }
    m = 1;
    while (can_move_to_space(x, y + i * m)) {
      Coords to(x, y + i * m);
      add_move(from, to);
      if (board[y + i * m][x].occupancy != NONE)
        break;
      m++;
    }
  }
}

void BoardState::add_king_moves(int x, int y) {
  Coords from(x, y);
  for (int i = -1; i <= 1; i++) {
    for (int j = -1; j <= 1; j++) {
      if (i == 0 && j == 0)
        continue;
      if (can_move_to_space(x + i, y + j) &&
          !king_in_check(x + i, y + j)) {
        Coords to(x + i, y + j);
        add_move(from, to);
      }
    }
  }
  int back_rank = !whites_turn * 7;
  if (can_castle_q_side[whites_turn] &&
      board[back_rank][1].occupancy == NONE &&
      board[back_rank][2].occupancy == NONE &&
      board[back_rank][3].occupancy == NONE &&
      !king_in_check(2, back_rank) &&
      !king_in_check(3, back_rank) &&
      !king_in_check(4, back_rank)) {
    Coords to(2, back_rank);
    add_move(from, to);
  }
  if (can_castle_k_side[whites_turn] &&
      board[back_rank][5].occupancy == NONE &&
      board[back_rank][6].occupancy == NONE &&
      !king_in_check(4, back_rank) &&
      !king_in_check(5, back_rank) &&
      !king_in_check(6, back_rank)) {
    Coords to(6, back_rank);
    add_move(from, to);
  }
}

void BoardState::enumerate_all_moves(void) {
  bool king_present = false;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      if (board[y][x].occupancy == NONE)
        continue;
      if (whites_turn == (board[y][x].colour == BLACK)) {
        if (variant == VARIANT_HILL && board[y][x].occupancy == KING &&
            x >= 3 && x <= 4 && y >= 3 && y <= 4) {
          possible_moves.clear();
          return;
        }
        continue;
      }
      switch (board[y][x].occupancy) {
        case PAWN:
          add_pawn_moves(x, y);
          break;
        case KNIGHT:
          add_knight_moves(x, y);
          break;
        case BISHOP:
          add_bishop_moves(x, y);
          break;
        case ROOK:
          add_rook_moves(x, y);
          break;
        case QUEEN:
          add_bishop_moves(x, y);
          add_rook_moves(x, y);
          break;
        case KING:
          add_king_moves(x, y);
          king_present = true;
          break;
        default:
          assert(false);
          break;
      }
    }
  }
  if (!king_present)
    possible_moves.clear();
}

int BoardState::Evaluate(bool eval_for_white)
{
  int score[2] = { 0 };
  int material[2] = { 0 };
  int bishops[2] = { 0 };
  int pawn_development_bonus = 100 / num_pieces_remaining;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      switch (board[y][x].occupancy) {
      case PAWN:
        material[board[y][x].colour] += 100;
        score[board[y][x].colour] +=
          (board[y][x].colour ? y - 1 : 6 - y) * pawn_development_bonus;
        break;
      case KNIGHT:
        material[board[y][x].colour] += 300;
        if (!board[y][x].colour * 7 == y)
          // minor piece on back rank
          score[board[y][x].colour] -= 25;
        break;
      case BISHOP:
        material[board[y][x].colour] += 300;
        if (!board[y][x].colour * 7 == y)
          // minor piece on back rank
          score[board[y][x].colour] -= 25;
        bishops[board[y][x].colour]++;
        break;
      case ROOK:
        material[board[y][x].colour] += 500;
        break;
      case QUEEN:
        material[board[y][x].colour] += 900;
        break;
      case KING:
        if (possible_moves.size() == 0 &&
            (board[y][x].colour == WHITE) == whites_turn) {
          if (!king_in_check(x, y))
            // stalemate
            return 0;
          // checkmate
          return (eval_for_white == (board[y][x].colour == WHITE))
            ? INT_MIN : INT_MAX;
        }
        score[board[y][x].colour] += 1000000;
        if (variant == VARIANT_HILL && x >= 3 && x <= 4 && y >= 3 && y <= 4) {
          return (eval_for_white == (board[y][x].colour == WHITE))
            ? INT_MAX : INT_MIN;
        }
        break;
      default:
        break;
      }
    }
  }
  for (int i = 0; i < 2; i++) {
    if (bishops[i] == 2)
      // has a bishop pair
      score[i] += 20;
  }
  if (material[BLACK] > material[WHITE]) {
    // Black should want to trade down, so let's make their material worth less
    score[BLACK] += lround(material[BLACK] * 0.99);
    score[WHITE] += material[WHITE];
  } else if (material[BLACK] < material[WHITE]) {
    // Vice versa
    score[BLACK] += material[BLACK];
    score[WHITE] += lround(material[WHITE] * 0.99);
  }
  return (eval_for_white ? 1 : -1) * (score[WHITE] - score[BLACK]);
}

static void print_board(BoardState &state)
{
  for (int i = 7; i >= 0; i--) {
    for (int j = 0; j < 8; j++) {
      cout << "\033[";
      if (state.board[i][j].occupancy != NULL &&
          state.board[i][j].colour == BLACK) {
        cout << ";34";
      }
      cout << "m";
      switch (state.board[i][j].occupancy) {
      case PAWN:
        cout << "P";
        break;
      case KNIGHT:
        cout << "N";
        break;
      case BISHOP:
        cout << "B";
        break;
      case ROOK:
        cout << "R";
        break;
      case QUEEN:
        cout << "Q";
        break;
      case KING:
        cout << "K";
        break;
      default:
        cout << "_";
        break;
      }
      cout << "\033[m";
      cout << " ";
    }
    cout << "\n";
  }
  cout << (state.whites_turn ? "White" : "Black") << " to move.\n";
  int score = state.Evaluate(true);
  cout << "White's current score: " << score << "\n";
  cout << "\n";
}

static int minimax(BoardState& state, int depth, int alpha, int beta,
  bool max_player, bool play_as_white)
{
  const int num_moves = state.possible_moves.size();
  if (depth == 0 || num_moves == 0)
    return state.Evaluate(play_as_white);
  if (max_player) {
    int value = INT_MIN;
    for (int i = 0; i < num_moves; i++) {
      BoardState child(state, state.possible_moves.get(i));
      value = max(value, minimax(child, depth - 1, alpha, beta, false, play_as_white));
      alpha = max(value, alpha);
      if (alpha >= beta)
        break;
    }
    return value;
  } else {
    int value = INT_MAX;
    for (int i = 0; i < num_moves; i++) {
      BoardState child(state, state.possible_moves.get(i));
      value = min(value, minimax(child, depth - 1, alpha, beta, true, play_as_white));
      beta  = min(value, beta);
      if (beta <= alpha)
        break;
    }
    return value;
  }
}

static void minimax_thread(BoardState& state, int move_num, int depth,
  volatile int *ret, volatile bool *completed)
{
  BoardState trial_state(state, state.possible_moves.get(move_num));
  *ret = minimax(trial_state, depth, INT_MIN, INT_MAX, false, state.whites_turn);
  *completed = true;
}

static Move* find_best_move(BoardState& state)
{
  const int num_moves = state.possible_moves.size();
  Move *best_move = &state.possible_moves.get(0);
  int best_score = INT_MIN;
  int search_depth = 1;
  Timer timer;
  while (timer.elapsed() < 4.0) {
    Move *best_move_this_iter = best_move;
    int best_score_this_iter = INT_MIN;
    volatile int *move_scores = new volatile int[num_moves];
    volatile bool *work_completed = new bool[num_moves];
    for (int i = 0; i < num_moves; i++)
      work_completed[i] = false;
    vector<thread> threads;
    for (int move_num = 0; move_num < num_moves; move_num++) {
      threads.emplace_back(
        minimax_thread, ref(state), move_num, search_depth,
          &move_scores[move_num], &work_completed[move_num]
      );
    }
  keep_waiting:
    if (timer.elapsed() > 30.0) {
      for (int i = 0; i < num_moves; i++) {
        threads[i].detach();
      }
      break;
    }
    this_thread::sleep_for(chrono::milliseconds(100));
    for (int i = 0; i < num_moves; i++) {
      if (!work_completed[i])
        goto keep_waiting;
    }

    for (int i = 0; i < num_moves; i++) {
      threads[i].join();
      if (move_scores[i] > best_score_this_iter) {
        best_score_this_iter = move_scores[i];
        best_move_this_iter = &state.possible_moves.get(i);
      }
    }
    best_move = best_move_this_iter;
    best_score = best_score_this_iter;
    if (best_score > 9000 || best_score < -9000)
      break;
    search_depth++;
  }
  cout << "Evaluated to search depth " << search_depth << " in " <<
    timer.elapsed() << " seconds\n";
  cout << "Best move has score " << best_score << "\n";

  if (variant == VARIANT_NONE && best_score <= -1000) {
    cout << "Resigns\n";
    return nullptr;
  }

  return best_move;
}

static bool is_letter_coord(char c)
{
  return c >= 'a' && c <= 'h';
}

static bool is_number_coord(char c)
{
  return c >= '1' && c <= '8';
}

static bool parse_move_string(BoardState& state, string str, Move &move)
{
  Piece piece_to_move;
  switch (str[0]) {
  case 'N':
    piece_to_move = KNIGHT;
    break;
  case 'B':
    piece_to_move = BISHOP;
    break;
  case 'R':
    piece_to_move = ROOK;
    break;
  case 'Q':
    piece_to_move = QUEEN;
    break;
  case 'K':
    piece_to_move = KING;
    break;
  default:
    if (is_letter_coord(str[0]))
      piece_to_move = PAWN;
    else
      return false;
    break;
  }
  if (piece_to_move == PAWN) {
    if (str[1] == 'x') {
      if (!is_letter_coord(str[2]) || !is_number_coord(str[3]))
        return false;
      move.to.x = str[2] - 'a';
      move.to.y = str[3] - '1';
      for (int i = 0; i < state.possible_moves.size(); i++) {
        Move found_move = state.possible_moves.get(i);
        if (found_move.to == move.to && found_move.from.x == str[0] - 'a' &&
            state.board[found_move.from.y][found_move.from.x].occupancy == PAWN) {
          move.from = found_move.from;
          return true;
        }
      }
    } else if (is_number_coord(str[1]) && str[2] == '\0') {
      move.to.x = str[0] - 'a';
      move.to.y = str[1] - '1';
      for (int i = 0; i < state.possible_moves.size(); i++) {
        Move found_move = state.possible_moves.get(i);
        if (found_move.to == move.to && found_move.from.x == move.to.x &&
            state.board[found_move.from.y][found_move.from.x].occupancy == PAWN) {
          move.from = found_move.from;
          return true;
        }
      }
    }
    return false;
  }
  int next_char_idx = 1;
  char disambiguation_char = 0;
  if ((is_letter_coord(str[1]) || is_number_coord(str[1])) &&
      (is_letter_coord(str[2]) || str[2] == 'x'))
    disambiguation_char = str[next_char_idx++];
  if (str[next_char_idx] == 'x')
    next_char_idx++;
  if (is_letter_coord(str[next_char_idx]) &&
      is_number_coord(str[next_char_idx + 1])) {
    move.to.x = str[next_char_idx] - 'a';
    move.to.y = str[next_char_idx + 1] - '1';
    for (int i = 0; i < state.possible_moves.size(); i++) {
      Move found_move = state.possible_moves.get(i);
      if (found_move.to == move.to && piece_to_move ==
          state.board[found_move.from.y][found_move.from.x].occupancy) {
        if (disambiguation_char && 
            found_move.from.x != disambiguation_char - 'a' &&
            found_move.from.y != disambiguation_char - '1')
          continue;
        move.from = found_move.from;
        return true;
      }
    }
  }
  return false;
}

int main()
{
  while (true) {
    string user_input;
    int num_players = 1;
    bool engine_plays_black = true;
    cout << "How many players? (0, 1, 2)\n";
    cin >> user_input;
    if (user_input == "0" || user_input == "2")
      num_players = atoi(user_input.c_str());

    if (num_players == 1) {
      cout << "Computer colour? (white, black)\n";
      cin >> user_input;
      if (user_input == "White" || user_input == "white")
        engine_plays_black = false;
    }

    cout << "Variant? (atomic, hill)\n";
    cin >> user_input;
    if (user_input == "Atomic" || user_input == "atomic")
      variant = VARIANT_ATOMIC;
    if (user_input == "Hill" || user_input == "hill")
      variant = VARIANT_HILL;

    vector<BoardState> game;
    game.emplace_back();
    int move_num = 0;

    print_board(game[move_num]);

    while (game[move_num].possible_moves.size()) {
      if (num_players > 0 &&
        !(move_num == 0 && num_players == 1 && !engine_plays_black)) {
        cout << "Please enter your move\n";
        cin >> user_input;
        while (user_input == "Undo" || user_input == "undo") {
          game.pop_back();
          game.pop_back();
          move_num -= 2;
          cout << "\n";
          print_board(game[move_num]);
          cout << "Please enter your move\n";
          cin >> user_input;
        }
        Move user_move;
        if (user_input == "Resign" || user_input == "resign" ||
          user_input == "Retry" || user_input == "retry" ||
          user_input == "Restart" || user_input == "restart") {
          break;
        } else if (user_input == "Exit" || user_input == "exit" ||
            user_input == "Quit" || user_input == "quit") {
          return 0;
        } else if (user_input == "Hint" || user_input == "hint") {
          Move* best_move = find_best_move(game[move_num]);
          if (!best_move)
            break;
          game.emplace_back(
            game[move_num++],
            *best_move
          );
          print_board(game[move_num]);
        } else if (parse_move_string(game[move_num], user_input, user_move)) {
          game.emplace_back(
            game[move_num++],
            user_move
          );
          cout << "\n";
          print_board(game[move_num]);
        } else {
          cout << "Failed to find a legal move matching that instruction\n";
          continue;
        }
      }

      if (num_players < 2) {
        Move* best_move = find_best_move(game[move_num]);
        if (!best_move)
          break;
        game.emplace_back(
          game[move_num++],
          *best_move
        );
        print_board(game[move_num]);
      }
    }
  }
}
