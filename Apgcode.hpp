#pragma once

#include "LifeAPI.hpp"
#include "Symmetry.hpp"
#include <string>
#include <vector>
#include <array>
#include <algorithm>

namespace Apgcode {

  class Encoder {
  private:
    static inline char GetChar(int index) {
      if (index < 10) return '0' + index;
      return 'a' + (index - 10);
    }
    
    static std::string EncodeWechsler(const LifeState& pattern);
    static std::string EncodeRuns(const std::vector<uint8_t>& strip);
    static std::vector<uint8_t> PatternToStrip(const LifeState& pattern, int minX, int maxX, int minY, int maxY);
    static std::string FindCanonical(const LifeState& pattern);
    
  public:
    static std::string EncodeStillLife(const LifeState& pattern);
    static std::string EncodeOscillator(const LifeState& pattern, unsigned period);
  };

  class Decoder {
  private:
    static std::vector<std::vector<uint8_t>> DecodeWechsler(const std::string& suffix);
    static LifeState StripsToPattern(const std::vector<std::vector<uint8_t>>& strips);
    
  public:
    static LifeState Decode(const std::string& apgcode);
    static bool IsValidApgcode(const std::string& apgcode);
  };


  std::string Encoder::EncodeWechsler(const LifeState& pattern) {
    if (pattern.IsEmpty()) return "";

    auto [minX, minY, maxX, maxY] = pattern.XYBounds();
    std::vector<std::string> strips;
    
    for (int startY = minY; startY <= maxY; startY += 5) {
      int endY = std::min(startY + 4, maxY);
      auto strip = PatternToStrip(pattern, minX, maxX, startY, endY);
      strips.push_back(EncodeRuns(strip));
    }
    
    std::string result;
    for (size_t i = 0; i < strips.size(); ++i) {
      if (i > 0) result += 'z';
      result += strips[i];
    }
    
    return result;
  }

  std::vector<uint8_t> Encoder::PatternToStrip(const LifeState& pattern, int minX, int maxX, int minY, int maxY) {
    std::vector<uint8_t> result;
    
    for (int x = minX; x <= maxX; ++x) {
      uint8_t column = 0;
      for (int y = minY; y <= maxY; ++y) {
        if (pattern.GetSafe(x, y)) {
          column |= (1 << (y - minY));
        }
      }
      result.push_back(column);
    }
    
    while (!result.empty() && result.back() == 0) {
      result.pop_back();
    }
    
    return result;
  }

  std::string Encoder::EncodeRuns(const std::vector<uint8_t>& strip) {
    if (strip.empty()) return "";
    
    std::string result;
    size_t i = 0;
    
    while (i < strip.size()) {
      if (strip[i] == 0) {
        size_t runLength = 0;
        while (i < strip.size() && strip[i] == 0) {
          runLength++;
          i++;
        }
            
        if (runLength == 2) {
          result += 'w';
        } else if (runLength == 3) {
          result += 'x';
        } else if (runLength >= 4 && runLength <= 39) {
          result += 'y';
          if (runLength <= 13) {
            result += GetChar(runLength - 4);
          } else if (runLength <= 35) {
            result += GetChar(runLength - 14 + 10);
          } else {
            result += GetChar(runLength - 36 + 32);
          }
        } else {
          for (size_t j = 0; j < runLength; ++j) {
            result += '0';
          }
        }
      } else {
        result += GetChar(strip[i]);
        i++;
      }
    }
    
    return result;
  }

  std::string Encoder::FindCanonical(const LifeState& pattern) {
    std::vector<std::string> candidates;
    auto orbit = pattern.SymmetryOrbit();
    
    for (const auto& variant : orbit) {
      std::string encoded = EncodeWechsler(variant);
      if (!encoded.empty()) {
        candidates.push_back(encoded);
      }
    }
    
    if (candidates.empty()) {
      return "";
    }
    
    std::sort(candidates.begin(), candidates.end(), [](const std::string& a, const std::string& b) {
      if (a.length() != b.length()) return a.length() < b.length();
      return a < b;
    });
    
    return candidates[0];
  }

  std::string Encoder::EncodeStillLife(const LifeState& pattern) {
    unsigned pop = pattern.GetPop();
    std::string suffix = FindCanonical(pattern);
    if (suffix.empty()) return "";
    return "xs" + std::to_string(pop) + "_" + suffix;
  }

  std::string Encoder::EncodeOscillator(const LifeState& pattern, unsigned period) {
    std::string suffix = FindCanonical(pattern);
    if (suffix.empty()) return "";
    return "xp" + std::to_string(period) + "_" + suffix;
  }

  std::vector<std::vector<uint8_t>> Decoder::DecodeWechsler(const std::string& suffix) {
    std::vector<std::vector<uint8_t>> result;
    std::vector<uint8_t> row;
    size_t i = 0;
    
    while (i < suffix.length()) {
      char c = suffix[i];
        
      if (c == 'z') {
        i++;
        result.push_back(row);
        row = {};
        continue;
      }
        
      if (c == 'w') {
        row.push_back(0);
        row.push_back(0);
      } else if (c == 'x') {
        row.push_back(0);
        row.push_back(0);
        row.push_back(0);
      } else if (c == 'y' && i + 1 < suffix.length()) {
        char next = suffix[i + 1];
        int runLength = 4;
            
        if (next >= '0' && next <= '9') {
          runLength += (next - '0');
        } else if (next >= 'a' && next <= 'v') {
          runLength += (next - 'a' + 10);
        }
            
        for (int j = 0; j < runLength; ++j) {
          row.push_back(0);
        }
        i++;
      } else if (c >= '0' && c <= '9') {
        row.push_back(c - '0');
      } else if (c >= 'a' && c <= 'v') {
        row.push_back(c - 'a' + 10);
      }
        
      i++;
    }

    if(row.size() > 0)
      result.push_back(row);
    
    return result;
  }

  LifeState
  Decoder::StripsToPattern(const std::vector<std::vector<uint8_t>> &strips) {
    LifeState result;
    
    int y = 0;

    for (auto &row : strips) {
      for (size_t x = 0; x < row.size(); ++x) {
        uint8_t column = row[x];
        for (int bit = 0; bit < 5; ++bit) {
          if (column & (1 << bit)) {
            result.SetSafe(x, y + bit, true);
          }
        }
      }
      y += 5;
    }
    
    return result;
  }

  LifeState Decoder::Decode(const std::string& apgcode) {
    size_t underscorePos = apgcode.find('_');
    if (underscorePos == std::string::npos) {
      return LifeState();
    }
    
    std::string prefix = apgcode.substr(0, underscorePos);
    std::string suffix = apgcode.substr(underscorePos + 1);
    
    auto decoded = DecodeWechsler(suffix);
    return StripsToPattern(decoded);
  }

  bool Decoder::IsValidApgcode(const std::string& apgcode) {
    size_t underscorePos = apgcode.find('_');
    if (underscorePos == std::string::npos) {
      return false;
    }
    
    std::string prefix = apgcode.substr(0, underscorePos);
    return (prefix.substr(0, 2) == "xs" || prefix.substr(0, 2) == "xp") && 
      prefix.length() > 2;
  }

} // namespace Apgcode

// LifeState method implementations
std::string LifeState::EncodeApgcode() const {
  return Apgcode::Encoder::EncodeStillLife(*this);
}

std::string LifeState::EncodeApgcode(unsigned period) const {
  return Apgcode::Encoder::EncodeOscillator(*this, period);
}

LifeState LifeState::DecodeApgcode(const std::string& apgcode) {
  return Apgcode::Decoder::Decode(apgcode);
}
