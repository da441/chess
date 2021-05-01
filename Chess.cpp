#include <iostream>
#include <thread>
#include <cassert>

#include "Chess.h"
#include "PieceSquareTables.h"
#include "TranspositionTable.h"
#include "Utils.h"

static Variant variant = VARIANT_NONE;
static TranspositionTable* ttable;
static int positions_checked;

static bool within_bounds(int x, int y)
{
  return x >= 0 && x < 8 && y >= 0 && y < 8;
}

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
  , castling_rights{ true, true, true, true }
  , material{ 0 }
  , previous_state(nullptr)
  , previous_move(nullptr)
  , zobrist_hash(0)
  , endgame_reached(false)
  , eval(0)
{
  for (int y = 2; y < 6; y ++) {
    for (int x = 0; x < 8; x++) {
      board[y][x].occupancy = NONE;
    }
  }
  for (int x = 0; x < 8; x++) {
    add_piece(x, 1, PAWN, WHITE);
    add_piece(x, 6, PAWN, BLACK);
  }
  PieceColour c = WHITE;
  for (int y = 0; y <= 7; y += 7) {
    for (int x = 0; x < 8; x++) {
      add_piece(x, y, back_rank[x], c);
    }
    c = BLACK;
  }

  psts[PAWN] = pawn_pst;
  psts[KNIGHT] = knight_pst;
  psts[BISHOP] = bishop_pst;
  psts[ROOK] = rook_pst;
  psts[QUEEN] = queen_pst;
  psts[KING] = king_mg_pst;

  enumerate_all_moves();
}

