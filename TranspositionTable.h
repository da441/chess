#pragma once

#include <unordered_map>
#include <cstdint>
#include <random>
using namespace std;

enum PieceType {
  WHITE_PAWN,
  WHITE_KNIGHT,
  WHITE_BISHOP,
  WHITE_ROOK,
  WHITE_QUEEN,
  WHITE_KING,
  BLACK_PAWN,
  BLACK_KNIGHT,
  BLACK_BISHOP,
  BLACK_ROOK,
  BLACK_QUEEN,
  BLACK_KING,
  NUM_PIECE_TYPES,
};

enum CastlingRight {
  WHITE_KINGSIDE,
  WHITE_QUEENSIDE,
  BLACK_KINGSIDE,
  BLACK_QUEENSIDE,
};

enum TableEntryFlag : uint8_t {
  FLAG_EXACT,
  FLAG_LOWER_BOUND,
  FLAG_UPPER_BOUND,
};

class TableEntry {
public:
  TableEntry(int d, int e, TableEntryFlag f) {
    depth = d;
    eval = e;
    flag = f;
  }
  int eval;
  uint8_t depth;
  TableEntryFlag flag;
};

constexpr int num_random_numbers = 64 * NUM_PIECE_TYPES + 1 + 4 + 8;

class TranspositionTable {
public:
  TranspositionTable();
  void add(uint64_t hash, int depth, int eval, TableEntryFlag flag);
  bool search(uint64_t hash, int depth, TableEntry **entry);
  void clear();
  void zobrist_xor_piece(uint64_t& hash, PieceType piece, int x_pos, int y_pos);
  void zobrist_xor_player(uint64_t& hash);
  void zobrist_xor_castling_rights(uint64_t& hash, CastlingRight castling_right);
  void zobrist_xor_en_passant(uint64_t& hash, int en_passant_file);
private:
  unordered_map<uint64_t, TableEntry> map;
  linear_congruential_engine<std::uint64_t, 48271, 0, ULLONG_MAX> rng;
  uint64_t random_numbers[num_random_numbers];
};
