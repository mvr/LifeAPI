#include <gtest/gtest.h>

#include "../LifeAPI.hpp"
#include "../Parsing.hpp"

TEST(ParsingTest, ParseVsConstantParseDifference) {
  auto examples = {
    "2b2o$bobo$bo$2o!",
    "2o$2o!",
  };
  for (auto &rle : examples) {
    LifeState parsed = LifeState::Parse(rle);
    LifeState constantParsed = LifeState::ConstantParse(rle);
    EXPECT_EQ(parsed, constantParsed);
  }
}
