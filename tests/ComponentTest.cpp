#include <gtest/gtest.h>

#include "../LifeAPI.hpp"
#include "../Apgcode.hpp"
#include "../Component.hpp"
#include "../Parsing.hpp"

void DeleteOccurrences(LifeState &state, const LifeState &target) {
  LifeState matches = state.MatchLive(target);

  auto coords = matches.OnCells();
  for (auto [x, y] : coords) {
    state &= ~ target.Moved(x, y);
  }
}

// Helper function to validate that gliders in each salvo are moving in the correct direction
void ValidateGliderSet(const GliderSet& gs, const std::string& testName = "") {
    // Create reference gliders in each direction
    LifeState seGlider = LifeState::ConstantParse("bo$2bo$3o!");  // SE-moving glider
    LifeState swGlider = seGlider.Transformed(SymmetryTransform::Rotate90);    // SW-moving
    LifeState nwGlider = seGlider.Transformed(SymmetryTransform::Rotate180OddBoth); // NW-moving  
    LifeState neGlider = seGlider.Transformed(SymmetryTransform::Rotate270);   // NE-moving

    GliderSet remaining = gs;

    for (int t = 0; t < 4; t++) {
      DeleteOccurrences(remaining.se, seGlider);
      DeleteOccurrences(remaining.sw, swGlider);
      DeleteOccurrences(remaining.nw, nwGlider);
      DeleteOccurrences(remaining.ne, neGlider);

      seGlider.Step();
      swGlider.Step();
      nwGlider.Step();
      neGlider.Step();
    }

    if (!remaining.se.IsEmpty()) {
      FAIL() << testName << ": SE salvo contains non SE gliders: " << gs.se;
    }
    if (!remaining.sw.IsEmpty()) {
      FAIL() << testName << ": SW salvo contains non SW gliders: " << gs.sw;
    }
    if (!remaining.nw.IsEmpty()) {
      FAIL() << testName << ": NW salvo contains non NW gliders: " << gs.nw;
    }
    if (!remaining.ne.IsEmpty()) {
      FAIL() << testName << ": NE salvo contains non NE gliders: " << gs.ne;
    }
}

TEST(ComponentTest, ParseComponentString) {
    std::string compStr = "xs4_33+2>glider_data>xs6_696";
    auto [inData, gliderData, outData] = Component::SplitSJKLine(compStr);
    
    EXPECT_EQ(inData, "xs4_33+2");
    EXPECT_EQ(gliderData, "glider_data");
    EXPECT_EQ(outData, "xs6_696");
}

TEST(ComponentTest, ParseComponentStringNoPhase) {
    std::string compStr = "xs4_33>glider_data>xs6_696";
    auto [inData, gliderData, outData] = Component::SplitSJKLine(compStr);
    
    EXPECT_EQ(inData, "xs4_33");
    EXPECT_EQ(gliderData, "glider_data");
    EXPECT_EQ(outData, "xs6_696");
}

TEST(ComponentTest, ParseApgcodeWithPhase) {
  {
    auto [apgcode, phase] = Component::ParseApgcodeWithPhase("xs4_33+5");
    EXPECT_EQ(apgcode, "xs4_33");
    EXPECT_EQ(phase, 5);
  }
  {
    auto [apgcode, phase] = Component::ParseApgcodeWithPhase("xs4_33");
    EXPECT_EQ(apgcode, "xs4_33");
    EXPECT_EQ(phase, 0);
  }
  {
    auto [apgcode, phase] = Component::ParseApgcodeWithPhase("xs4_33+");
    EXPECT_EQ(apgcode, "xs4_33");
    EXPECT_EQ(phase, 0);
  }
}

TEST(ComponentTest, InvalidComponentString) {
    EXPECT_THROW(Component::SplitSJKLine("invalid"), std::invalid_argument);
    EXPECT_THROW(Component::SplitSJKLine("only>one"), std::invalid_argument);
}

TEST(ComponentTest, GliderSetAdvance) {
    LifeState seGlider = LifeState::ConstantParse("bo$2bo$3o!");
    LifeState swGlider, nwGlider, neGlider;
    
    GliderSet gs(seGlider, swGlider, nwGlider, neGlider);
    GliderSet advanced = gs.Stepped(1);
    
    EXPECT_NE(gs.Realise(), advanced.Realise());
    EXPECT_NE(gs.se, advanced.se);
}

