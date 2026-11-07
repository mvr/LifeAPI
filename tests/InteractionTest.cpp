#include <gtest/gtest.h>

#include "../LifeAPI.hpp"
#include "../Parsing.hpp"
#include "../Symmetry.hpp"

TEST(InteractionTest, EaterSelfInteractionTest) {
  LifeState eater = LifeState::ConstantParse("2b2o$bobo$bo$2o!");

  LifeState offsets = eater.InteractionOffsets(eater);
  for (int i = -10; i < 10; i++) {
    for (int j = -10; j < 10; j++) {
      if (!(eater & eater.Moved(i, j)).IsEmpty())
        continue;

      LifeState together = eater | eater.Moved(i, j);
      LifeState stepped = together;
      stepped.Step();

      if (offsets.GetSafe(i, j)) {
        EXPECT_FALSE(together == stepped) << "Expected interaction at offset (" << i << ", " << j << "): " << together;
      } else {
        EXPECT_TRUE(together == stepped) << "Expected no interaction at offset (" << i << ", " << j << "): " << together;
      }
    }
  }
}

void TestInteractionCounts(LifeState state) {
  LifeState bit3(InitializedTag::UNINITIALIZED), bit2(InitializedTag::UNINITIALIZED), bit1(InitializedTag::UNINITIALIZED), bit0(InitializedTag::UNINITIALIZED);
  state.CountNeighbourhood(bit3, bit2, bit1, bit0);
  LifeState true1(InitializedTag::UNINITIALIZED), true2(InitializedTag::UNINITIALIZED), trueM(InitializedTag::UNINITIALIZED);
  true1 = ~state & ~bit3 & ~bit2 & ~bit1 & bit0;
  true2 = ~state & ~bit3 & ~bit2 & bit1 & ~bit0;
  trueM = ~state & (bit3 | bit2 | (bit1 & bit0));

  LifeState fast1(InitializedTag::UNINITIALIZED), fast2(InitializedTag::UNINITIALIZED), fastM(InitializedTag::UNINITIALIZED);
  state.InteractionCounts(fast1, fast2, fastM);

  EXPECT_EQ(true1, fast1);
  EXPECT_EQ(true2, fast2);
  EXPECT_EQ(trueM, fastM);
}

TEST(InteractionTest, EaterInteractionCountsTest) {
  LifeState eater = LifeState::ConstantParse("2b2o$bobo$bo$2o!");
  TestInteractionCounts(eater);
}

TEST(InteractionTest, RandomInteractionCountsTest) {
  for (unsigned i = 0; i < 100; i++) {
    LifeState random = LifeState::RandomState();
    TestInteractionCounts(random);
  }
}

TEST(InteractionTest, FirstOnReturnsLiveCellOrEmptySentinel) {
  std::vector<LifeState> states = {
      LifeState(),
      LifeState::Cell({0, 0}),
      LifeState::Cell({63, 63}),
      LifeState::Cell({31, 31}),
      LifeState::SolidRect(0, 0, 1, 64),
      LifeState::SolidRect(60, 0, 4, 64),
      LifeState::SolidRect(8, 8, 16, 16),
      LifeState::Checkerboard(),
      ~LifeState(),
  };

  for (int x = 0; x < N; x++) {
    for (int y = 0; y < N; y++) {
      states.push_back(LifeState::Cell({x, y}));
    }
  }

  for (unsigned i = 0; i < 100; i++) {
    states.push_back(LifeState::RandomState());
  }

  for (const auto &state : states) {
    const auto cell = state.FirstOn();
    if (state.IsEmpty()) {
      EXPECT_EQ(cell, std::make_pair(-1, -1));
    } else {
      ASSERT_GE(cell.first, 0);
      ASSERT_LT(cell.first, N);
      ASSERT_GE(cell.second, 0);
      ASSERT_LT(cell.second, N);
      EXPECT_TRUE(state.Get(cell));
    }
  }
}
