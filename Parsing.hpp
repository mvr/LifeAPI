#pragma once

#include <sstream>

#include "LifeAPI.hpp"

// TODO: Could be specialised to use bit operations to get runs instead of doing one cell at a time
std::string GenericRLE(auto&& cellchar, bool flushtrailing = false) {
  std::stringstream result;

  unsigned eol_count = 0;

  for (unsigned j = 0; j < 64; j++) {
    char last_val = cellchar(torus_wrap(0 + (N / 2)), torus_wrap(j + 32));
    unsigned run_count = 0;

    for (unsigned i = 0; i < N; i++) {
      char val = cellchar(torus_wrap(i + (N / 2)), torus_wrap(j + 32));

      // Flush linefeeds if we find a live cell
      if (val != '.' && val != 'b' && eol_count > 0) {
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

    // Flush run of live cells at end of line
    if (last_val != '.' && last_val != 'b') {
      if (run_count > 1)
        result << run_count;

      result << last_val;

      run_count = 0;
    }

    eol_count++;
  }

  // Flush trailing linefeeds
  if (flushtrailing && eol_count > 0) {
    if (eol_count > 1)
      result << eol_count;

    result << "$";
  }

  result << "!";

  return result.str();
}

inline std::string RowRLE(std::vector<LifeState> &row, bool flushtrailing = false) {
  const unsigned spacing = 84;

  std::stringstream result;

  bool last_val;
  unsigned run_count = 0;
  unsigned eol_count = 0;
  for (unsigned j = 0; j < spacing; j++) {
    if(j < 64)
      last_val = row[0].GetSafe(0 - N/2, j - 32);
    else
      last_val = false;
    run_count = 0;

    for (auto &pat : row) {
      for (unsigned i = 0; i < spacing; i++) {
        bool val = false;
        if(i < N && j < 64)
          val = pat.GetSafe(i - N/2, j - 32);

        // Flush linefeeds if we find a live cell
        if (val && eol_count > 0) {
          if (eol_count > 1)
            result << eol_count;

          result << "$";

          eol_count = 0;
        }

        // Flush current run if val changes
        if (val == !last_val) {
          if (run_count > 1)
            result << run_count;

          if (last_val == 1)
            result << "o";
          else
            result << "b";

          run_count = 0;
        }

        run_count++;
        last_val = val;
      }
    }
    // Flush run of live cells at end of line
    if (last_val) {
      if (run_count > 1)
        result << run_count;

      result << "o";

      run_count = 0;
    }

    eol_count++;
  }

  // Flush trailing linefeeds
  if (flushtrailing && eol_count > 0) {
    if (eol_count > 1)
      result << eol_count;

    result << "$";

    eol_count = 0;
  }

  result << "!";

  return result.str();
}


template<typename T> T GenericParse(const std::string &rle, auto&& interpretcell) {
  std::string noheader;
  std::istringstream iss(rle);

  for (std::string line; std::getline(iss, line); ) {
    if(line[0] != 'x')
      noheader += line;
  }

  T result;

  int cnt = 0;
  int x = 0;
  int y = 0;

  for (char const ch : noheader) {
    if (ch >= '0' && ch <= '9') {
      cnt *= 10;
      cnt += (ch - '0');
    } else if (ch == '$') {
      if (cnt == 0)
        cnt = 1;

      if (cnt == 129)
        // TODO: error
        return result;

      y += cnt;
      x = 0;
      cnt = 0;
    } else if (ch == '!') {
      break;
    } else if (ch == '\r' || ch == '\n' || ch == ' ') {
      continue;
    } else {
      if (cnt == 0)
        cnt = 1;

      for (int j = 0; j < cnt; j++) {
        interpretcell(result, ch, x, y);
        x++;
      }

      cnt = 0;
    }
  }
  return result;
}

LifeState LifeState::Parse(const std::string &rle) {
  return GenericParse<LifeState>(rle, [&](LifeState &result, char ch, int x, int y) -> void {
    if (ch == 'o') {
      result.Set(x, y);
    }
  });
}

std::string LifeState::RLE() const {
  return GenericRLE([&](int x, int y) -> char {
    return Get(x, y) ? 'o' : 'b';
  });
}
