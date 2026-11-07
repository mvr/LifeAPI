#include <gtest/gtest.h>

#include "../LifeAPI.hpp"
#include "../Parsing.hpp"
#include "../Symmetry.hpp"

namespace {

LifeState ReferenceHalve(const LifeState &state) {
  LifeState result;
  for (unsigned x = 0; x < 64; x++) {
    for (unsigned y = 0; y < 64; y++) {
      result.Set(x, y,
                 state.Get((2 * (x % 32)) % 64, (2 * (y % 32)) % 64));
    }
  }
  return result;
}

void ExpectHalveMatchesReference(const LifeState &state) {
  EXPECT_EQ(state.Halve(), ReferenceHalve(state));
}

} // namespace

TEST(HalveTest, EmptyAndFullStates) {
  ExpectHalveMatchesReference(LifeState());
  ExpectHalveMatchesReference(~LifeState());
}

TEST(HalveTest, SingleCells) {
  for (unsigned x = 0; x < 64; x++) {
    for (unsigned y = 0; y < 64; y++) {
      ExpectHalveMatchesReference(LifeState::Cell({x, y}));
    }
  }
}

TEST(HalveTest, StructuredStates) {
  ExpectHalveMatchesReference(LifeState::SolidRect(0, 0, 1, 64));
  ExpectHalveMatchesReference(LifeState::SolidRect(0, 0, 64, 1));
  ExpectHalveMatchesReference(LifeState::SolidRect(7, 11, 17, 19));
  ExpectHalveMatchesReference(LifeState::Checkerboard());
  ExpectHalveMatchesReference(LifeState::ConstantParse("2o$obo$3o!"));
}

TEST(HalveTest, RandomStates) {
  for (unsigned i = 0; i < 100; i++) {
    ExpectHalveMatchesReference(LifeState::RandomState());
  }
}
