#pragma once

#include <array>
#include <bit>
#include <random>
#include <vector>

#define XXH_INLINE_ALL 1
#include "xxHash/xxhash.h"

#include "Bits.hpp"

const int N = 64;

constexpr unsigned torus_wrap(int x) {
  return x & (N - 1); // Valid for negative x
}

namespace PRNG {
static std::random_device rd;
static std::mt19937_64 e2(rd());
static std::uniform_int_distribution<uint64_t>
    dist(std::llround(std::pow(2, 61)), std::llround(std::pow(2, 62)));
} // namespace PRNG

enum struct SymmetryTransform : uint32_t;
enum struct StaticSymmetry : uint32_t;

// See LifeTarget.hpp
struct LifeTarget;

// See LifeStrip.hpp
struct StripIndex;
struct LifeStateStrip;
struct LifeStateStripProxy;
struct LifeStateStripConstProxy;

enum class InitializedTag { UNINITIALIZED };

struct __attribute__((aligned(64))) LifeState {
  uint64_t state[N];

  ////////////////////////////////
  // Constructors
  ////////////////////////////////

  constexpr LifeState() : state{0} {}
  explicit constexpr LifeState(__attribute__((unused)) InitializedTag) {}

  LifeState(const LifeState &) = default;
  LifeState &operator=(const LifeState &) = default;
  LifeState(LifeState&& other) noexcept = default;
  LifeState& operator=(LifeState&& other) noexcept = default;

  // Avoid accidentally coercing from a bool
  LifeState &operator=(bool) = delete;

  static constexpr LifeState Cell(std::pair<int, int> cell) {
    LifeState result;
    result.Set(cell.first, cell.second);
    return result;
  }

  static LifeState RandomState() {
    LifeState result;
    for (unsigned i = 0; i < N; i++)
      result[i] = PRNG::dist(PRNG::e2);

    return result;
  }

  // State is parity of (x + y), so (0, 0) is OFF
  static LifeState Checkerboard() {
    // mvrnote: TODO, just ConstantParse it
    LifeState checkerboard;
    for (int i = 0; i < N; i++) {
      if (i % 2 == 0)
        checkerboard.state[i] = 0xAAAAAAAAAAAAAAAAULL;
      else
        checkerboard.state[i] = std::rotl(0xAAAAAAAAAAAAAAAAULL, 1);
    }
    return checkerboard;
  }

  static constexpr LifeState SolidRect(int x, int y, int w, int h) {
    uint64_t column;
    if (h < 64)
      column = std::rotl(((uint64_t)1 << h) - 1, y);
    else
      column = ~0ULL;

    unsigned start, end;
    if (w < N) {
      start = torus_wrap(x);
      end = torus_wrap(x + w);
    } else {
      start = 0;
      end = N;
    }

    LifeState result;
    if (end > start) {
      for (unsigned int i = start; i < end; i++)
        result[i] = column;
    } else {
      for (unsigned int i = 0; i < end; i++)
        result[i] = column;
      for (unsigned int i = start; i < N; i++)
        result[i] = column;
    }
    return result;
  }