TEST(ComponentTest, RealiseCompExamples) {
  auto sjk = {
    "xs11_2ege13>23 -5 44 -3/-10 2 9 1 28 1/17 -2 45 -3/@0F11 -7>xs23_wgbaa4zmlk3z1023",
    "xs18_3lk453z3443>23 -5 44 -3/-10 2 9 1 28 1/17 -2 45 -3/@0L-7 -6>xs30_mkl3z124b88a6zw6996",
    "xs19_0j9cz0haak8z121>9 -1/-3 3//@0L-4 11>xs20_259ak8z069pz032",
    "xs20_01784q2sgzc8421>33 0/-15 9 15 4 20 -4/21 3/@0r-1 9>xs21_0ciabgz6221w1248c",
    "xs20_0iu069a4z32ac>17 -3/4 0/17 -3/@0r-3 7>xs21_0c88e1egoz2596",
    "xs16_4aajk4ozx121>41 -4/-8 8 -4 2 15 -8 37 -12/7 4 40 -2/@0L-2 -5>xs21_g886licz1pl3",
    "xs16_j1u0696z11>23 -5 44 -3/-10 2 9 1 28 1/17 -2 45 -3/@0B-6 12>xs28_xgill2zca96w23z3156",
    "xs17_3iaczw11d96>44 -4/-10 14 5 3 18 -8 30 -3 59 -14/11 0 26 2 45 4/@0B-2 11>xs21_0j9ab96zc453",
    "xs17_69jwrbzx121>17 -4/8 -2 38 -6/-4 10 9 0/@0f16 6>xs20_69jwrb8ozx121",
    "xs17_kq23z14606a4>14 -4/-8 6 12 -4//@0l11 6>xs20_8e1231kmzx4a43",
    "xs18_08e1u8z69521>17 -4/8 -2 38 -6/-4 10 9 0/@0b-9 0>xs21_069a4oz3117871",
    "xs18_0j5q4oz34521>63 -3/-7 17/-20 16 -3 9 23 2 42 -3/-34 25 -4 16 35 5@0r6 6>xs20_069m88gz69a521",
  };

  for (auto &s : sjk) {
    auto [base, gliderSet, out] = Component::FromSJK(s);
    ValidateGliderSet(gliderSet, "RealiseCompExamples");
    EXPECT_EQ((base | gliderSet.Realise()).Stepped(200).EncodeApgcode(), out.EncodeApgcode()) << "Got " << (base | gliderSet.Realise()).Stepped(200);
  }
}

// TEST(ComponentTest, RoundTripExamples) {
//   auto sjk = {
//     "xs11_2ege13>23 -5 44 -3/-10 2 9 1 28 1/17 -2 45 -3/@0F11 -7>xs23_wgbaa4zmlk3z1023",
//     "xs18_3lk453z3443>23 -5 44 -3/-10 2 9 1 28 1/17 -2 45 -3/@0L-7 -6>xs30_mkl3z124b88a6zw6996",
//     "xs19_0j9cz0haak8z121>9 -1/-3 3//@0L-4 11>xs20_259ak8z069pz032",
//     "xs20_01784q2sgzc8421>33 0/-15 9 15 4 20 -4/21 3/@0r-1 9>xs21_0ciabgz6221w1248c",
//     "xs20_0iu069a4z32ac>17 -3/4 0/17 -3/@0r-3 7>xs21_0c88e1egoz2596",
//     "xs16_4aajk4ozx121>41 -4/-8 8 -4 2 15 -8 37 -12/7 4 40 -2/@0L-2 -5>xs21_g886licz1pl3",
//     "xs16_j1u0696z11>23 -5 44 -3/-10 2 9 1 28 1/17 -2 45 -3/@0B-6 12>xs28_xgill2zca96w23z3156",
//     "xs17_3iaczw11d96>44 -4/-10 14 5 3 18 -8 30 -3 59 -14/11 0 26 2 45 4/@0B-2 11>xs21_0j9ab96zc453",
//     "xs17_69jwrbzx121>17 -4/8 -2 38 -6/-4 10 9 0/@0f16 6>xs20_69jwrb8ozx121",
//     "xs17_kq23z14606a4>14 -4/-8 6 12 -4//@0l11 6>xs20_8e1231kmzx4a43",
//     "xs18_08e1u8z69521>17 -4/8 -2 38 -6/-4 10 9 0/@0b-9 0>xs21_069a4oz3117871",
//     "xs18_0j5q4oz34521>63 -3/-7 17/-20 16 -3 9 23 2 42 -3/-34 25 -4 16 35 5@0r6 6>xs20_069m88gz69a521",
//   };

