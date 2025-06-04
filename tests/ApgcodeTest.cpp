#include <gtest/gtest.h>

#include "../LifeAPI.hpp"
#include "../Parsing.hpp"
#include "../Apgcode.hpp"

TEST(ApgcodeTest, EncodeStillBasics) {
  std::vector<std::pair<std::string, std::string>> tests = {
      {"2o$2o!", "xs4_33"},
      {"b2o$o2bo$b2o!", "xs6_696"},
      {"b2o$o2bo$bobo$2bo!", "xs7_2596"}};

  for (auto &[ rle, expected ] : tests) {
    LifeState pat = LifeState::Parse(rle);
    std::string apgcode = pat.EncodeApgcode();
    EXPECT_EQ(apgcode, expected);
  }
}

TEST(ApgcodeTest, EncodeOscBasics) {
  std::vector<std::pair<std::string, std::string>> tests = {
      {"3o!", "xp2_7"},
      {"b3o$3o!", "xp2_7e"},
      {"2o$o$3bo$2b2o!", "xp2_318c"}};

  for (auto &[ rle, expected ] : tests) {
    LifeState pat = LifeState::Parse(rle);
    std::string apgcode = pat.EncodeApgcode(2);
    EXPECT_EQ(apgcode, expected);
  }
}

TEST(ApgcodeTest, DecodeStillBasics) {
  std::vector<std::pair<std::string, std::string>> tests = {
      {"2o$2o!", "xs4_33"},
      {"bo$obo$obo$bo!", "xs6_696"},
      {"b2o$o2bo$bobo$2bo!", "xs7_2596"}};

  for (auto &[ rle, apgcode ] : tests) {
    LifeState decoded = LifeState::DecodeApgcode(apgcode);
    LifeState expected = LifeState::Parse(rle);

    EXPECT_EQ(decoded, expected);
  }
}

TEST(ApgcodeTest, RoundTripApgcode) {
  std::vector<std::string> tests = {"xs4_33",
                                    "xs6_696",
                                    "xs7_25ac",
                                    "xs31_0ca178b96z69d1d96",
                                    "xs37_g8eh6o8zd5llc1uizx343",

                                    "xs17_4aabaa4zw252"};

  for (auto &apgcode : tests) {
    LifeState decoded = LifeState::DecodeApgcode(apgcode);
    std::string encoded = decoded.EncodeApgcode();

    EXPECT_EQ(apgcode, encoded);
  }
}

TEST(ApgcodeTest, ValidateApgcodes) {
    EXPECT_TRUE(Apgcode::Decoder::IsValidApgcode("xs4_33"));
    EXPECT_TRUE(Apgcode::Decoder::IsValidApgcode("xp2_7"));
    EXPECT_TRUE(Apgcode::Decoder::IsValidApgcode("xs6_696"));
    
    EXPECT_FALSE(Apgcode::Decoder::IsValidApgcode("invalid"));
    EXPECT_FALSE(Apgcode::Decoder::IsValidApgcode("xs4"));
    EXPECT_FALSE(Apgcode::Decoder::IsValidApgcode("_33"));
    EXPECT_FALSE(Apgcode::Decoder::IsValidApgcode(""));
}

TEST(ApgcodeTest, EmptyPattern) {
    LifeState empty;
    std::string encoded = empty.EncodeApgcode();
    EXPECT_TRUE(encoded.empty());
}

TEST(ApgcodeTest, WechslerRunEncoding) {
    LifeState pattern = LifeState::Parse("o4bo!");
    std::string encoded = pattern.EncodeApgcode();
    
    EXPECT_FALSE(encoded.empty());
    EXPECT_TRUE(encoded.find("w") != std::string::npos || 
                encoded.find("x") != std::string::npos ||
                encoded.find("0") != std::string::npos);
}

TEST(ApgcodeTest, MultiStripPattern) {
    LifeState pattern = LifeState::Parse("o$o$o$o$o$o$o!");
    std::string encoded = pattern.EncodeApgcode();
    
    EXPECT_FALSE(encoded.empty());
    EXPECT_TRUE(encoded.find("z") != std::string::npos);
}
