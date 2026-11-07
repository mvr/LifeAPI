#include <gtest/gtest.h>

#include "../LifeAPI.hpp"
#include "../Parsing.hpp"
#include "../Symmetry.hpp"

void TestFundamentalDomain(StaticSymmetry s, std::pair<int, int> offset) {
  LifeState domain = FundamentalDomain(s);
  domain.Move(HalveOffset(s, offset));
  LifeState missing = ~Symmetricize(domain, s, offset);
  EXPECT_TRUE(missing.IsEmpty())
      << "Failed for " << SymmetryToString(s) << " offset (" << offset.first
      << ", " << offset.second << "): " << missing.RLE();
}

TEST(SymmetryTest, FundamentalDomainSymmetricizeOrigin) {
  using enum StaticSymmetry;
  for (auto s : {C1, C2, C4, D2AcrossX, D2AcrossY, D2diagodd, D2negdiagodd, D4, D4diag, }) {
    TestFundamentalDomain(s, {0, 0});
  }
}

TEST(SymmetryTest, FundamentalDomainSymmetricizeOffset) {
  using enum StaticSymmetry;
  for (auto s : {C1, C2, C4, D4, D4diag, }) {
    for (int i = 1; i < 10; i++) {
      for (int j = 1; j < 10; j++) {
        if(s == D4diag && (i + j) % 2 == 1)
          continue;

        TestFundamentalDomain(s, {i, j});
        TestFundamentalDomain(s, {64 - i, j});
        TestFundamentalDomain(s, {i, 64 - j});
        TestFundamentalDomain(s, {64 - i, 64 - j});
      }
    }
  }
  for (int i = 1; i < 10; i++)
    TestFundamentalDomain(D2AcrossX, {0, i});
  for (int i = 1; i < 10; i++)
    TestFundamentalDomain(D2AcrossY, {i, 0});
  for (int i = 1; i < 10; i++)
    TestFundamentalDomain(D2diagodd, {i, 64 - i});
  for (int i = 1; i < 10; i++)
    TestFundamentalDomain(D2negdiagodd, {i, i});
}

TEST(SymmetryTest, PerpComponentDiag) {
  for (int x = 0; x < 10; x++) {
    for (int y = 0; y < 10; y++) {
      if ((x + y) % 2 == 1)
        continue;

      std::pair<int, int> pt = {x, y};
      auto c1 = PerpComponent(SymmetryTransform::ReflectAcrossYeqX, pt);
      auto c2 = PerpComponent(SymmetryTransform::ReflectAcrossYeqNegXP1, pt);
      std::pair<int, int> recombined = {(c1.first + c2.first) % 64,
                                        (c1.second + c2.second) % 64};
      EXPECT_EQ(pt, recombined);
    }
  }
}

void TestIntersectingOffset(StaticSymmetry s, std::pair<int, int> offset) {
  for (int x = 0; x < 10; x++) {
    for (int y = 0; y < 10; y++) {
      std::pair<int, int> pt = {x, y};

      LifeState state = LifeState::Cell(pt);
      LifeState symmetricized = Symmetricize(state, s, offset);

      LifeState offsets = IntersectingOffsets(state, symmetricized, s);

      EXPECT_TRUE(offsets.Get(offset));
    }
  }
}

LifeState C4ImageAtOffset(const LifeState &state, SymmetryTransform transform,
                          std::pair<int, int> offset) {
  LifeState result = state;
  result.Transform(transform);

  using enum SymmetryTransform;
  switch (transform) {
  case Rotate90:
    result.Move(offset);
    return result;
  case Rotate180OddBoth:
    result.Move(offset.first - offset.second, offset.second + offset.first);
    return result;
  case Rotate270:
    result.Move(-offset.second, offset.first);
    return result;
  default:
    __builtin_unreachable();
  }
}

LifeState ReferenceC4IntersectingOffsets(const LifeState &pat1,
                                         const LifeState &pat2) {
  LifeState result;
  using enum SymmetryTransform;
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      const std::pair<int, int> offset{x, y};
      for (auto transform : {Rotate90, Rotate180OddBoth, Rotate270}) {
        if (!(C4ImageAtOffset(pat1, transform, offset) & pat2).IsEmpty()) {
          result.Set(x, y);
          break;
        }
      }
    }
  }
  return result;
}

void ExpectC4IntersectingOffsetsMatchesReference(const LifeState &pat1,
                                                 const LifeState &pat2) {
  EXPECT_EQ(IntersectingOffsets(pat1, pat2, StaticSymmetry::C4),
            ReferenceC4IntersectingOffsets(pat1, pat2));
}

TEST(SymmetryTest, IntersectingOffsets) {
  using enum StaticSymmetry;
  for (auto s : {C2, C4, }) {
    for (int i = 0; i < 10; i++) {
      for (int j = 0; j < 10; j++) {
        TestIntersectingOffset(s, {i, j});
      }
    }
  }
  for (int i = 0; i < 10; i++)
    TestIntersectingOffset(D2AcrossX, {0, i});
  for (int i = 0; i < 10; i++)
    TestIntersectingOffset(D2AcrossY, {i, 0});
  for (int i = 1; i < 10; i++)
    TestIntersectingOffset(D2diagodd, {i, 64 - i});
  for (int i = 0; i < 10; i++)
    TestIntersectingOffset(D2negdiagodd, {i, i});
}

TEST(SymmetryTest, C4IntersectingOffsetsMatchesShiftedImageReference) {
  ExpectC4IntersectingOffsetsMatchesReference(LifeState::Cell({0, 0}),
                                              LifeState::Cell({0, 1}));
  ExpectC4IntersectingOffsetsMatchesReference(LifeState::Cell({0, 0}),
                                              LifeState::Cell({1, 63}));

  for (int ax = 0; ax < 4; ax++) {
    for (int ay = 0; ay < 4; ay++) {
      for (int bx = 0; bx < 4; bx++) {
        for (int by = 0; by < 4; by++) {
          ExpectC4IntersectingOffsetsMatchesReference(
              LifeState::Cell({ax, ay}), LifeState::Cell({bx, by}));
        }
      }
    }
  }

  for (int t = 0; t < 10; t++) {
    LifeState pat1;
    LifeState pat2;
    for (int i = 0; i < 6; i++) {
      pat1.Set((t * 17 + i * 7) % 64, (t * 23 + i * 11) % 64);
      pat2.Set((t * 19 + i * 5 + 3) % 64, (t * 29 + i * 13 + 1) % 64);
    }
    ExpectC4IntersectingOffsetsMatchesReference(pat1, pat2);
  }
}
