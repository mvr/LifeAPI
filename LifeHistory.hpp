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

  inline std::string RLE() const;
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

  static inline LifeHistory Parse(const std::string &s);
  static inline LifeHistory ParseBellman(const std::string &s);

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
    case 'o':
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

inline char LifeHistoryCharAt(const LifeHistory& hist, int x, int y) {
  unsigned mask = hist.state.GetSafe(x, y)
                + (hist.history.GetSafe(x, y) << 1)
                + (hist.marked.GetSafe(x, y) << 2)
                + (hist.original.GetSafe(x, y) << 3);
  return LifeHistory::StateToChar(mask);
}

inline std::string RowRLE(const std::vector<LifeHistory> &row, bool flushtrailing = false, unsigned rowgap = 0) {
  const unsigned spacing = 84;

  std::stringstream result;

  unsigned eol_count = 0;
  for (unsigned j = 0; j < spacing; j++) {
    char last_val = '.';
    if (j < 64 && !row.empty())
      last_val = LifeHistoryCharAt(row[0], 0 - N/2, j - 32);

    unsigned run_count = 0;

    for (const auto &pat : row) {
      for (unsigned i = 0; i < spacing; i++) {
        char val = '.';
        if (i < N && j < 64)
          val = LifeHistoryCharAt(pat, i - N/2, j - 32);

        // Flush linefeeds if we find a non-empty cell
        if (val != '.' && eol_count > 0) {
          if (eol_count > 1)
            result << eol_count;

          result << "$";

          eol_count = 0;
        }

        // Flush current run if val changes
        if (val != last_val) {
          if (run_count > 1)
            result << run_count;

          result << last_val;

          run_count = 0;
        }

        run_count++;
        last_val = val;
      }
    }

    // Flush run of non-empty cells at end of line
    if (last_val != '.') {
      if (run_count > 1)
        result << run_count;

      result << last_val;
    }

    eol_count++;
  }

  // Flush trailing linefeeds
  if (flushtrailing && eol_count > 0) {
    if (eol_count > 1)
      result << eol_count + rowgap;

    result << "$";
  } else {
    result << "!";
  }

  return result.str();
}
