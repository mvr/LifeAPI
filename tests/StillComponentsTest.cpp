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

TEST(StillComponentsTest, IsPseudoStillLife) {
  std::vector<std::pair<std::string, bool>> tests = {
    // Empty pattern
    {"!", true},

    {"2o$2o!", false},           // block
    {"bo$obo$2o!", false},       // beehive
    {"2o$obo$bobo$b2o!", false}, // boat
    {"o2bo$4o2$4o$o2bo!", false}, // Mirrored Table

    {"2o$2o5$2o$2o!", true},               // two blocks
    {"2ob2o$2ob2o!", true},                // two blocks touching
    {"3bo$2bobo$2bobo$3bo2$2o$2o!", true}, // corner interaction

    {"2ob2o$2ob2o2$2ob2o$2ob2o!", true}, // quad blocks
    {"6b2o$2bobo2bo$bob2obo$bo4b2o$2ob2o$3b2ob2o$2o4bo$bob2obo$o2bobo$2o!", true}, // Triple Pseudo
    {"8b2o$3b2obo2bo$3bob2obo$8b2o$3bob2o3bo$b3ob2ob2o$o7bo$b3ob2obo$3bobobo!", true}, // Quad Pseudo
  };



  for (auto &[ rle, expected ] : tests) {
    LifeState pat = LifeState::Parse(rle);
    bool result = pat.IsPseudoStillLife();
    EXPECT_EQ(result, expected) << "failed for " << rle;
  }
}
