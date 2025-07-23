#pragma once

#include <bit>

#include "LifeAPI.hpp"
#include "Symmetry.hpp"

struct NeighbourCount {
  LifeState bit3;
  LifeState bit2;
  LifeState bit1;
  LifeState bit0;

  NeighbourCount()
  : bit3{InitializedTag::UNINITIALIZED}, bit2{InitializedTag::UNINITIALIZED}, bit1{InitializedTag::UNINITIALIZED}, bit0{InitializedTag::UNINITIALIZED} {}
  NeighbourCount(const LifeState &bit3, const LifeState &bit2, const LifeState &bit1, const LifeState &bit0) : bit3{bit3}, bit2{bit2}, bit1{bit1}, bit0{bit0} {};

  NeighbourCount operator~() const {
    NeighbourCount result;
    result.bit3 = ~bit3;
    result.bit2 = ~bit2;
    result.bit1 = ~bit1;
    result.bit0 = ~bit0;
    return result;
  }

  NeighbourCount Transformed(SymmetryTransform t) const {
    return {bit3.Transformed(t), bit2.Transformed(t), bit1.Transformed(t),
            bit0.Transformed(t)};
  }

  NeighbourCount Moved(std::pair<int, int> p) const {
    return {bit3.Moved(p), bit2.Moved(p), bit1.Moved(p), bit0.Moved(p)};
  }

  static void CountRows(const LifeState &state,
                        uint64_t (&col0)[N + 2],
                        uint64_t (&col1)[N + 2]) {
    for (unsigned i = 0; i < N; i++) {
      uint64_t a = state.state[i];
      uint64_t l = std::rotl(a, 1);
      uint64_t r = std::rotr(a, 1);

      col0[i+1] = l ^ r ^ a;
      col1[i+1] = ((l ^ r) & a) | (l & r);
    }
    col0[0] = col0[N]; col0[N+1] = col0[1];
    col1[0] = col1[N]; col1[N+1] = col1[1];
  }

  NeighbourCount(const LifeState &state)
  : bit3{InitializedTag::UNINITIALIZED}, bit2{InitializedTag::UNINITIALIZED}, bit1{InitializedTag::UNINITIALIZED}, bit0{InitializedTag::UNINITIALIZED} {
    uint64_t col0[N + 2];
    uint64_t col1[N + 2];
    CountRows(state, col0, col1);

    for (unsigned i = 0; i < N; i++) {
      uint64_t u_on0 = col0[i];
      uint64_t c_on0 = col0[i+1];
      uint64_t l_on0 = col0[i+2];
      uint64_t u_on1 = col1[i];
      uint64_t c_on1 = col1[i+1];
      uint64_t l_on1 = col1[i+2];

      uint64_t on3, on2, on1, on0;

      uint64_t uc0, uc1, uc2, uc_carry0;
      LifeState::HalfAdd(uc0, uc_carry0, u_on0, c_on0);
      LifeState::FullAdd(uc1, uc2, u_on1, c_on1, uc_carry0);

      uint64_t on_carry1, on_carry0;
      LifeState::HalfAdd(on0, on_carry0, uc0, l_on0);
      LifeState::FullAdd(on1, on_carry1, uc1, l_on1, on_carry0);
      LifeState::HalfAdd(on2, on3, uc2, on_carry1);

      bit3.state[i] = on3;
      bit2.state[i] = on2;
      bit1.state[i] = on1;
      bit0.state[i] = on0;
    }
  }
  inline NeighbourCount Add(const NeighbourCount &other, const LifeState &incarry) const {
    NeighbourCount result;
    LifeState carry = incarry;
    LifeState::FullAdd(result.bit0, carry, bit0, other.bit0, carry);
    LifeState::FullAdd(result.bit1, carry, bit1, other.bit1, carry);
    LifeState::FullAdd(result.bit2, carry, bit2, other.bit2, carry);
    LifeState::FullAdd(result.bit3, carry, bit3, other.bit3, carry);
    return result;
  }

  inline NeighbourCount operator+(const NeighbourCount &other) const {
    return Add(other, LifeState());
  }

  inline NeighbourCount Subtract(const NeighbourCount &other) const {
    return Add(~other, ~LifeState());
  }

  inline NeighbourCount operator-(const NeighbourCount &other) const {
    return Add(~other, ~LifeState());
  }

  inline LifeState WithExactly(unsigned n) {
    LifeState result = ~LifeState();

    if (n & (1<<0)) result &= bit0; else result &= ~bit0;
    if (n & (1<<1)) result &= bit1; else result &= ~bit1;
    if (n & (1<<2)) result &= bit2; else result &= ~bit2;
    if (n & (1<<3)) result &= bit3; else result &= ~bit3;

    return result;
  }

  // inline NeighbourCount Add(uint64_t mask, const NeighbourCount &other, const LifeState &incarry) const {
  //   NeighbourCount result;
  //   LifeState carry = incarry;
  //   LifeState carry2(InitializedTag::UNINITIALIZED);
  //   LifeState::FullAdd(mask, result.bit0, carry2, bit0, other.bit0, carry);
  //   LifeState::FullAdd(mask, result.bit1, carry,  bit1, other.bit1, carry2);
  //   LifeState::FullAdd(mask, result.bit2, carry2, bit2, other.bit2, carry);
  //   LifeState::FullAdd(mask, result.bit3, carry,  bit3, other.bit3, carry2);
  //   return result;
  // }

  // inline NeighbourCount Subtract(uint64_t mask, const NeighbourCount &other) const {
  //   return Add(mask, ~other, ~LifeState());
  // }
};