  static constexpr LifeState SolidRectXY(int x1, int y1, int x2, int y2) {
    return SolidRect(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
  }

  static constexpr LifeState NZOIAround(std::pair<int, int> cell, unsigned distance) {
    unsigned size = 2 * distance + 1;
    return LifeState::SolidRect(cell.first - distance, cell.second - distance,
                                size, size);
  }

  static constexpr LifeState CellZOI(std::pair<int, int> cell) {
    return LifeState::NZOIAround(cell, 1);
  }

  ////////////////////////////////
  // Getting and Setting
  ////////////////////////////////

  void Set(unsigned x, unsigned y) { state[x] |= (1ULL << y); }
  void Erase(unsigned x, unsigned y) { state[x] &= ~(1ULL << y); }
  void Set(unsigned x, unsigned y, bool val) { if(val) Set(x, y); else Erase(x, y); }
  bool Get(unsigned x, unsigned y) const { return (state[x] & (1ULL << y)) != 0; }
  void SetSafe(int x, int y, bool val) { Set(torus_wrap(x), torus_wrap(y), val); }
  bool GetSafe(int x, int y) const { return Get(torus_wrap(x), torus_wrap(y)); }

  void Set(std::pair<int, int> cell) { Set(cell.first, cell.second); };
  void Erase(std::pair<int, int> cell) { Erase(cell.first, cell.second); };
  void Set(std::pair<int, int> cell, bool val) { Set(cell.first, cell.second, val); };
  bool Get(std::pair<int, int> cell) const { return Get(cell.first, cell.second); };
  void SetSafe(std::pair<int, int> cell, bool val) { SetSafe(cell.first, cell.second, val); };
  bool GetSafe(std::pair<int, int> cell) const { return GetSafe(cell.first, cell.second); };

  constexpr uint64_t &operator[](const unsigned i) { return state[i]; }
  constexpr uint64_t operator[](const unsigned i) const { return state[i]; }

  template <unsigned width>
  std::array<uint64_t, width> GetStrip(unsigned column) const {
    std::array<uint64_t, width> result;
    const unsigned offset = (width - 1) / 2; // 0, 0, 1, 1, 2, 2

    if (offset <= column && column + width - 1 - offset < N) {
      for (unsigned i = 0; i < width; i++) {
        const unsigned c = column + i - offset;
        result[i] = state[c];
      }
    } else {
      for (unsigned i = 0; i < width; i++) {
        const unsigned c = torus_wrap(column + i + N - offset);
        result[i] = state[c];
      }
    }
    return result;
  }

  template <unsigned width>
  void SetStrip(unsigned column, std::array<uint64_t, width> value) {
    const unsigned offset = (width - 1) / 2; // 0, 0, 1, 1, 2, 2
    for (unsigned i = 0; i < width; i++) {
      const unsigned c = torus_wrap(column + i + N - offset);
      state[c] = value[i];
    }
  }

  inline LifeStateStripProxy operator[](const StripIndex column);
  inline const LifeStateStripConstProxy operator[](const StripIndex column) const;

  template <unsigned radius> uint64_t GetPatch(std::pair<int, int> cell) const {
    auto [x, y] = cell;

    unsigned diameter = 2 * radius + 1;

    uint64_t result = 0;

    for (unsigned i = 0; i < diameter; i++) {
      const unsigned c = torus_wrap(x + i + N - radius);
      uint64_t bits = std::rotr(state[c], y - radius) & ((1ULL << diameter) - 1);
      result |= std::rotl(bits, i * diameter);
    }

    return result;
  }

  template <unsigned radius>
  void SetPatch(std::pair<int, int> cell, uint64_t value) {
    auto [x, y] = cell;

    unsigned diameter = 2 * radius + 1;

    for (unsigned i = 0; i < diameter; i++) {
      const unsigned c = torus_wrap(x + i + N - radius);
      uint64_t bits = std::rotr(value, i * diameter) & ((1ULL << diameter) - 1);
      state[c] &= ~std::rotl((1ULL << diameter) - 1, y - radius);
      state[c] |= std::rotl(bits, y - radius);
    }
  }

  ////////////////////////////////
  // Operators
  ////////////////////////////////

  bool operator==(const LifeState &b) const {
    uint64_t diffs = 0;

    for (unsigned i = 0; i < N; i++)
      diffs |= state[i] ^ b[i];

    return diffs == 0;
  }

  bool operator!=(const LifeState &b) const { return !(*this == b); }

  LifeState operator~() const {
    LifeState result(InitializedTag::UNINITIALIZED);
    for (unsigned i = 0; i < N; i++) {
      result[i] = ~state[i];
    }
    return result;
  }

  LifeState operator&(const LifeState &other) const {
    LifeState result(InitializedTag::UNINITIALIZED);
    for (unsigned i = 0; i < N; i++) {
      result[i] = state[i] & other[i];
    }
    return result;
  }

  LifeState &operator&=(const LifeState &other) {
    for (unsigned i = 0; i < N; i++) {
      state[i] = state[i] & other[i];
    }
    return *this;
  }

  LifeState operator|(const LifeState &other) const {
    LifeState result(InitializedTag::UNINITIALIZED);
    for (unsigned i = 0; i < N; i++) {
      result[i] = state[i] | other[i];
    }
    return result;
  }

  LifeState &operator|=(const LifeState &other) {
    for (unsigned i = 0; i < N; i++) {
      state[i] = state[i] | other[i];
    }
    return *this;
  }

  LifeState operator^(const LifeState &other) const {
    LifeState result(InitializedTag::UNINITIALIZED);
    for (unsigned i = 0; i < N; i++) {
      result[i] = state[i] ^ other[i];
    }
    return result;
  }

  LifeState &operator^=(const LifeState &other) {
    for (unsigned i = 0; i < N; i++) {
      state[i] = state[i] ^ other[i];
    }
    return *this;
  }

  ////////////////////////////////
  // Queries
  ////////////////////////////////

  bool IsEmpty() const {
    uint64_t all = 0;
    for (unsigned i = 0; i < N; i++) {
      all |= state[i];
    }

    return all == 0;
  }

  unsigned GetPop() const {
    unsigned pop = 0;

    for (unsigned i = 0; i < N; i++) {
      pop += std::popcount(state[i]);
    }

    return pop;
  }

  // Get an ON cell as fast as possible. I make no guarantees about which cell it will be
  std::pair<int, int> FirstOn() const {
    for (int x = N - 4; x >= 0; x -= 4) {
      if ((state[x] | state[x + 1] | state[x + 2] | state[x + 3]) == 0ULL) {
        continue;
      }

      if (state[x] != 0ULL) {
        return std::make_pair(x, std::countr_zero(state[x]));
      } else if (state[x + 1] != 0ULL) {
        return std::make_pair(x + 1, std::countr_zero(state[x + 1]));
      } else if (state[x + 2] != 0ULL) {
        return std::make_pair(x + 2, std::countr_zero(state[x + 2]));
      } else {
        return std::make_pair(x + 3, std::countr_zero(state[x + 3]));
      }
    }

    return std::make_pair(-1, -1);
  }

  // std::pair<int, int> FirstOnWrapped() const {
  //   unsigned foundq = N;
  //   for (unsigned x = 0; x < N/2; x += 4) {
  //     if ((state[x] | state[x + 1] | state[x + 2] | state[x + 3]) != 0ULL) {
  //       foundq = x;
  //     }
  //   }
  //   if (foundq == N) {
  //   for (unsigned x = N/2; x < N; x += 4) {
  //     if ((state[x] | state[x + 1] | state[x + 2] | state[x + 3]) != 0ULL) {
  //       foundq = x;
  //     }
  //   }
  //   }
  //   if (foundq == N) {
  //     return std::make_pair(-1, -1);
  //   }

  //   if (state[foundq] != 0ULL) {
  //     return std::make_pair(foundq, (std::countr_zero(std::rotr(state[foundq], 32)) + 32) % 64);
  //   } else if (state[foundq + 1] != 0ULL) {
  //     return std::make_pair(foundq + 1, (std::countr_zero(std::rotr(state[foundq + 1], 32)) + 32) % 64);
  //   } else if (state[foundq + 2] != 0ULL) {
  //     return std::make_pair(foundq + 2, (std::countr_zero(std::rotr(state[foundq + 2], 32)) + 32) % 64);
  //   } else if (state[foundq + 3] != 0ULL) {
  //     return std::make_pair(foundq + 3, (std::countr_zero(std::rotr(state[foundq + 3], 32)) + 32) % 64);
  //   } else {
  //     return std::make_pair(-1, -1);
  //   }
  // }

  inline std::vector<std::pair<int, int>> OnCells() const;

  LifeState FirstCell() const { return LifeState::Cell(FirstOn()); }

  std::pair<int, int> FindSetNeighbour(std::pair<int, int> cell) const {
    // This could obviously be done faster by extracting the result
    // directly from the columns, but this is probably good enough for now
    const std::array<std::pair<int, int>, 9> directions = {std::make_pair(0, 0), {-1, 0}, {1, 0}, {0,1}, {0, -1}, {-1,-1}, {-1,1}, {1, -1}, {1, 1}};
    for (auto d : directions) {
      int x = torus_wrap(cell.first + d.first);
      int y = torus_wrap(cell.second + d.second);
      if (Get(x, y))
        return std::make_pair(x, y);
    }
    return std::make_pair(-1, -1);
  }

  uint64_t GetHash() const { return XXH3_64bits(state, N * sizeof(uint64_t)); }

  inline uint64_t GetOctoHash() const;

  inline bool AreDisjoint(const LifeState &pat) const {
    uint64_t differences = 0;
    #pragma clang loop vectorize(enable)
    for (unsigned i = 0; i < N; i++) {
      uint64_t difference = (~state[i] & pat[i]) ^ (pat[i]);
      differences |= difference;
    }

    return differences == 0;
  }

  inline bool Contains(const LifeState &pat) const {
    uint64_t differences = 0;
    #pragma clang loop vectorize(enable)
    for (unsigned i = 0; i < N; i++) {
      uint64_t difference = (state[i] & pat[i]) ^ (pat[i]);
      differences |= difference;
    }

    return differences == 0;
  }

  bool Contains(const LifeState &pat, int targetDx, int targetDy) const {
    int dy = torus_wrap(targetDy);

    for (unsigned i = 0; i < N; i++) {
      int curX = torus_wrap(i + targetDx);

      if ((std::rotr(state[curX], dy) & pat[i]) != (pat[i]))
        return false;
    }
    return true;
  }

  bool AreDisjoint(const LifeState &pat, int targetDx, int targetDy) const {
    int dy = torus_wrap(targetDy);

    for (unsigned i = 0; i < N; i++) {
      int curX = torus_wrap(i + targetDx);

      if (((~std::rotr(state[curX], dy)) & pat[i]) != pat[i])
        return false;
    }

    return true;
  }

  inline bool Contains(const LifeTarget &target, int dx, int dy) const;
  inline bool Contains(const LifeTarget &target) const;

  LifeState MatchLive(const LifeState &live) const {
    LifeState invThis = ~*this;
    return ~live.Mirrored().Convolve(invThis);
  }

  LifeState MatchLiveAndDead(const LifeState &live, const LifeState &dead) const {
    LifeState invThis = ~*this;
    LifeState liveMatches = ~live.Mirrored().Convolve(invThis);

    if (liveMatches.IsEmpty())
      return LifeState();
    return liveMatches & ~Convolve(dead.Mirrored());
  }

  LifeState MatchesLiveAndDeadSym(const LifeState &live,
                                  const LifeState &dead) const;

  LifeState Match(const LifeState &live) const {
    return MatchLiveAndDead(live, live.GetBoundary());
  }

  LifeState Match(const LifeTarget &target) const;

  std::array<int, 4> XYBounds() const {
    int leftMargin;
    int rightMargin;

    if constexpr (N == 64) {
      uint64_t popCols = PopulatedColumns();
      popCols = std::rotr(popCols, 32);
      leftMargin = std::countr_zero(popCols);
      rightMargin = std::countl_zero(popCols);
    }

    if constexpr (N == 32) {
      uint32_t popCols = PopulatedColumns();
      popCols = std::rotr(popCols, 16);
      leftMargin = std::countr_zero(popCols);
      rightMargin = std::countl_zero(popCols);
    }

    uint64_t orOfCols = 0;
    for (unsigned i = 0; i < N; ++i)
      orOfCols |= state[i];

    if (orOfCols == 0ULL) {
      return std::array<int, 4>({-1, -1, -1, -1});
    }

    orOfCols = std::rotr(orOfCols, 32);
    int topMargin = std::countr_zero(orOfCols);
    int bottomMargin = std::countl_zero(orOfCols);

    if constexpr (N == 64) {
      return std::array<int, 4>({leftMargin - 32, topMargin - 32,
                                 31 - rightMargin, 31 - bottomMargin});
    }
    if constexpr (N == 32) {
      return std::array<int, 4>({leftMargin - 16, topMargin - 32,
                                 15 - rightMargin, 31 - bottomMargin});
    }
  }

  uint64_t PopulatedColumns() const {
    uint64_t result = 0;
    for (unsigned i = 0; i < N; i++)
      if (state[i] != 0)
        result |= 1ULL << i;
    return result;
  }

  std::pair<int, int> WidthHeight() const {
    uint64_t orOfCols = 0;
    for (unsigned i = 0; i < N; ++i)
      orOfCols |= state[i];

    if (orOfCols == 0ULL) // empty grid.
      return std::make_pair(0, 0);

    uint64_t cols = PopulatedColumns();
    unsigned width;
    if constexpr (N == 64) {
      width = populated_width_uint64_t(cols);
    }

    if constexpr (N == 32) {
      width = populated_width_uint32_t((uint32_t)cols);
    }

    unsigned height = populated_width_uint64_t(orOfCols);

    return {width, height};
  }

  ////////////////////////////////
  // ZOI
  ////////////////////////////////

  LifeState ZOI() const {
    LifeState temp(InitializedTag::UNINITIALIZED);
    for (unsigned i = 0; i < N; i++) {
      uint64_t col = state[i];
      temp[i] = col | std::rotl(col, 1) | std::rotr(col, 1);
    }

    LifeState boundary(InitializedTag::UNINITIALIZED);

    boundary[0] = temp[N - 1] | temp[0] | temp[1];
    for (int i = 1; i < N - 1; i++)
      boundary[i] = temp[i - 1] | temp[i] | temp[i + 1];
    boundary[N - 1] = temp[N - 2] | temp[N - 1] | temp[0];

    return boundary;
  }

  LifeState GetBoundary() const { return ZOI() & ~*this; }

  // Convolve with 3o$obo$3o!
  LifeState ZOIHollow() const {
    LifeState temp(InitializedTag::UNINITIALIZED);

    for (unsigned i = 0; i < N; i++) {
      uint64_t col = state[i];
      temp[i] = col | std::rotl(col, 1) | std::rotr(col, 1);
    }

    LifeState tempmid(InitializedTag::UNINITIALIZED);
    for (unsigned i = 0; i < N; i++) {
      uint64_t col = state[i];
      tempmid[i] = std::rotl(col, 1) | std::rotr(col, 1);
    }

    LifeState boundary(InitializedTag::UNINITIALIZED);
    boundary[0] = temp[N - 1] | tempmid[0] | temp[1];
    for (int i = 1; i < N - 1; i++)
      boundary[i] = temp[i - 1] | tempmid[i] | temp[i + 1];
    boundary[N - 1] = temp[N - 2] | tempmid[N - 1] | temp[0];

    return boundary;
  }

  LifeState BigZOI() const {
    LifeState b(InitializedTag::UNINITIALIZED);
    b[0] = state[0] | std::rotl(state[0], 1) | std::rotr(state[0], 1) |
           state[N - 1] | state[0 + 1];
    for (unsigned i = 1; i < N - 1; i++) {
      b[i] = state[i] | std::rotl(state[i], 1) | std::rotr(state[i], 1) |
             state[i - 1] | state[i + 1];
    }
    b[N - 1] = state[N - 1] | std::rotl(state[N - 1], 1) |
               std::rotr(state[N - 1], 1) | state[N - 1 - 1] | state[0];

    LifeState c(InitializedTag::UNINITIALIZED);
    c[0] = b[0] | b[N - 1] | b[0 + 1];
    for (unsigned i = 1; i < N - 1; i++) {
      c[i] = b[i] | b[i - 1] | b[i + 1];
    }
    c[N - 1] = b[N - 1] | b[N - 1 - 1] | b[0];

    LifeState zoi(InitializedTag::UNINITIALIZED);

    zoi[0] = c[0] | std::rotl(c[0], 1) | std::rotr(c[0], 1);
    for (unsigned i = 1; i < N - 1; i++) {
      zoi[i] = c[i] | std::rotl(c[i], 1) | std::rotr(c[i], 1);
    }
    zoi[N - 1] = c[N - 1] | std::rotl(c[N - 1], 1) | std::rotr(c[N - 1], 1);

    return zoi;
  }

  uint64_t ZOIColumn(int i) const {
    uint64_t col = state[torus_wrap(i - 1)] | state[i] | state[torus_wrap(i + 1)];
    return std::rotl(col, 1) | col | std::rotr(col, 1);
  }

/*   static LifeState ColumnZOI(int i, uint64_t col) {
    LifeState result;
    col = col | std::rotl(col, 1) | std::rotr(col, 1);
    result[(i - 1 + N) % N] = col;
    result[i] = col;
    result[(i + 1) % N] = col;
    return result;
  } */

  LifeState NZOI(unsigned distance) const {
    return Convolve(LifeState::NZOIAround({0, 0}, distance));
  }

  LifeState BufferAround(std::pair<int, int> size) const {
    auto bounds = XYBounds();

    if (bounds[0] == -1 &&
        bounds[1] == -1 &&
        bounds[2] == -1 &&
        bounds[3] == -1)
      return ~LifeState();

    int width = bounds[2] - bounds[0] + 1;
    int height = bounds[3] - bounds[1] + 1;

    int remainingwidth = size.first - width;
    int remainingheight = size.second - height;

    if (remainingwidth < 0 || remainingheight < 0)
      return LifeState();
    else
      return LifeState::SolidRectXY(bounds[0] - remainingwidth,
                                    bounds[1] - remainingheight,
                                    bounds[2] + remainingwidth,
                                    bounds[3] + remainingheight);
  }

  LifeState MooreZOI() const {
    LifeState temp(InitializedTag::UNINITIALIZED);
    LifeState boundary(InitializedTag::UNINITIALIZED);
    for (unsigned i = 0; i < N; i++) {
      uint64_t col = state[i];
      temp[i] = col | std::rotl(col, 1) | std::rotr(col, 1);
    }

    boundary[0] = state[N - 1] | temp[0] | state[1];

    for (unsigned i = 1; i < N - 1; i++)
      boundary[i] = state[i - 1] | temp[i] | state[i + 1];

    boundary[N - 1] = state[N - 2] | temp[N - 1] | state[0];

    return boundary;
  }

  inline LifeState Convolve(const LifeState &other) const;

  LifeState ComponentContaining(const LifeState &seed, const LifeState &corona) const {
    LifeState result;
    LifeState tocheck = seed;
    while (!tocheck.IsEmpty()) {
      LifeState neighbours = tocheck.Convolve(corona) & *this;
      tocheck = neighbours & ~result;
      result |= neighbours;
    }

    return result;
  }

  std::vector<LifeState> Components(const LifeState &corona) const {
    std::vector<LifeState> result;
    LifeState remaining = *this;
    while (!remaining.IsEmpty()) {
      LifeState component = remaining.ComponentContaining(remaining.FirstCell(), corona);
      result.push_back(component);
      remaining &= ~component;
    }
    return result;
  }

  std::vector<LifeState> StillComponents() const {
    LifeState out1, out2, outMore;
    InteractionCounts(out1, out2, outMore);
    const LifeState stillzoi = LifeState::ConstantParse("2bo$b3o$5o$b3o$2bo!", -2, -2);
    auto cs = (*this | outMore).Components(stillzoi);
    for(auto &c : cs){
      c &= *this;
    }
    return cs;
  }

  LifeState StillComponentContaining(const LifeState &seed) const {
    LifeState out1, out2, outMore;
    InteractionCounts(out1, out2, outMore);
    const LifeState stillzoi = LifeState::ConstantParse("2bo$b3o$5o$b3o$2bo!", -2, -2);
    auto c = (*this | outMore).ComponentContaining(seed, stillzoi);
    return c & *this;
  }

private:
  bool IsPseudoStillLifeHelper(const std::vector<LifeState>& components, bool topLevel) const {
    if (components.empty()) return true;

    if (!topLevel) {
      LifeState combined;
      for (auto &c : components) {
        combined |= c;
      }
      if (combined == combined.Stepped())
        return true;
    }
    
    // Try all non-empty proper subsets (2^n - 2 possibilities)
    int n = components.size();
    for (int mask = 1; mask < (1 << n) - 1; mask++) {
      LifeState group1, group2;
      std::vector<LifeState> group1Components, group2Components;
      
      // Split components based on mask
      for (int i = 0; i < n; i++) {
        if (mask & (1 << i)) {
          group1 |= components[i];
          group1Components.push_back(components[i]);
        } else {
          group2 |= components[i];
          group2Components.push_back(components[i]);
        }
      }
      
      if (IsPseudoStillLifeHelper(group1Components, false) &&
          IsPseudoStillLifeHelper(group2Components, false)) {
        return true;
      }
    }
    
    return false;
  }

public:
  bool IsPseudoStillLife() const {
    auto components = Components(LifeState::ConstantParse("3o$3o$3o!", -1, -1));
    return IsPseudoStillLifeHelper(components, true);
  }

  ////////////////////////////////
  // Transforms
  ////////////////////////////////

  void Move(int x, int y) {
    uint64_t temp[N];
    x = torus_wrap(x);
    const int shift = static_cast<int>(static_cast<unsigned>(y) & 63u);
    const unsigned split = N - x;

    if (x == 0 && shift == 0)
      return;

    if (x == 0) {
      for (unsigned i = 0; i < N; i++) {
        state[i] = std::rotl(state[i], shift);
      }
      return;
    }

    if (shift == 0) {
      for (unsigned i = 0; i < split; i++) {
        temp[i + x] = state[i];
      }

      for (unsigned i = split; i < N; i++) {
        temp[i - split] = state[i];
      }

      for (unsigned i = 0; i < N; i++) {
        state[i] = temp[i];
      }
      return;
    }

    for (unsigned i = 0; i < split; i++) {
      temp[i + x] = std::rotl(state[i], shift);
    }

    for (unsigned i = split; i < N; i++) {
      temp[i - split] = std::rotl(state[i], shift);
    }

    for (unsigned i = 0; i < N; i++) {
      state[i] = temp[i];
    }
  }
  void Move(std::pair<int, int> vec) { Move(vec.first, vec.second); }

  constexpr LifeState Moved(int x, int y) const {
    x = torus_wrap(x);
    const int shift = static_cast<int>(static_cast<unsigned>(y) & 63u);
    const unsigned split = N - x;

    if (x == 0 && shift == 0)
      return *this;

    LifeState result(InitializedTag::UNINITIALIZED);

    if (x == 0) {
      for (unsigned i = 0; i < N; i++) {
        result[i] = std::rotl(state[i], shift);
      }
      return result;
    }

    if (shift == 0) {
      for (unsigned i = 0; i < split; i++) {
        result[i + x] = state[i];
      }

      for (unsigned i = split; i < N; i++) {
        result[i - split] = state[i];
      }
      return result;
    }

    for (unsigned i = 0; i < split; i++) {
      result[i + x] = std::rotl(state[i], shift);
    }

    for (unsigned i = split; i < N; i++) {
      result[i - split] = std::rotl(state[i], shift);
    }

    return result;
  }

  // constexpr LifeState Moved(int x, int y) const {
  //   LifeState result(InitializedTag::UNINITIALIZED);

  //   if (x < 0)
  //     x += N;
  //   if (y < 0)
  //     y += 64;

  //   for (unsigned i = 0; i < N; i++) {
  //     int newi = (i + x) % N;
  //     result[newi] = std::rotl(state[i], y);
  //   }
  //   return result;
  // }

  constexpr LifeState Moved(std::pair<int, int> vec) const {
    return Moved(vec.first, vec.second);
  }

  void AlignWith(const LifeState &other) {
    auto offset = Match(other).FirstOn();
    Move(-offset.first, -offset.second);
  }

  void Reverse(unsigned idxS, unsigned idxE) {
    for (unsigned i = 0; idxS + 2 * i < idxE; i++) {
      int l = idxS + i;
      int r = idxE - i;

      uint64_t temp = state[l];
      state[l] = state[r];
      state[r] = temp;
    }
  }

  void FlipY() { // even reflection across y-axis, ie (0,0) maps to (-1, 0)
    Reverse(0, N - 1);
  }

  void BitReverse() {
    for (unsigned i = 0; i < N; i++) {
      state[i] = __builtin_bitreverse64(state[i]);
    }
  }
  // even reflection across x-axis, ie (0,0) maps to (0, -1)
  void FlipX() { BitReverse(); }

  void Transpose(bool whichDiagonal) {
    int j, k;
    uint64_t m, t;

    for (j = N / 2, m = (~0ULL) >> (N / 2); j; j >>= 1, m ^= m << j) {
      for (k = 0; k < N; k = ((k | j) + 1) & ~j) {
        if (whichDiagonal) {
          t = (state[k] ^ (state[k | j] >> j)) & m;
          state[k] ^= t;
          state[k | j] ^= (t << j);
        } else {
          t = (state[k] >> j ^ (state[k | j])) & m;
          state[k] ^= (t << j);
          state[k | j] ^= t;
        }
      }
    }
  }

  void Transpose() { Transpose(true); }

  inline void Transform(SymmetryTransform transf);

  LifeState Mirrored() const {
    LifeState result = *this;
    result.FlipX();
    result.FlipY();
    result.Move(1, 1);
    return result;
  }

  LifeState Transformed(SymmetryTransform transf) const {
    LifeState result = *this;
    result.Transform(transf);
    return result;
  }

  void Transform(int dx, int dy, SymmetryTransform transf) {
    Move(dx, dy);
    Transform(transf);
  }

  inline std::vector<LifeState> SymmetryOrbit() const;
  inline std::vector<SymmetryTransform> SymmetryOrbitRepresentatives() const;

  LifeState Halve() const;
  LifeState HalveX() const;
  LifeState HalveY() const;

  LifeState Skew() const;
  LifeState InvSkew() const;

  ////////////////////////////////
  // Stepping and Counting
  ////////////////////////////////

  static inline void HalfAdd(uint64_t &out0, uint64_t &out1, const uint64_t ina, const uint64_t inb) {
    out0 = ina ^ inb;
    out1 = ina & inb;
  }

  static inline void FullAdd(uint64_t &out0, uint64_t &out1, const uint64_t ina, const uint64_t inb, const uint64_t inc) {
    uint64_t halftotal = ina ^ inb;
    out0 = halftotal ^ inc;
    uint64_t halfcarry1 = ina & inb;
    uint64_t halfcarry2 = inc & halftotal;
    out1 = halfcarry1 | halfcarry2;
  }

  // From Page 5 of
  // https://www.gathering4gardner.org/g4g13gift/math/RokickiTomas-GiftExchange-LifeAlgorithms-G4G13.pdf
  uint64_t inline Rokicki(const uint64_t &a, const uint64_t &bU0,
                          const uint64_t &bU1, const uint64_t &bB0,
                          const uint64_t &bB1) {
    uint64_t aw = std::rotl(a, 1);
    uint64_t ae = std::rotr(a, 1);
    uint64_t s0 = aw ^ ae;
    uint64_t s1 = aw & ae;
    uint64_t ts0 = bB0 ^ bU0;
    uint64_t ts1 = (bB0 & bU0) | (ts0 & s0);
    return (bB1 ^ bU1 ^ ts1 ^ s1) & ((bB1 | bU1) ^ (ts1 | s1)) &
           ((ts0 ^ s0) | a);
  }

  static inline void HalfAdd(LifeState &outbit, LifeState &outcarry,
                             const LifeState &ina, const LifeState &inb) {
    outbit = ina ^ inb;
    outcarry = ina & inb;
  }

  static inline void FullAdd(LifeState &outbit, LifeState &outcarry,
                             const LifeState &ina, const LifeState &inb,
                             const LifeState &inc) {
    LifeState halftotal = ina ^ inb;
    outbit = halftotal ^ inc;
    LifeState halfcarry1 = ina & inb;
    LifeState halfcarry2 = inc & halftotal;
    outcarry = halfcarry1 | halfcarry2;
  }

  inline void Step();

  // mvrnote: This could be done without the copy
  inline LifeState Stepped() const {
    LifeState copy = *this;
    copy.Step();
    return copy;
  }
  
  inline void StepAlt();

  void Step(unsigned numIters) {
    for (unsigned i = 0; i < numIters; i++) {
      Step();
    }
  }
  inline LifeState Stepped(unsigned numIters) const {
    LifeState copy = *this;
    copy.Step(numIters);
    return copy;
  }


  bool StepFor(std::pair<int, int> cell) const {
    unsigned count = CountNeighbours(cell);
    if (Get(cell))
      return count == 2 || count == 3;
    else
      return count == 3;
  }

  inline void CountRows(LifeState &__restrict__ bit0,
                        LifeState &__restrict__ bit1) const {
    for (int i = 0; i < N; i++) {
      uint64_t a = state[i];
      uint64_t l = std::rotl(a, 1);
      uint64_t r = std::rotr(a, 1);

      bit0.state[i] = l ^ r ^ a;
      bit1.state[i] = ((l ^ r) & a) | (l & r);
    }
  }

  inline void CountNeighbourhood(LifeState &__restrict__ bit3,
                                 LifeState &__restrict__ bit2,
                                 LifeState &__restrict__ bit1,
                                 LifeState &__restrict__ bit0) const {
    LifeState col0(InitializedTag::UNINITIALIZED), col1(InitializedTag::UNINITIALIZED);
    CountRows(col0, col1);

    for (int i = 0; i < N; i++) {
      int idxU;
      int idxB;
      if (i == 0)
        idxU = N - 1;
      else
        idxU = i - 1;

      if (i == N - 1)
        idxB = 0;
      else
        idxB = i + 1;

      uint64_t u_on1 = col1.state[idxU];
      uint64_t u_on0 = col0.state[idxU];
      uint64_t c_on1 = col1.state[i];
      uint64_t c_on0 = col0.state[i];
      uint64_t l_on1 = col1.state[idxB];
      uint64_t l_on0 = col0.state[idxB];

      uint64_t on3, on2, on1, on0;
      uint64_t uc0, uc1, uc2, uc_carry0;
      HalfAdd(uc0, uc_carry0, u_on0, c_on0);
      FullAdd(uc1, uc2, u_on1, c_on1, uc_carry0);

      uint64_t on_carry1, on_carry0;
      HalfAdd(on0, on_carry0, uc0, l_on0);
      FullAdd(on1, on_carry1, uc1, l_on1, on_carry0);
      HalfAdd(on2, on3, uc2, on_carry1);

      bit3.state[i] = on3;
      bit2.state[i] = on2;
      bit1.state[i] = on1;
      bit0.state[i] = on0;
    }
  }

  // Count OFF cells with exactly 1 neighbour, exactly 2 neighbours, and all
  // others
  inline void InteractionCounts(LifeState &__restrict__ out1,
                                LifeState &__restrict__ out2,
                                LifeState &__restrict__ outMore) const {
    LifeState col0(InitializedTag::UNINITIALIZED), col1(InitializedTag::UNINITIALIZED);
    CountRows(col0, col1);

    for (int i = 0; i < N; i++) {
      int idxU;
      int idxB;
      if (i == 0)
        idxU = N - 1;
      else
        idxU = i - 1;

      if (i == N - 1)
        idxB = 0;
      else
        idxB = i + 1;

      uint64_t u_on1 = col1.state[idxU];
      uint64_t u_on0 = col0.state[idxU];
      uint64_t c_on1 = col1.state[i];
      uint64_t c_on0 = col0.state[i];
      uint64_t l_on1 = col1.state[idxB];
      uint64_t l_on0 = col0.state[idxB];

      uint64_t final_sum, final_carry;
      FullAdd(final_sum, final_carry, u_on0, c_on0, l_on0);

      uint64_t carry_sum, carry_carry;
      FullAdd(carry_sum, carry_carry, u_on1, c_on1, l_on1);

      out1[i] = ~state[i] & ~carry_carry & final_sum & ~carry_sum & ~final_carry;
      out2[i] = ~state[i] & ~carry_carry & ~final_sum & (carry_sum ^ final_carry);
      outMore[i] = ~state[i] & ~out2[i] & (final_carry | carry_sum | carry_carry);
    }
  }

  // Count OFF cells with exactly 1 neighbour, exactly 2 neighbours, and all
  // others
  inline void InteractionCountsAndNext(LifeState &__restrict__ out1,
                                       LifeState &__restrict__ out2,
                                       LifeState &__restrict__ outMore,
                                       LifeState &__restrict__ next) const {
    LifeState col0(InitializedTag::UNINITIALIZED), col1(InitializedTag::UNINITIALIZED);
    CountRows(col0, col1);

    for (int i = 0; i < N; i++) {
      int idxU;
      int idxB;
      if (i == 0)
        idxU = N - 1;
      else
        idxU = i - 1;

      if (i == N - 1)
        idxB = 0;
      else
        idxB = i + 1;

      uint64_t u_on1 = col1.state[idxU];
      uint64_t u_on0 = col0.state[idxU];
      uint64_t c_on1 = col1.state[i];
      uint64_t c_on0 = col0.state[i];
      uint64_t l_on1 = col1.state[idxB];
      uint64_t l_on0 = col0.state[idxB];

      uint64_t final_sum, final_carry;
      FullAdd(final_sum, final_carry, u_on0, c_on0, l_on0);

      uint64_t carry_sum, carry_carry;
      FullAdd(carry_sum, carry_carry, u_on1, c_on1, l_on1);

      out1[i] = ~state[i] & ~carry_carry & final_sum & ~carry_sum & ~final_carry;
      out2[i] = ~state[i] & ~carry_carry & ~final_sum & (carry_sum ^ final_carry);
      outMore[i] = ~state[i] & ~out2[i] & (final_carry | carry_sum | carry_carry);

      carry_carry = carry_carry ^ (carry_sum & final_carry);
      next[i] = (final_sum ^ carry_carry) &
                (final_carry ^ carry_sum ^ carry_carry) &
                (state[i] | final_sum);
    }
  }

  unsigned CountNeighboursWithCenter(std::pair<int, int> cell) const {
    if (cell.first > 0 && cell.first < N - 1) {
      unsigned result = 0;
      for (unsigned i = 0; i <= 2; i++) {
        uint64_t column = state[cell.first + i - 1];
        column = std::rotr(column, cell.second - 1);
        result += std::popcount(column & 0b111);
      }
      return result;
    } else {
      unsigned result = 0;
      for (unsigned i = 0; i <= 2; i++) {
        uint64_t column = state[torus_wrap(cell.first + i - 1)];
        column = std::rotr(column, cell.second - 1);
        result += std::popcount(column & 0b111);
      }
      return result;
    }
  }

  unsigned CountNeighbours(std::pair<int, int> cell) const {
    return CountNeighboursWithCenter(cell) - (Get(cell) ? 1 : 0);
  }

  LifeState InteractionOffsets(const LifeState &other) const {
    const LifeState &a_state = *this;
    LifeState a_bit3(InitializedTag::UNINITIALIZED), a_bit2(InitializedTag::UNINITIALIZED),
        a_bit1(InitializedTag::UNINITIALIZED), a_bit0(InitializedTag::UNINITIALIZED);
    CountNeighbourhood(a_bit3, a_bit2, a_bit1, a_bit0);
    LifeState a_out1 = ~a_bit3 & ~a_bit2 & ~a_bit1 &  a_bit0;
    LifeState a_out2 = ~a_bit3 & ~a_bit2 &  a_bit1 & ~a_bit0;
    LifeState a_out3 = ~a_bit3 & ~a_bit2 &  a_bit1 &  a_bit0;

    LifeState b_state = other.Mirrored();
    LifeState b_bit3(InitializedTag::UNINITIALIZED), b_bit2(InitializedTag::UNINITIALIZED),
        b_bit1(InitializedTag::UNINITIALIZED), b_bit0(InitializedTag::UNINITIALIZED);
    b_state.CountNeighbourhood(b_bit3, b_bit2, b_bit1, b_bit0);
    LifeState b_out1 = ~b_bit3 & ~b_bit2 & ~b_bit1 &  b_bit0;
    LifeState b_out2 = ~b_bit3 & ~b_bit2 &  b_bit1 & ~b_bit0;
    LifeState b_out3 = ~b_bit3 & ~b_bit2 &  b_bit1 &  b_bit0;

    return
      // Overlaps
      a_state.Convolve(b_state) |
      // Positions that cause births
      (a_out1 & ~a_state).Convolve(b_out2 & ~b_state) |
      (b_out1 & ~b_state).Convolve(a_out2 & ~a_state) |
      // positions that cause overcrowding
      (a_out3 & a_state).Convolve((b_bit3 | b_bit2 | b_bit1) & ~b_state) |
      ((a_bit2 | a_bit3) & a_state).Convolve((b_bit3 | b_bit2 | b_bit1 | b_bit0) & ~b_state) |
      (b_out3 & b_state).Convolve((a_bit3 | a_bit2 | a_bit1) & ~a_state) |
      ((b_bit2 | b_bit3) & b_state).Convolve((a_bit3 | a_bit2 | a_bit1 | a_bit0) & ~a_state)
      ;
  }

  ////////////////////////////////
  // Parsing and Printing
  ////////////////////////////////

  static inline LifeState Parse(const std::string &rle);

  static LifeState Parse(const std::string &rle, int dx, int dy,
                         SymmetryTransform trans) {
    LifeState result = LifeState::Parse(rle);
    result.Transform(dx, dy, trans);
    return result;
  }

  static LifeState Parse(const std::string &rle, int dx, int dy) {
    LifeState result = LifeState::Parse(rle);
    result.Move(dx, dy);
    return result;
  }

  constexpr static LifeState ConstantParse(const std::string &rle) {
    LifeState result;

    int cnt = 0;
    int x = 0;
    int y = 0;

    for (char const ch : rle) {
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
          if (ch == 'o')
            result.state[x] |= (1ULL << y);
          x++;
        }

        cnt = 0;
      }
    }

