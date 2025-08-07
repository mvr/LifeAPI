#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>

#include "../Component.hpp"
#include "../ComponentTemplate.hpp"

struct SearchResult {
  unsigned cost;
  std::string predecessor;
  std::string componentLine;
    
  SearchResult() : cost(std::numeric_limits<double>::infinity()) {}
  SearchResult(double c, const std::string& pred, const std::string& comp)
    : cost(c), predecessor(pred), componentLine(comp) {}
};

class ComponentDatabase {
public:
  std::unordered_map<std::string, std::vector<std::tuple<unsigned, std::string, std::string>>> db;
    
  void LoadFromFiles(const std::vector<std::string>& filePaths, bool verbose = false);
  void LoadFromFile(const std::string& filePath, bool verbose = false);
  std::unordered_map<std::string, SearchResult> Dijkstra(const std::string& seed = "") const;
  
  std::vector<ComponentTemplate> LoadComponentTemplates(bool verbose = false, unsigned minOccurrences = 1) const;
  
  // Filter function to determine if a component is useful for synthesis
  static bool IsUsefulComponent(const Component& comp);
};

void ComponentDatabase::LoadFromFiles(const std::vector<std::string>& filePaths, bool verbose) {
  db.clear();
  for (const auto& path : filePaths) {
    LoadFromFile(path, verbose);
  }
}

void ComponentDatabase::LoadFromFile(const std::string& filePath, bool verbose) {
  if (verbose) {
    std::cerr << "Reading from " << filePath << std::endl;
  }
    
  std::ifstream file(filePath);
  if (!file.is_open()) {
    return; // Silently ignore missing files like Python version
  }
    
  std::string line;
  while (std::getline(file, line)) {
    // Remove comments and trim
    size_t commentPos = line.find('#');
    if (commentPos != std::string::npos) {
      line = line.substr(0, commentPos);
    }
        
    // Trim whitespace
    line.erase(line.find_last_not_of(" \t\r\n") + 1);
    line.erase(0, line.find_first_not_of(" \t\r\n"));

    if (line.empty()) continue;
        
    try {
      auto [inStr, cost, outStr] = Component::SplitSJKLineCost(line);
      db[inStr].emplace_back(cost, outStr, line);
    } catch (const std::exception&) {
      // Skip invalid lines
      continue;
    }
  }
}


std::unordered_map<std::string, SearchResult> ComponentDatabase::Dijkstra(const std::string& seed) const {
  using PriorityItem = std::pair<double, std::string>;
  std::priority_queue<PriorityItem, std::vector<PriorityItem>, std::greater<PriorityItem>> pq;
    
  std::unordered_map<std::string, SearchResult> result;
  result[seed] = SearchResult(0.0, "", "");
  pq.emplace(0.0, seed);
    
  while (!pq.empty()) {
    auto [dist, curr] = pq.top();
    pq.pop();
        
    if (dist > result[curr].cost) continue;
        
    auto it = db.find(curr);
    if (it == db.end()) continue;
        
    for (const auto& [cost, outStr, compLine] : it->second) {
      double newDist = dist + cost;
            
      auto resultIt = result.find(outStr);
      if (resultIt == result.end() ||
          newDist < resultIt->second.cost ||
          (newDist == resultIt->second.cost && compLine < resultIt->second.componentLine)) {
                
        pq.emplace(newDist, outStr);
        result[outStr] = SearchResult(newDist, curr, compLine);
      }
    }
  }
    
  return result;
}

