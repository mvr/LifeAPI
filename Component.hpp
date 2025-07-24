#pragma once

#include <string>
#include <vector>
#include <tuple>
#include <stdexcept>
#include <sstream>
#include <unordered_map>

#include "LifeAPI.hpp"
#include "LifeHistory.hpp"
#include "Apgcode.hpp"
#include "Symmetry.hpp"

class GliderSet {
public:
  LifeState se;
  LifeState sw;
  LifeState nw;
  LifeState ne;
    
  GliderSet() = default;
  GliderSet(const LifeState& se, const LifeState& sw, const LifeState& nw, const LifeState& ne);

  // TODO: It is dumb to be recomputing this each time
  unsigned Cost() const { return (se.GetPop() + sw.GetPop() + nw.GetPop() + ne.GetPop())/5; }

  LifeState Realise() const;

  GliderSet Stepped(int n) const;
  GliderSet Rewind(int generations) const;

  GliderSet Transformed(SymmetryTransform t) const;
  GliderSet Moved(std::pair<int, int> p) const;

  static GliderSet FromSJK(const std::string& gliderData, unsigned rewind = 4);
  std::string ToSJK() const;
};

struct Component {
  LifeState base;
  GliderSet gliderSet;
  LifeState out;

  unsigned Cost() const { return gliderSet.Cost(); }
  bool SanityCheck() const;

  LifeState Realise() const;

  static Component FromSJK(const std::string& compStr);
  std::string ToSJK() const;
  std::string RLE() const;

  static std::tuple<std::string, std::string, std::string> SplitSJKLine(const std::string& compStr);
  static std::tuple<std::string, unsigned, std::string> SplitSJKLineCost(const std::string& compStr);
  static std::pair<std::string, int> ParseApgcodeWithPhase(const std::string& inData);

  static LifeState BaseGlider(int steps);
};


GliderSet::GliderSet(const LifeState& se, const LifeState& sw, const LifeState& nw, const LifeState& ne) 
  : se(se), sw(sw), nw(nw), ne(ne) {
}

LifeState GliderSet::Realise() const {
  return se | sw | nw | ne;
}

GliderSet GliderSet::Stepped(int n) const {
  return GliderSet(se.Stepped(n), sw.Stepped(n), nw.Stepped(n), ne.Stepped(n));
}

GliderSet GliderSet::Rewind(int n) const {
  int offset = (n + 3) / 4;  // ceil(generations/4)
  
  int forwardStep = 0;
  if (n % 4 != 0) {
    forwardStep = 4 - (n % 4);
  }
  
  LifeState rewoundSE = se.Moved(-offset, -offset);
  LifeState rewoundSW = sw.Moved(offset, -offset);
  LifeState rewoundNW = nw.Moved(offset, offset);
  LifeState rewoundNE = ne.Moved(-offset, offset);
  
  if (forwardStep > 0) {
    rewoundSE = rewoundSE.Stepped(forwardStep);
    rewoundSW = rewoundSW.Stepped(forwardStep);
    rewoundNW = rewoundNW.Stepped(forwardStep);
    rewoundNE = rewoundNE.Stepped(forwardStep);
  }
  
  return GliderSet(rewoundSE, rewoundSW, rewoundNW, rewoundNE);
}

