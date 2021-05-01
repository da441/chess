#pragma once

#include <chrono>
#include <string>

#include "Chess.h"

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

void move_to_string(const BoardState *state, const Move* move, string& str);
bool parse_move_string(const BoardState& state, const string str, const Move* &move);
void print_board(BoardState& state);