BoardState::BoardState(const BoardState *prev_state, const Move *move, bool enum_moves)
  : zobrist_hash(prev_state->zobrist_hash)
  , en_passant_available{ -1, -1 }
  , endgame_reached(prev_state->endgame_reached)
  , eval(0)
  , evaluated(false)
{
  memcpy(board, prev_state->board, sizeof(board));
  memcpy(castling_rights, prev_state->castling_rights, sizeof(castling_rights));
  memcpy(psts, prev_state->psts, sizeof(psts));
  memcpy(material, prev_state->material, sizeof(material));

  bool piece_captured = false;

  if (prev_state->en_passant_available.x >= 0) {
    ttable->zobrist_xor_en_passant(
      zobrist_hash, prev_state->en_passant_available.x);
  }

  if (board[move->from.y][move->from.x].occupancy == PAWN) {
    int pawn_displacement = move->to.y - move->from.y;
    if (pawn_displacement > 1 || pawn_displacement < -1) {
      // Mark a double-moving pawn as able to be captured en passant
      en_passant_available =
        Coords(move->from.x, move->from.y + pawn_displacement / 2);
      ttable->zobrist_xor_en_passant(zobrist_hash, move->from.x);
    } else if (move->from.x != move->to.x &&
      board[move->to.y][move->to.x].occupancy == NONE) {
      // If a pawn moved diagonally to an unoccupied square, it's en passant
      remove_piece(move->to.x, move->to.y - pawn_displacement);
      piece_captured = true;
    }
  }

  // Castling
  if (board[move->from.y][move->from.x].occupancy == KING) {
    int king_displacement = move->to.x - move->from.x;
    if (king_displacement > 1 || king_displacement < -1) {
      int king_move_direction = king_displacement / 2;
      int rook_old_x = move->to.x;
      while (rook_old_x % 7 != 0)
        rook_old_x += king_move_direction;
      add_piece(move->from.x + king_move_direction, move->from.y,
        board[move->from.y][rook_old_x]);
      remove_piece(rook_old_x, move->from.y);
    }
  }

  // If a king or rook moves, it can no longer castle
  for (int i = 0; i < 4; i++) {
    int back_rank = i < 2 ? 0 : 7;
    int rook_x = i % 2 ? 0 : 7;
    if (castling_rights[i] &&
        (move->from == Coords(4,      back_rank) ||
         move->from == Coords(rook_x, back_rank) ||
         move->to   == Coords(rook_x, back_rank))) {
      castling_rights[i] = false;
      ttable->zobrist_xor_castling_rights(
        zobrist_hash, static_cast<CastlingRight>(i));
    }
  }

  switch (variant) {
  case VARIANT_ATOMIC:
    if (board[move->to.y][move->to.x].occupancy != NONE) {
      for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
          if (!within_bounds(move->to.x + dx, move->to.y + dy))
            continue;
          switch (board[move->to.y + dy][move->to.x + dx].occupancy) {
          case NONE:
            break;
          case PAWN:
            if (dx || dy)
              break;
          default:
            remove_piece(move->to.x + dx, move->to.y + dy);
            piece_captured = true;
            break;
          }
        }
      }
    } else {
      add_piece(move->to.x, move->to.y, board[move->from.y][move->from.x]);
    }
    // Only remove piece if we haven't already done so above
    if (board[move->from.y][move->from.x].occupancy != NONE) {
      remove_piece(move->from.x, move->from.y);
      piece_captured = true;
    }
    break;
  default:
    if (board[move->to.y][move->to.x].occupancy != NONE) {
      material[board[move->to.y][move->to.x].colour][board[move->to.y][move->to.x].occupancy]--;
      piece_captured = true;
      (material[WHITE][QUEEN] == 0 || material[WHITE][KNIGHT] + material[WHITE][BISHOP] + material[WHITE][ROOK] < 2);
      // remove taken piece from hash
      ttable->zobrist_xor_piece(zobrist_hash, static_cast<PieceType>(
          !board[move->to.y][move->to.x].colour * 6 +
          board[move->to.y][move->to.x].occupancy
        ), move->to.x, move->to.y);
    }
    add_piece(move->to.x, move->to.y, board[move->from.y][move->from.x]);
    remove_piece(move->from.x, move->from.y);
    break;
  }

  // Queen a pawn that made its way to the end
  if (board[move->to.y][move->to.x].occupancy == PAWN && move->to.y % 7 == 0) {
    remove_piece(move->to.x, move->to.y);
    add_piece(move->to.x, move->to.y, QUEEN, board[move->to.y][move->to.x].colour);
  }

  whites_turn = !prev_state->whites_turn;
  ttable->zobrist_xor_player(zobrist_hash);

  if (enum_moves)
    enumerate_all_moves();
  moves_enumerated = enum_moves;

  if (!endgame_reached && piece_captured) {
    // See if we've now reached the endgame
    int players_in_endgame = 0;
    for (int i = 0; i < 2; i++) {
      if (material[i][QUEEN] == 0 ||
          material[i][KNIGHT] + material[i][BISHOP] + material[i][ROOK] < 2)
        players_in_endgame++;
    }
    if (players_in_endgame == 2) {
      endgame_reached = true;
      psts[KING] = king_eg_pst;
    }
  }

  previous_state = prev_state;
  previous_move = move;
  positions_checked++;

  // Check for draw by repetition
  int repetitions = 1;
  const BoardState *iter = previous_state;
  while (iter != nullptr) {
    if (iter->zobrist_hash == zobrist_hash)
      repetitions++;
    iter = iter->previous_state;
  }
  if (repetitions >= 3) {// Draw
    possible_moves.clear();
    moves_enumerated = true;
    eval = 0;
    evaluated = true;
  }
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
  possible_moves.emplace_back(from, to);
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
  if (castling_rights[whites_turn ? WHITE_QUEENSIDE : BLACK_QUEENSIDE] &&
      board[back_rank][1].occupancy == NONE &&
      board[back_rank][2].occupancy == NONE &&
      board[back_rank][3].occupancy == NONE &&
      !king_in_check(2, back_rank) &&
      !king_in_check(3, back_rank) &&
      !king_in_check(4, back_rank)) {
    Coords to(2, back_rank);
    add_move(from, to);
  }
  if (castling_rights[whites_turn ? WHITE_KINGSIDE : BLACK_KINGSIDE] &&
      board[back_rank][5].occupancy == NONE &&
      board[back_rank][6].occupancy == NONE &&
      !king_in_check(4, back_rank) &&
      !king_in_check(5, back_rank) &&
      !king_in_check(6, back_rank)) {
    Coords to(6, back_rank);
    add_move(from, to);
  }
}