GliderSet GliderSet::Transformed(SymmetryTransform t) const {
  LifeState tSe = se.Transformed(t);
  LifeState tSw = sw.Transformed(t);
  LifeState tNw = nw.Transformed(t);
  LifeState tNe = ne.Transformed(t);
  
  switch(t) {
  case SymmetryTransform::Identity:
    return GliderSet(tSe, tSw, tNw, tNe);
  case SymmetryTransform::Rotate90:
  case SymmetryTransform::Rotate90Even:
    return GliderSet(tNe, tSe, tSw, tNw);
  case SymmetryTransform::Rotate180OddBoth:
  case SymmetryTransform::Rotate180EvenHorizontal:
  case SymmetryTransform::Rotate180EvenVertical:
  case SymmetryTransform::Rotate180EvenBoth:
    return GliderSet(tNw, tNe, tSe, tSw);
  case SymmetryTransform::Rotate270:
  case SymmetryTransform::Rotate270Even:
    return GliderSet(tSw, tNw, tNe, tSe);
  case SymmetryTransform::ReflectAcrossX:
  case SymmetryTransform::ReflectAcrossXEven:
    return GliderSet(tNe, tNw, tSw, tSe);
  case SymmetryTransform::ReflectAcrossY:
  case SymmetryTransform::ReflectAcrossYEven:
    return GliderSet(tSw, tSe, tNe, tNw);
  case SymmetryTransform::ReflectAcrossYeqX:
    return GliderSet(tSe, tNe, tNw, tSw);
  case SymmetryTransform::ReflectAcrossYeqNegX:
  case SymmetryTransform::ReflectAcrossYeqNegXP1:
    return GliderSet(tNw, tSw, tSe, tNe);
  }
}

GliderSet GliderSet::Moved(std::pair<int, int> p) const {
  return {
    se.Moved(p), sw.Moved(p), nw.Moved(p), ne.Moved(p),
  };
}

GliderSet GliderSet::FromSJK(const std::string& gliderData, unsigned rewind) {
  // Map orientation codes to SymmetryTransform
  static const std::unordered_map<char, SymmetryTransform> orientationMap = {
    {'F', SymmetryTransform::Identity},
    {'L', SymmetryTransform::Rotate270},
    {'B', SymmetryTransform::Rotate180OddBoth},
    {'R', SymmetryTransform::Rotate90},
    {'f', SymmetryTransform::ReflectAcrossX},
    {'l', SymmetryTransform::ReflectAcrossYeqNegXP1},
    {'b', SymmetryTransform::ReflectAcrossY},
    {'r', SymmetryTransform::ReflectAcrossYeqX}
  };
    
  // Parse fields and transformation
  size_t atPos = gliderData.find('@');
  std::string fields = (atPos != std::string::npos) ? gliderData.substr(0, atPos) : gliderData;
  std::string transStr = (atPos != std::string::npos) ? gliderData.substr(atPos + 1) : "";
    
  // Process transformation
  int t = 0;
  SymmetryTransform orientation = SymmetryTransform::Identity;
  int shiftX = 0, shiftY = 0;
    
  if (!transStr.empty()) {
    // Find orientation code
    for (char c : "FLBRflbr") {
      size_t pos = transStr.find(c);
      if (pos != std::string::npos) {
        t = std::stoi(transStr.substr(0, pos));
        orientation = orientationMap.at(c);
                
        // Parse shift values
        std::string shiftPart = transStr.substr(pos + 1);
        std::istringstream iss(shiftPart);
        iss >> shiftX >> shiftY;
        break;
      }
    }
  }
    
  // Parse fields and create salvos
  std::vector<LifeState> salvos(4);
  std::istringstream fieldStream(fields);
  std::string field;
  int salvoIndex = 0;
    
  while (std::getline(fieldStream, field, '/') && salvoIndex < 4) {
    LifeState salvo;
    std::istringstream timeStream(field);
    std::string timeStr, laneStr;
        
    // Parse time/lane pairs
    while (timeStream >> timeStr >> laneStr) {
      int time = std::stoi(timeStr);
      int lane = std::stoi(laneStr);
            
      // Create glider at position and advance by time
      salvo |= Component::BaseGlider(-time - t - 4 - rewind).Moved(0, -lane);
    }
        
    // Apply rotations based on salvo index (SE, SW, NW, NE)
    if (salvoIndex == 1) {
      salvo.Transform(SymmetryTransform::Rotate90);
    } else if (salvoIndex == 2) {
      salvo.Transform(SymmetryTransform::Rotate180OddBoth);
    } else if (salvoIndex == 3) {
      salvo.Transform(SymmetryTransform::Rotate270);
    }
        
    salvos[salvoIndex] = salvo;
    salvoIndex++;
  }
    
  // Create glider set and apply transformation
  GliderSet result(salvos[0], salvos[1], salvos[2], salvos[3]);

  return result.Transformed(orientation).Moved({shiftX, shiftY});
}

