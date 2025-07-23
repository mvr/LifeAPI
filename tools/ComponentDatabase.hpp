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