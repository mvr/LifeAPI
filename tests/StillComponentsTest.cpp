#include <gtest/gtest.h>

#include "../LifeAPI.hpp"
#include "../Parsing.hpp"

TEST(StillComponentsTest, Basics) {
  std::vector<std::pair<std::string, unsigned>> tests = {
      {"2o$2o!", 1},
      {"2ob2o$2ob2o!", 1},
      {"2o$o2bo$2b2o!", 1},
      {"2obo$ob2o!", 1},
      {"2o$obo$2bo$2b2o!", 1},
      {"5bo$bo2bobo$obo2bo$bo!", 2},
  };

  for (auto &[ rle, expected ] : tests) {
    LifeState pat = LifeState::Parse(rle);
    unsigned count = pat.StillComponents().size();
    EXPECT_EQ(count, expected) << "failed for " << rle;
  }
}