std::vector<ComponentTemplate> ComponentDatabase::LoadComponentTemplates(bool verbose, unsigned minOccurrences) const {
  if(verbose)
    std::cerr << "Extracting templates from components" << std::endl;
  unsigned total_components = 0;

  std::unordered_map<uint64_t, std::tuple<ComponentTemplate, unsigned, unsigned>> uniqueTemplates; // template, cost, count

  for (const auto &[inputApgcode, components] : db) {
    if (inputApgcode == "") continue;
    if (inputApgcode.substr(0, 2) != "xs") continue;

    for (const auto& [cost, outputApgcode, componentLine] : components) {
      if (outputApgcode == "") continue;
      if (outputApgcode == inputApgcode) continue;
      if (outputApgcode.substr(0, 2) != "xs") continue;

      try {
        Component comp = Component::FromSJK(componentLine);

        // Filter out useless components
        if (!IsUsefulComponent(comp))
          continue;

        ComponentTemplate templ = ComponentTemplate::FromComponent(comp);

        // Calculate hash for all orientations and use the minimum
        uint64_t minHash = 0;
        ComponentTemplate canonicalTempl;

        using enum SymmetryTransform;
        for (auto transform :
             {Identity, ReflectAcrossX, ReflectAcrossYeqX, ReflectAcrossY,
              ReflectAcrossYeqNegXP1, Rotate90, Rotate270, Rotate180OddBoth}) {
          ComponentTemplate transformedTempl = templ.Transformed(transform);
          transformedTempl.NormalisePosition();
          uint64_t hash = transformedTempl.GetHash();
          if (minHash == 0 || hash < minHash) {
            minHash = hash;
            canonicalTempl = transformedTempl;
          }
        }
        
        auto it = uniqueTemplates.find(minHash);
        
        if (it == uniqueTemplates.end()) {
          // First occurrence of this template
          uniqueTemplates[minHash] = std::make_tuple(canonicalTempl, cost, 1);
        } else {
          // Template already exists, increment count and update cost if lower
          auto& [existingTempl, existingCost, count] = it->second;
          count++;
          if (cost < existingCost) {
            existingTempl = canonicalTempl;
            existingCost = cost;
          }
        }
      } catch (const std::exception&) {
        // Skip invalid components
        continue;
      }

      total_components++;
    }
  }

  // Convert to vector, only including templates that occurred at least minOccurrences times
  std::vector<ComponentTemplate> result;
  unsigned templatesBeforeFiltering = uniqueTemplates.size();
  
  for (const auto& [hash, templateTuple] : uniqueTemplates) {
    const auto& [templ, cost, count] = templateTuple;
    if (count >= minOccurrences) {
      result.push_back(templ);
    }
  }
  
  // Sort templates by population (smallest first), then by base hash, then by out hash
  std::sort(result.begin(), result.end(), [](const ComponentTemplate& a, const ComponentTemplate& b) {
    int aBasePop = a.base.GetPop();
    int bBasePop = b.base.GetPop();
    if (aBasePop != bBasePop) {
      return aBasePop < bBasePop;
    }
    
    uint64_t aBaseHash = a.base.GetHash();
    uint64_t bBaseHash = b.base.GetHash();
    if (aBaseHash != bBaseHash) {
      return aBaseHash < bBaseHash;
    }
    
    return a.out.GetHash() < b.out.GetHash();
  });

  // for (auto &t : result) {
  //   std::cout << t.RLE() << std::endl;
  // }

  if (verbose) {
    std::cerr << "Loaded " << total_components << " total components" << std::endl;
    std::cerr << "Found " << templatesBeforeFiltering << " unique templates" << std::endl;
    if (minOccurrences > 1) {
      std::cerr << "Filtered to " << result.size() << " templates (occurring ≥" << minOccurrences << " times)" << std::endl;
    } else {
      std::cerr << "Loaded " << result.size() << " templates" << std::endl;
    }
  }

  return result;
}

std::vector<std::string> GetSJKFiles(const std::string& path) {
  std::vector<std::string> sjkFiles;
    
  try {
    std::filesystem::path fsPath(path);
    
    if (std::filesystem::is_regular_file(fsPath)) {
      // If it's a single file, check if it's an .sjk file
      if (fsPath.extension() == ".sjk") {
        sjkFiles.push_back(fsPath.string());
      }
    } else if (std::filesystem::is_directory(fsPath)) {
      // If it's a directory, recursively find all .sjk files
      for (const auto& entry : std::filesystem::recursive_directory_iterator(fsPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".sjk") {
          sjkFiles.push_back(entry.path().string());
        }
      }
    }
  } catch (const std::filesystem::filesystem_error&) {
    // Path doesn't exist or is inaccessible
  }
    
  return sjkFiles;
}

bool ComponentDatabase::IsUsefulComponent(const Component& comp) {
  // We can't handle pure cleanup steps
  if ((comp.out & ~comp.base).IsEmpty()) {
    return false;
  }

  // Filter out components where all input still lifes are small
  // (these are likely syntheses replicating soups)
  auto components = comp.base.StillComponents();
  if (components.size() > 0) {
    bool hasLargeComponent = false;
    for (const auto& component : components) {
      if (component.GetPop() > 6) {
        hasLargeComponent = true;
        break;
      }
    }
    if (!hasLargeComponent) {
      std::cerr << "Skipping all small component " << comp.Realise() << std::endl;
      return false;
    }
  }

  LifeState state = comp.Realise();
  LifeState everActive;
  unsigned gen = 0;

  bool done = false;
  while (!done) {
    LifeState prev = state;

    everActive |= state ^ comp.base;

    state.Step();
    gen++;
    if (state == prev)
      done = true;
    if (gen > 300)
      throw std::runtime_error("Component took too long");
  }

  // Filter out components which cause disconnected changes
  // The vast majority of these are the same component applied
  // symmetrically
  auto diffComponents = everActive.Components(LifeState::ConstantParse("5o$5o$5o$5o$5o!", -2, -2));
  if (diffComponents.size() > 1) {
    std::cerr << "Skipping disconnected component " << comp.Realise() << std::endl;
    return false;
  }

  return true;
}