    return result;
  }

  constexpr static LifeState ConstantParse(const std::string &rle, int dx, int dy) {
    return LifeState::ConstantParse(rle).Moved(dx, dy);
  }

  inline void Print() const;
  inline std::string RLE() const;
  
  inline std::string EncodeApgcode() const;
  inline std::string EncodeApgcode(unsigned period) const;
  static inline LifeState DecodeApgcode(const std::string& apgcode);

  friend std::ostream &operator<<(std::ostream &os, LifeState const &self) {
    return os << self.RLE();
  }

  ////////////////////////////////
  // Code requiring ConstantParse
  ////////////////////////////////

  LifeState ComponentContaining(const LifeState &seed) const {
    constexpr LifeState corona =
        LifeState::ConstantParse("b3o$5o$5o$5o$b3o!", -2, -2);
    return ComponentContaining(seed, corona);
  }
  std::vector<LifeState> Components() const {
    constexpr LifeState corona =
        LifeState::ConstantParse("b3o$5o$5o$5o$b3o!", -2, -2);
    return Components(corona);
  }
};

void LifeState::Step() {
  uint64_t sum0[N];
  uint64_t sum1[N];
  uint64_t s0[N];
  uint64_t s1[N];

  for (unsigned i = 0; i < N; i++) {
    const uint64_t center = state[i];
    const uint64_t left = std::rotl(center, 1);
    const uint64_t right = std::rotr(center, 1);
    s0[i] = left ^ right;
    s1[i] = left & right;
    sum0[i] = s0[i] ^ center;
    sum1[i] = s1[i] | (s0[i] & center);
  }

  for (unsigned i = 0; i < N; i++) {
    const int idxU = (i == 0) ? N - 1 : i - 1;
    const int idxB = (i == N - 1) ? 0 : i + 1;
    const uint64_t ts0 = sum0[idxB] ^ sum0[idxU];
    const uint64_t ts1 = (sum0[idxB] & sum0[idxU]) | (ts0 & s0[i]);
    state[i] = (sum1[idxB] ^ sum1[idxU] ^ ts1 ^ s1[i]) &
               ((sum1[idxB] | sum1[idxU]) ^ (ts1 | s1[i])) &
               ((ts0 ^ s0[i]) | state[i]);
  }
}