void BoardState::enumerate_all_moves() {
  possible_moves.reserve(50);
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

void BoardState::add_piece(int x, int y, Square& sq)
{
  board[y][x] = sq;
  ttable->zobrist_xor_piece(zobrist_hash,
    static_cast<PieceType>(!sq.colour * 6 + sq.occupancy), x, y);
  material[sq.colour][sq.occupancy]++;
}

void BoardState::add_piece(int x, int y, Piece piece, PieceColour colour)
{
  board[y][x].occupancy = piece;
  board[y][x].colour = colour;
  ttable->zobrist_xor_piece(zobrist_hash,
    static_cast<PieceType>(!colour * 6 + piece - 1), x, y);
  material[colour][piece]++;
}

void BoardState::remove_piece(int x, int y)
{
  ttable->zobrist_xor_piece(zobrist_hash, static_cast<PieceType>(
      !board[y][x].colour * 6 + board[y][x].occupancy
    ), x, y);
  material[board[y][x].colour][board[y][x].occupancy]--;
  board[y][x].occupancy = NONE;
}

int BoardState::evaluate()
{
  static const int piece_values[] = { 100, 300, 300, 500, 900, 20000 };
  int score[2] = { 0 };
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      if (board[y][x].occupancy == NONE)
        continue;

      score[board[y][x].colour] += piece_values[board[y][x].occupancy];
      score[board[y][x].colour] +=
        psts[board[y][x].occupancy][(board[y][x].colour == WHITE ? 7 - y : y) * 8 + x];

      if (board[y][x].occupancy == KING) {
        if (moves_enumerated && possible_moves.size() == 0 &&
          (board[y][x].colour == WHITE) == whites_turn) {
          if (!king_in_check(x, y))
            // stalemate
            return 0;
          // checkmate
          return board[y][x].colour == WHITE ? INT16_MIN : INT16_MAX;
        }
        if (variant == VARIANT_HILL && x >= 3 && x <= 4 && y >= 3 && y <= 4) {
          return board[y][x].colour == WHITE ? INT16_MAX : INT16_MIN;
        }
        if (!endgame_reached) {// King safety
          const int forwards = board[y][x].colour * 2 - 1;
          int i = 1;
          while (i <= 2 && within_bounds(x, y + forwards * i)) {
            if (board[y + forwards * i][x].occupancy == PAWN &&
              board[y + forwards * i][x].colour == board[y][x].colour) {
              score[board[y][x].colour] += 50;
              break;
            }
            i++;
          }
        }
      }
    }
  }
  for (int i = 0; i < 2; i++) {
    if (material[i][BISHOP] == 2)
      // has a bishop pair
      score[i] += 20;
  }
  return score[WHITE] - score[BLACK];
}

int BoardState::Evaluate()
{
  if (!evaluated) {
    eval = evaluate();
    evaluated = true;
  }
  return eval;
}

void BoardState::UpdateEval(int score)
{
  eval = score;
}

static bool sort_fn(BoardState& a, BoardState& b)
{
  return a.whites_turn
    ? a.Evaluate() < b.Evaluate()
    : a.Evaluate() > b.Evaluate();
}

static int negamax(BoardState& state, int depth, int alpha, int beta, int colour)
{
  int original_alpha = alpha;

  TableEntry *entry;
  if (ttable->search(state.zobrist_hash, depth, &entry)) {
    switch (entry->flag) {
    case FLAG_EXACT:
      return entry->eval;
    case FLAG_LOWER_BOUND:
      alpha = max(alpha, entry->eval);
      break;
    case FLAG_UPPER_BOUND:
      beta = min(beta, entry->eval);
      break;
    }
    if (alpha >= beta)
      return entry->eval;
  }
  const int num_moves = state.possible_moves.size();
  if (depth == 0 || num_moves == 0)
    return state.Evaluate() * colour;

  vector<BoardState> trial_states;
  trial_states.reserve(num_moves);
  for (int move_num = 0; move_num < num_moves; move_num++) {
    trial_states.emplace_back(&state, &state.possible_moves[move_num], depth > 1);
  }

  // Sorting again towards the end of the search gives little reordering,
  // and stops being worth the cost of sorting
  if (depth > 2)
    sort(trial_states.begin(), trial_states.end(), sort_fn);

  int value = INT_MIN;
  for (int i = 0; i < num_moves; i++) {
    value = max(value, -negamax(trial_states[i], depth - 1, -beta, -alpha, -colour));
    alpha = max(value, alpha);
    if (alpha >= beta)
      break;
  }

  ttable->add(state.zobrist_hash, depth, value,
    value <= original_alpha ? FLAG_UPPER_BOUND : value >= beta ? FLAG_LOWER_BOUND : FLAG_EXACT);
  return value;
}