LifeState Component::Realise() const {
  return base | gliderSet.Realise();
}

// TODO: This could be better
bool Component::SanityCheck() const {
  if (Realise().Stepped(200) != out)
    return false;

  // Rewind in steps of 16 until all salvos are outside the base pattern bounding box
  auto [minX, minY, maxX, maxY] = base.XYBounds();

  GliderSet rewoundGliders = gliderSet;
  int totalRewind = 0;
  const int rewindStep = 16;
  const int maxRewindSteps = 20; // Safety limit

  for (int step = 0; step < maxRewindSteps; step++) {
    rewoundGliders = rewoundGliders.Rewind(rewindStep);
    totalRewind += rewindStep;

    // Check that each individual salvo is outside the base pattern bounding box
    bool allSalvosOutside = true;

    for (const LifeState& salvo : {rewoundGliders.se, rewoundGliders.sw, rewoundGliders.nw, rewoundGliders.ne}) {
      if (!salvo.IsEmpty()) {
        auto [sMinX, sMinY, sMaxX, sMaxY] = salvo.XYBounds();
        if (!(sMaxX < minX || sMinX > maxX || sMaxY < minY || sMinY > maxY)) {
          allSalvosOutside = false;
          break;
        }
      }
    }

    if (allSalvosOutside) {
      break;
    }
  }

  Component rewoundComp;
  rewoundComp.base = base;
  rewoundComp.gliderSet = rewoundGliders;
  rewoundComp.out = out;

  if (rewoundComp.Realise().Stepped(totalRewind) != Realise())
    return false;

  // TODO: Torus wrap?

  return true;
}

std::tuple<std::string, std::string, std::string> Component::SplitSJKLine(const std::string& compStr) {
  size_t firstPos = compStr.find('>');
  size_t secondPos = compStr.find('>', firstPos + 1);
    
  if (firstPos == std::string::npos || secondPos == std::string::npos) {
    throw std::invalid_argument("Invalid component string format");
  }
    
  std::string inData = compStr.substr(0, firstPos);
  std::string gliderData = compStr.substr(firstPos + 1, secondPos - firstPos - 1);
  std::string outData = compStr.substr(secondPos + 1);
    
  return std::make_tuple(inData, gliderData, outData);
}

std::tuple<std::string, unsigned, std::string> Component::SplitSJKLineCost(const std::string& line) {
  auto [inData, gliderData, outData] = SplitSJKLine(line);

  if (outData == "xs0_0") outData = "";

  // Extract apgcode (before '+' if present)
  std::string apgcode = inData;
  size_t plusPos = inData.find('+');
  if (plusPos != std::string::npos) {
    apgcode = inData.substr(0, plusPos);
  }

  // Count gliders in glider data
  std::string fields = gliderData;
  size_t atPos = gliderData.find('@');
  if (atPos != std::string::npos) {
    fields = gliderData.substr(0, atPos);
  }

  int nGliders = 0;
  std::stringstream fieldStream(fields);
  std::string field;

  while (std::getline(fieldStream, field, '/')) {
    std::stringstream tokens(field);
    std::string token;
    int tokenCount = 0;
    while (tokens >> token) {
      tokenCount++;
    }
    nGliders += tokenCount / 2;
  }

  return std::make_tuple(apgcode, nGliders, outData);
}


