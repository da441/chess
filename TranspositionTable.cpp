#include "TranspositionTable.h"

TranspositionTable::TranspositionTable() {
  rng.seed(11195303932578022943);
  for (int i = 0; i < num_random_numbers; i++) {
    random_numbers[i] = rng();
  }
}

void TranspositionTable::add(uint64_t hash, int depth, int eval, TableEntryFlag flag)
{
  if (map.count(hash)) {
    // already exists but we've recalculated => we've gone deeper, so replace
    map.erase(hash);
  }
  map.emplace(hash, TableEntry(depth, eval, flag));
}

bool TranspositionTable::search(uint64_t hash, int depth, TableEntry **entry)
{
  auto result = map.find(hash);
  if (result != map.end()) {
    *entry = &result->second;
    if ((*entry)->depth >= depth) {
      return true;
    }
  }
  return false;
}

void TranspositionTable::clear()
{
  map.clear();
}

void TranspositionTable::zobrist_xor_piece(
  uint64_t& hash, PieceType piece, int x_pos, int y_pos)
{
  hash ^= random_numbers[piece * 64 + y_pos * 8 + x_pos];
}

void TranspositionTable::zobrist_xor_player(uint64_t& hash)
{
  hash ^= random_numbers[64 * NUM_PIECE_TYPES + 1];
}

void TranspositionTable::zobrist_xor_castling_rights(
  uint64_t& hash, CastlingRight castling_right)
{
  hash ^= random_numbers[64 * NUM_PIECE_TYPES + 1 + castling_right];
}

void TranspositionTable::zobrist_xor_en_passant(
  uint64_t& hash, int en_passant_file)
{
  hash ^= random_numbers[64 * NUM_PIECE_TYPES + 1 + 4 + en_passant_file];
}