void LifeState::StepAlt() {
  LifeState col0(InitializedTag::UNINITIALIZED), col1(InitializedTag::UNINITIALIZED);
  CountRows(col0, col1);

  for (unsigned i = 0; i < N; i++) {
    int idxU;
    int idxB;
    if (i == 0)
      idxU = N - 1;
    else
      idxU = i - 1;

    if (i == N - 1)
      idxB = 0;
    else
      idxB = i + 1;

    uint64_t a = state[i];

    uint64_t u_on1 = col1.state[idxU];
    uint64_t u_on0 = col0.state[idxU];
    uint64_t c_on1 = col1.state[i];
    uint64_t c_on0 = col0.state[i];
    uint64_t l_on1 = col1.state[idxB];
    uint64_t l_on0 = col0.state[idxB];

    uint64_t final_sum, final_carry;
    FullAdd(final_sum, final_carry, u_on0, c_on0, l_on0);

    uint64_t carry_sum, carry_carry;
    FullAdd(carry_sum, carry_carry, u_on1, c_on1, l_on1);

    carry_carry = carry_carry ^ (final_carry & carry_sum);
    state[i] = (final_sum ^ carry_carry) & (final_carry ^ carry_sum ^ carry_carry) & (a | final_sum);
  }
}

void LifeState::Print() const {
  for (unsigned j = 0; j < 64; j++) {
    for (unsigned i = 0; i < N; i++) {
      if (GetSafe(i - (N / 2), j - 32) == 0) {
        int hor = 0;
        int ver = 0;

        if ((j - 32) % 10 == 0)
          hor = 1;

        if ((i - (N / 2)) % 10 == 0)
          ver = 1;

        if (hor == 1 && ver == 1)
          printf("+");
        else if (hor == 1)
          printf("-");
        else if (ver == 1)
          printf("|");
        else
          printf(".");
      } else
        printf("O");
    }
    printf("\n");
  }
}