const Move* BoardState::find_best_move()
{
  const int num_moves = possible_moves.size();
  const Move *best_move = &possible_moves[0];
  int best_score = INT_MIN;
  int search_depth = 0;
  Timer timer;
  positions_checked = 0;

  vector<BoardState> trial_states;
  trial_states.reserve(num_moves);
  for (int move_num = 0; move_num < num_moves; move_num++) {
    trial_states.emplace_back(this, &possible_moves[move_num]);
  }

  while (timer.elapsed() < 5.0) {
    const Move *best_move_this_iter = best_move;
    int best_score_this_iter = INT_MIN;
    int alpha = INT16_MIN, beta = INT16_MAX;

    sort(trial_states.begin(), trial_states.end(), sort_fn);

    for (int move_num = 0; move_num < num_moves; move_num++) {
      int score = -negamax(trial_states[move_num], search_depth, -beta, -alpha, whites_turn ? -1 : 1);
      alpha = max(score, alpha);
      trial_states[move_num].UpdateEval(whites_turn ? score : -score);
      if (score > best_score_this_iter) {
        best_score_this_iter = score;
        best_move_this_iter = trial_states[move_num].previous_move;
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
  cout << "Checked " << positions_checked << " positions in total\n";
  string str;
  move_to_string(this, best_move, str);
  cout << "Best move " << str << " has score " << best_score << "\n";

  ttable->clear();

  if (variant == VARIANT_NONE && best_score <= -1000) {
    cout << "Resigns\n";
    return nullptr;
  }

  return best_move;
}

int main()
{
  ttable = new TranspositionTable();
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

    list<BoardState> game;
    game.emplace_back();

    print_board(game.back());

    while (game.back().possible_moves.size()) {
      if (num_players > 0 &&
        !(game.size() == 1 && num_players == 1 && !engine_plays_black)) {
        cout << "Please enter your move\n";
        cin >> user_input;
        while (user_input == "Undo" || user_input == "undo") {
          game.pop_back();
          game.pop_back();
          cout << "\n";
          print_board(game.back());
          cout << "Please enter your move\n";
          cin >> user_input;
        }
        const Move *user_move = nullptr;
        if (user_input == "Resign" || user_input == "resign" ||
          user_input == "Retry" || user_input == "retry" ||
          user_input == "Restart" || user_input == "restart") {
          break;
        } else if (user_input == "Exit" || user_input == "exit" ||
            user_input == "Quit" || user_input == "quit") {
          return 0;
        } else if (user_input == "Moves" || user_input == "moves") {
          for (unsigned i = 0; i < game.back().possible_moves.size(); i++) {
            string str;
            move_to_string(&game.back(), &game.back().possible_moves[i], str);
            cout << str << (i < game.back().possible_moves.size() - 1 ? ", " : ".");
          }
          cout << "\n";
        } else if (user_input == "Hint" || user_input == "hint") {
          const Move* best_move = game.back().find_best_move();
          if (!best_move)
            break;
          game.emplace_back(&game.back(), best_move);
          print_board(game.back());
        } else if (parse_move_string(game.back(), user_input, user_move)) {
          game.emplace_back(&game.back(), user_move);
          cout << "\n";
          print_board(game.back());
        } else {
          cout << "Failed to find a legal move matching that instruction\n";
          continue;
        }
      }

      if (num_players < 2) {
        const Move* best_move = game.back().find_best_move();
        if (!best_move)
          break;
        game.emplace_back(&game.back(), best_move);
        print_board(game.back());
      }
    }
  }
}