std::pair<std::string, int> Component::ParseApgcodeWithPhase(const std::string& inData) {
  size_t plusPos = inData.find('+');
    
  if (plusPos == std::string::npos) {
    return std::make_pair(inData, 0);
  }
    
  std::string apgcode = inData.substr(0, plusPos);
  std::string phaseStr = inData.substr(plusPos + 1);
    
  int phase = 0;
  if (!phaseStr.empty()) {
    try {
      phase = std::stoi(phaseStr);
    } catch (const std::exception&) {
      phase = 0;
    }
  }
    
  return std::make_pair(apgcode, phase);
}

LifeState Component::BaseGlider(int steps) {
  static const std::array<LifeState, 4> gliderPhases = {
    LifeState::ConstantParse("bo$2bo$3o!"),
    LifeState::ConstantParse("obo$b2o$bo!").Moved(0, 1),
    LifeState::ConstantParse("2bo$obo$b2o!").Moved(0, 1),
    LifeState::ConstantParse("o$b2o$2o!").Moved(1, 1)
  };

  return gliderPhases[steps & 0b11].Moved(steps >> 2, steps >> 2);
}

Component Component::FromSJK(const std::string& compStr) {
  auto [inData, gliderData, outData] = SplitSJKLine(compStr);
  auto [apgcode, phase] = ParseApgcodeWithPhase(inData);

  Component result;

  result.base = LifeState::DecodeApgcode(apgcode);
  if (phase > 0) {
    result.base = result.base.Stepped(phase);
  }
  result.gliderSet = GliderSet::FromSJK(gliderData);

  // We can't use the provided Apgcode for the output because we don't
  // know what orientation and offset to place it at, so we just have
  // to run the step
  result.out = result.base | result.gliderSet.Realise();
  unsigned gen = 0;
  bool done = false;
  while (!done) {
    LifeState prev = result.out;

    result.out.Step();
    gen++;
    if (result.out == prev)
      done = true;
    if (gen > 200)
      throw std::runtime_error("Component took too long");
  }

  // Double-check that we got the right output
  // (Torus wrap means we might not)
  if (result.out.EncodeApgcode() != outData)
    throw std::runtime_error("Component gave the wrong result");

  return result;
}

std::string GliderSet::ToSJK() const {
  std::vector<std::vector<std::pair<int, int>>> pairs(4);

  LifeState salvos[4] = {se, sw.Transformed(SymmetryTransform::Rotate90),
                         nw.Transformed(SymmetryTransform::Rotate180OddBoth),
                         ne.Transformed(SymmetryTransform::Rotate270)};
  
  for (int dir = 0; dir < 4; dir++) {
    LifeState salvo = salvos[dir];
    
    // Find gliders in each phase
    for (int phase = 0; phase < 4; phase++) {
      LifeState glider = Component::BaseGlider(phase);
      LifeState matches = salvo.MatchLive(glider);
      
      auto coords = matches.OnCells();
      for (auto [x, y] : coords) {
        int time = 1 - 4 * (x + 1) - (phase + 1) % 4;
        int lane = x - y + (phase > 0 && phase < 3 ? 1 : 0);
        pairs[dir].push_back({time, lane});
      }
    }
    
    std::sort(pairs[dir].begin(), pairs[dir].end());
  }
  
  // Convert to string format
  std::string result;
  for (int dir = 0; dir < 4; dir++) {
    if (dir > 0) result += "/";
    for (size_t i = 0; i < pairs[dir].size(); i++) {
      if (i > 0) result += " ";
      result += std::to_string(pairs[dir][i].first) + " " + std::to_string(pairs[dir][i].second);
    }
  }
  
  return result;
}

std::string Component::ToSJK() const {
  std::string baseApgcode = base.EncodeApgcode();
  std::string gliderData = gliderSet.ToSJK();
  std::string outApgcode = out.EncodeApgcode();
  
  if (outApgcode == "xs0_0") outApgcode = "";
  
  return baseApgcode + ">" + gliderData + ">" + outApgcode;
}

std::string Component::RLE() const {
  LifeState original = base & out;
  LifeHistory history(base | gliderSet.Realise(), LifeState(), LifeState(), original);
  return history.RLEWHeader();
}
