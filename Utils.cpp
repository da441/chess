#include <iostream>

#include "utils.h"

void move_to_string(const BoardState *state, const Move* move, string& str)
{
  switch (state->board[move->from.y][move->from.x].occupancy) {
  case PAWN:
    if (state->board[move->to.y][move->to.x].occupancy == NONE) {
      str.push_back('a' + move->to.x);
      str.push_back('1' + move->to.y);
      return;
    }
    str.push_back('a' + move->from.x);
    break;
  case KNIGHT:
    str.push_back('N');
    break;
  case BISHOP:
    str.push_back('B');
    break;
  case ROOK:
    str.push_back('R');
    break;
  case QUEEN:
    str.push_back('Q');
    break;
  case KING:
    str.push_back('K');
    break;
  }
  if (state->board[move->to.y][move->to.x].occupancy != NONE)
    str.push_back('x');
  str.push_back('a' + move->to.x);
  str.push_back('1' + move->to.y);
  return;
}

static bool is_letter_coord(char c)
{
  return c >= 'a' && c <= 'h';
}

static bool is_number_coord(char c)
{
  return c >= '1' && c <= '8';
}

bool parse_move_string(const BoardState& state, const string str, const Move* &move)
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
  case 'O':
    if (str == "O-O") {
      for (int i = 0; i < state.possible_moves.size(); i++) {
        const Move *found_move = &state.possible_moves[i];
        if (found_move->to.x == 6 && found_move->from.x == 4 &&
          state.board[found_move->from.y][found_move->from.x].occupancy == KING) {
          move = found_move;
          return true;
        }
      }
    }
    else if (str == "O-O-O") {
      for (int i = 0; i < state.possible_moves.size(); i++) {
        const Move *found_move = &state.possible_moves[i];
        if (found_move->to.x == 2 && found_move->from.x == 4 &&
          state.board[found_move->from.y][found_move->from.x].occupancy == KING) {
          move = found_move;
          return true;
        }
      }
    }
    return false;
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
      Coords dest = Coords(str[2] - 'a', str[3] - '1');
      for (int i = 0; i < state.possible_moves.size(); i++) {
        const Move *found_move = &state.possible_moves[i];
        if (found_move->to == dest && found_move->from.x == str[0] - 'a' &&
          state.board[found_move->from.y][found_move->from.x].occupancy == PAWN) {
          move = found_move;
          return true;
        }
      }
    } else if (is_number_coord(str[1]) && str[2] == '\0') {
      Coords dest = Coords(str[0] - 'a', str[1] - '1');
      for (int i = 0; i < state.possible_moves.size(); i++) {
        const Move *found_move = &state.possible_moves[i];
        if (found_move->to == dest && found_move->from.x == dest.x &&
          state.board[found_move->from.y][found_move->from.x].occupancy == PAWN) {
          move = found_move;
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
    Coords dest = Coords(str[next_char_idx] - 'a', str[next_char_idx + 1] - '1');
    for (int i = 0; i < state.possible_moves.size(); i++) {
      const Move *found_move = &state.possible_moves[i];
      if (found_move->to == dest && piece_to_move ==
        state.board[found_move->from.y][found_move->from.x].occupancy) {
        if (disambiguation_char &&
          found_move->from.x != disambiguation_char - 'a' &&
          found_move->from.y != disambiguation_char - '1')
          continue;
        move = found_move;
        return true;
      }
    }
  }
  return false;
}

void print_board(BoardState& state)
{
  for (int i = 7; i >= 0; i--) {
    for (int j = 0; j < 8; j++) {
      cout << "\033[";
      if (state.board[i][j].occupancy != NONE &&
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
  int score = state.Evaluate();
  cout << "White's current score: " << score << "\n";
  cout << "\n";
}