//   for (auto &s : sjk) {
//     auto comp = Component::FromSJK(s);
//     auto out = comp.ToSJK();
//     EXPECT_EQ(s, out) << "Got " << out;
//   }
// }

TEST(ComponentTest, GliderDirectionsFromSJK) {
    // Create a simple SJK line with one glider in each direction
    // Format: SE/SW/NW/NE salvos separated by '/'
    // Each salvo has time/lane pairs
    std::string sjkGliders = "0 0/0 0/0 0/0 0";  // One glider per direction at time 0, lane 0
    
    GliderSet gs = GliderSet::FromSJK(sjkGliders);
    
    // Use helper to validate directions
    ValidateGliderSet(gs, "GliderDirectionsFromSJK");
}

TEST(ComponentTest, TransformExamples) {
    // Use a glider set with gliders in all four directions
    GliderSet gs = GliderSet::FromSJK("0 0/0 0/0 0/0 0");
    
    using enum SymmetryTransform;
    
    // Test various transformations - each should preserve glider directions within salvos
    ValidateGliderSet(gs.Transformed(Identity), "Identity");
    ValidateGliderSet(gs.Transformed(Rotate90), "Rotate90");
    ValidateGliderSet(gs.Transformed(Rotate180OddBoth), "Rotate180");
    ValidateGliderSet(gs.Transformed(Rotate270), "Rotate270");
    ValidateGliderSet(gs.Transformed(ReflectAcrossX), "ReflectAcrossX");
    ValidateGliderSet(gs.Transformed(ReflectAcrossY), "ReflectAcrossY");
    ValidateGliderSet(gs.Transformed(ReflectAcrossYeqX), "ReflectAcrossYeqX");
    ValidateGliderSet(gs.Transformed(ReflectAcrossYeqNegX), "ReflectAcrossYeqNegX");
}

TEST(ComponentTest, GliderSetRewindBasic) {
    // Test basic rewind functionality with separated gliders
    std::string sjkGliders = "0 0/0 10/0 -10/0 -20";  // Gliders in different lanes to avoid overlap
    GliderSet original = GliderSet::FromSJK(sjkGliders);
    
    // Test that rewinding and then stepping forward gives back the original
    int rewindSteps = 8;
    GliderSet rewound = original.Rewind(rewindSteps);
    GliderSet restored = rewound.Stepped(rewindSteps);
    
    EXPECT_EQ(original.Realise(), restored.Realise()) << "Rewind + Step should restore original state";
}

TEST(ComponentTest, GliderSetRewindRoundTrip) {
    std::vector<std::string> testCases = {
        "0 0///",
        "/0 0//",
        "//0 0/",
        "///0 0",
        "0 0/0 10/0 -10/0 -20"
    };

    for (const auto& sjk : testCases) {
        GliderSet original = GliderSet::FromSJK(sjk);

        for (int rewindSteps : {0,1,2,3,4,5,6,7,8 }) {
            GliderSet rewound = original.Rewind(rewindSteps);
            GliderSet restored = rewound.Stepped(rewindSteps);

            EXPECT_EQ(original.se, restored.se)
                << "SE mismatch for sjk=" << sjk << " rewind=" << rewindSteps;
            EXPECT_EQ(original.sw, restored.sw)
                << "SW mismatch for sjk=" << sjk << " rewind=" << rewindSteps;
            EXPECT_EQ(original.nw, restored.nw)
                << "NW mismatch for sjk=" << sjk << " rewind=" << rewindSteps;
            EXPECT_EQ(original.ne, restored.ne)
                << "NE mismatch for sjk=" << sjk << " rewind=" << rewindSteps;
        }
    }
}