static inline void ConvolveInner(LifeState &result,
                                 const uint64_t (&doubledother)[N * 2],
                                 uint64_t x, unsigned int k,
                                 unsigned int postshift) {
  for (unsigned i = 0; i < N; i++) {
    result[i] |= std::rotl(convolve_uint64_t(x, doubledother[i+k]), postshift);
  }
}

LifeState LifeState::Convolve(const LifeState &other) const {
  LifeState result;
  uint64_t doubledother[N * 2];
  memcpy(doubledother, other.state, N * sizeof(uint64_t));
  memcpy(doubledother + N, other.state, N * sizeof(uint64_t));

  for (unsigned j = 0; j < N; j++) {
    unsigned k = N - j;
    uint64_t x = state[j];

    // Annoying special case
    if (x == ~0ULL) {
      ConvolveInner(result, doubledother, ~0ULL, k, 0);
      continue;
    }

    while (x != 0) {
      unsigned int postshift;

      uint64_t shifted;

      if ((x & 1) == 0) { // Possibly wrapped
        int lsb = std::countr_zero(x);
        shifted = std::rotr(x, lsb);
        postshift = lsb;
      } else {
        int lead = std::countl_one(x);
        shifted = std::rotl(x, lead);
        postshift = 64 - lead;
      }

      unsigned runlength = std::countr_one(shifted);
      runlength = std::min(runlength, (unsigned)32);
      uint64_t run = (1ULL << runlength) - 1;

      // Give the compiler a chance to optimise each separately
      switch(run) {
      case (1ULL << 1)  - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 2)  - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 3)  - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 4)  - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 5)  - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 6)  - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 7)  - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 8)  - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 9)  - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 10) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 11) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 12) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 13) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 14) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 15) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 16) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 17) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 18) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 19) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 20) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 21) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 22) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 23) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 24) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 25) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 26) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 27) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 28) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 29) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 30) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 31) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      case (1ULL << 32) - 1: ConvolveInner(result, doubledother, run, k, postshift); break;
      default:               ConvolveInner(result, doubledother, run, k, postshift); break;
      }

      // Already full
      if ((~result).IsEmpty())
        return result;

      x &= ~std::rotl(run, postshift);
    }
  }

  return result;
}

std::vector<std::pair<int, int>> LifeState::OnCells() const {
  LifeState remaining = *this;
  std::vector<std::pair<int, int>> result;
  for (int pop = remaining.GetPop(); pop > 0; pop--) {
    auto cell = remaining.FirstOn();
    result.push_back(cell);
    remaining.Erase(cell.first, cell.second);
  }
  return result;
}

