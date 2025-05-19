#pragma once

#include "LifeAPI.hpp"
#include "Parsing.hpp"

// This uses the same convention as lifelib's "layers" which do not
// match Golly's state names, so the parsing has to adjust for this.
struct LifeHistory {
  LifeState state;
  LifeState history;
  LifeState marked;
  LifeState original;

  LifeHistory() = default;
  LifeHistory(const LifeState &state, const LifeState &history,
                   const LifeState &marked, const LifeState &original)
      : state{state}, history{history}, marked{marked}, original{original} {};
  LifeHistory(const LifeState &state, const LifeState &history, const LifeState &marked)
      : state{state}, history{history}, marked{marked}, original{LifeState()} {};
  LifeHistory(const LifeState &state, const LifeState &history)
      : state{state}, history{history}, marked{LifeState()},
        original{LifeState()} {};

  std::string RLE() const;
  std::string RLEWHeader() const {
    return "x = 0, y = 0, rule = LifeHistory\n" + RLE();
  }
  friend std::ostream& operator<<(std::ostream& os, LifeHistory const& self) {
    return os << self.RLEWHeader();
  }

  static char StateToChar(unsigned mask) {
    switch (mask) {
    case 0b0000: return '.';
    case 0b0001: return 'A';
    case 0b0010: return 'B';
    case 0b0101: return 'C';
    case 0b0100: return 'D';
    case 0b1001: return 'E';
    default:     return 'F';
    }
  }

  static LifeHistory Parse(const std::string &s);
  static LifeHistory ParseBellman(const std::string &s);

  void Move(int x, int y) {
    state.Move(x, y);
    history.Move(x, y);
    marked.Move(x, y);
    original.Move(x, y);
  }

  void Move(std::pair<int, int> vec) { Move(vec.first, vec.second); }

  void AlignWith(const LifeState &other) {
    auto offset = state.Match(other).FirstOn();
    Move(-offset.first, -offset.second);
  }
};

std::string LifeHistory::RLE() const {
  return GenericRLE([&](int x, int y) -> char {
    unsigned val = state.Get(x, y) + (history.Get(x, y) << 1) + (marked.Get(x, y) << 2) + (original.Get(x, y) << 3);

    return StateToChar(val);
  });
}

LifeHistory LifeHistory::Parse(const std::string &rle) {
  return GenericParse<LifeHistory>(rle, [&](LifeHistory &result, char ch, int x, int y) -> void {
    switch(ch) {
    case 'A':
      result.state.Set(x, y);
      break;
    case 'B':
      result.history.Set(x, y);
      break;
    case 'C':
      result.state.Set(x, y);
      result.marked.Set(x, y);
      break;
    case 'D':
      result.marked.Set(x, y);
      break;
    case 'E':
      result.state.Set(x, y);
      result.original.Set(x, y);
      break;
    }
  });
}

LifeHistory LifeHistory::ParseBellman(const std::string &rle) {
  return GenericParse<LifeHistory>(rle, [&](LifeHistory &result, char ch, int x, int y) -> void {
    switch(ch) {
    case 'C':
      result.state.Set(x, y);
      break;
    case 'E':
      result.history.Set(x, y);
      break;
    }
  });
}
