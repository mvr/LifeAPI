#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <limits>
#include <functional>
#include <queue>

#include "../Component.hpp"
#include "../ComponentTemplate.hpp"
#include "ComponentDatabase.hpp"
#include "CLI11/include/CLI/CLI.hpp"

class TransferSynthesis {
public:
  // Structure to hold a synthesis result
  struct SynthesisResult {
    Component component;
    std::string precursorApgcode;
    std::string targetApgcode;
    bool valid;
    
    SynthesisResult() : valid(false) {}
    SynthesisResult(const Component& comp, const std::string& precursor, const std::string& target)
      : component(comp), precursorApgcode(precursor), targetApgcode(target), valid(true) {}
  };

  // Find all possible synthesis steps for a given pattern
  static std::vector<SynthesisResult> FindSynthesisSteps(
      const LifeState& targetPattern,
      const std::vector<ComponentTemplate>& templates,
      const std::unordered_map<std::string, SearchResult>& minPaths,
      int maxPrecursorPop = std::numeric_limits<int>::max());

  // Filter target objects, optionally skipping those with existing synthesis paths
  static std::vector<std::string> FilterTargetObjects(
      const std::vector<std::string>& objects,
      const std::unordered_map<std::string, SearchResult>& minPaths,
      bool skipExisting = false);

  // Check if a synthesis result is promising (filters out hopeless cases like distant blocks/tubs)
  static bool IsPromising(const SynthesisResult& synthesis);


  static void RunSynthesis(
      const std::vector<std::string> &transferComponentFiles,
      const std::vector<std::string> &objects,
      const std::unordered_map<std::string, SearchResult> &minPaths,
      int maxDepth = 1,
      int maxPrecursorPop = 30,
      bool skipExisting = false,
      bool acceptFirst = false);
};

std::vector<TransferSynthesis::SynthesisResult> TransferSynthesis::FindSynthesisSteps(
    const LifeState& targetPattern,
    const std::vector<ComponentTemplate>& templates,
    const std::unordered_map<std::string, SearchResult>& minPaths,
    int maxPrecursorPop) {
  
  std::vector<SynthesisResult> results;
  std::string targetApgcode = targetPattern.EncodeApgcode();
  
  for (const ComponentTemplate& templ : templates) {
    LifeState matches = templ.MatchReverse(targetPattern);
    
    for (auto [x, y] : matches.OnCells()) {
      try {
        LifeState transformedBase = templ.base.Moved(x, y);
        LifeState transformedOut = templ.out.Moved(x, y);
        
        Component resultComp;
        resultComp.base = (targetPattern & ~transformedOut) | transformedBase;
        resultComp.gliderSet = templ.gliderSet.Moved({x, y});
        resultComp.out = targetPattern;
        
        if (!resultComp.SanityCheck()) continue;

        if (resultComp.base.GetPop() > maxPrecursorPop)
          continue;

        std::string precursorApgcode = resultComp.base.EncodeApgcode();

        auto outputIt = minPaths.find(targetApgcode);
        bool newOutput = outputIt == minPaths.end();
        // if (newOutput) continue;

        auto inputIt = minPaths.find(precursorApgcode);
        if (inputIt == minPaths.end()) continue;
        if (!newOutput && inputIt->second.cost + resultComp.Cost() >= outputIt->second.cost) continue;
        
        results.emplace_back(resultComp, precursorApgcode, targetApgcode);
      } catch (const std::exception&) {
        continue;
      }
    }
  }
  
  return results;
}

std::vector<std::string> TransferSynthesis::FilterTargetObjects(
    const std::vector<std::string>& objects,
    const std::unordered_map<std::string, SearchResult>& minPaths,
    bool skipExisting) {
  
  // Filter objects to only xs patterns
  std::vector<std::string> xsObjects;
  for (const auto& obj : objects) {
    if (obj.substr(0, 2) == "xs") {
      xsObjects.push_back(obj);
    }
  }
  
  // If skipExisting is enabled, filter out objects that already have complete synthesis paths
  if (skipExisting) {
    std::vector<std::string> filteredObjects;
    for (const auto& obj : xsObjects) {
      if (minPaths.find(obj) == minPaths.end()) {
        filteredObjects.push_back(obj); // Keep only objects without synthesis paths
      }
    }
    size_t originalCount = xsObjects.size();
    xsObjects = std::move(filteredObjects);
    std::cerr << "Filtered from " << originalCount << " to " << xsObjects.size() 
              << " objects without complete synthesis paths" << std::endl;
  }
  
  return xsObjects;
}

bool TransferSynthesis::IsPromising(const SynthesisResult& synthesis) {
  LifeState precursor = synthesis.component.base;
  auto components = precursor.StillComponents();
  
  // Single component is always promising
  if (components.size() <= 1) {
    return true;
  }
  
  // Filter out patterns with very small components (blocks, etc.)
  for (const auto& component : components) {
    if (component.GetPop() <= 4) {
      return false;
    }
  }
  
  // Bounding box strategy: check if components are too distant
  auto originalBounds = precursor.XYBounds();
  int originalWidth = originalBounds[2] - originalBounds[0];
  int originalHeight = originalBounds[3] - originalBounds[1];
  int originalArea = originalWidth * originalHeight;

  // Density check: if pattern is too sparse overall, likely not promising
  int population = precursor.GetPop();
  double density = (double)population / originalArea;
  if (density < 0.1) {
    return false;
  }

  for (const auto& component : components) {
    LifeState remaining = precursor & ~component;
    if (remaining.IsEmpty()) continue;
    
    auto newBounds = remaining.XYBounds();
    int newWidth = newBounds[2] - newBounds[0];
    int newHeight = newBounds[3] - newBounds[1];
    int newArea = newWidth * newHeight;
    
    double areaShrinkage = 1.0 - (double)newArea / originalArea;
    if (areaShrinkage > 0.3) {
      return false;
    }
  }

  return true;
}


void TransferSynthesis::RunSynthesis(
    const std::vector<std::string> &transferComponentFiles,
    const std::vector<std::string> &objects,
    const std::unordered_map<std::string, SearchResult> &minPaths,
    int maxDepth,
    int maxPrecursorPop,
    bool skipExisting,
    bool acceptFirst) {

  // Load component templates
  ComponentDatabase db;
  db.LoadFromFiles(transferComponentFiles, true);
  std::vector<ComponentTemplate> templates = db.LoadComponentTemplates(true);

  // Filter target objects
  std::vector<std::string> filteredObjects = FilterTargetObjects(objects, minPaths, skipExisting);

  // Build mini synthesis database using ComponentDatabase
  ComponentDatabase miniDb;
  
  // Process each target object
  std::cerr << "Starting synthesis search (max depth: " << maxDepth 
            << ", max precursor pop: " << maxPrecursorPop << ")" << std::endl;
  std::cerr << "Processing " << filteredObjects.size() << " target objects" << std::endl;
  
  int successCount = 0;
  int improvedCount = 0;
  
  for (const auto& target : filteredObjects) {
    // Determine target cost based on existing synthesis
    unsigned targetCost = std::numeric_limits<unsigned>::max();
    bool hasExisting = false;
    
    auto it = minPaths.find(target);
    if (it != minPaths.end()) {
      targetCost = it->second.cost;
      hasExisting = true;
    }
    
    std::cerr << "Searching for " << target;
    if (hasExisting) {
      std::cerr << " (trying to beat cost " << targetCost << ")...";
    } else {
      std::cerr << " (new synthesis)...";
    }
    std::cerr << std::endl;
    
    // Build mini database for multi-step synthesis
    miniDb.db.clear();
    std::unordered_set<std::string> processed;
    
    // Priority queue: (population, depth, apgcode) - lower population processed first
    using QueueEntry = std::tuple<int, int, std::string>; // population, depth, apgcode
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> searchQueue;
    
    // Get population of target
    int targetPop = 0;
    try {
      LifeState targetPattern = LifeState::DecodeApgcode(target);
      targetPop = targetPattern.GetPop();
    } catch (const std::exception&) {
      targetPop = 999; // Fallback for unparseable patterns
    }
    
    searchQueue.push({targetPop, 0, target});
    
    while (!searchQueue.empty()) {
      auto [population, depth, currentApgcode] = searchQueue.top();
      searchQueue.pop();
      
      if (processed.find(currentApgcode) != processed.end()) {
        continue;
      }
      processed.insert(currentApgcode);
      
      std::cerr << "Processing depth " << depth << ", pop " << population << ": " << currentApgcode << ", remaining in queue: " << searchQueue.size() << std::endl;

      // Skip max-depth patterns since database lookups are now done immediately
      if (depth >= maxDepth)
        continue;
      
      // Check database for early termination (non-max-depth patterns)
      auto it = minPaths.find(currentApgcode);
      if (it != minPaths.end()) {
        // Add to mini database as a seed
        miniDb.db[""].emplace_back(it->second.cost, currentApgcode, ">>" + currentApgcode);
        std::cerr << "Found in database: " << currentApgcode << " cost " << it->second.cost << std::endl;
        
        // If accept-first is enabled and we found a synthesis, stop searching
        if (acceptFirst && currentApgcode != target) {
          std::cerr << "Accept-first enabled: stopping search early" << std::endl;
          break;
        }
      }

      // Find synthesis steps and add to mini database
      try {
        LifeState targetPattern = LifeState::DecodeApgcode(currentApgcode);
        auto orientations = targetPattern.SymmetryOrbit();

        for (const LifeState &pattern : orientations) {
          // If we're at the lowest depth, allow anything to be looked up in the database
          int popLimit = depth == maxDepth - 1 ? std::numeric_limits<unsigned>::max() : maxPrecursorPop;

          auto syntheses = FindSynthesisSteps(pattern, templates, minPaths, popLimit);

          for (auto& synthesis : syntheses) {
            // Filter out unpromising synthesis results
            if (!IsPromising(synthesis)) {
              continue;
            }
            synthesis.component.ShiftToFitTorus();
            // Add synthesis to mini database
            miniDb.db[synthesis.precursorApgcode].emplace_back(
              synthesis.component.Cost(), 
              synthesis.targetApgcode, 
              synthesis.component.Realise().RLE()
            );

            // If we're at max depth, do database lookup immediately
            if (depth + 1 >= maxDepth) {
              if (processed.find(synthesis.precursorApgcode) == processed.end()) {
                auto it = minPaths.find(synthesis.precursorApgcode);
                if (it != minPaths.end()) {
                  // Add directly to mini database as a seed
                  processed.insert(synthesis.precursorApgcode);
                  miniDb.db[""].emplace_back(it->second.cost, synthesis.precursorApgcode, ">>" + synthesis.precursorApgcode);
                  std::cerr << "Found in database at max depth: " << synthesis.precursorApgcode << " cost " << it->second.cost << std::endl;
                  
                  // If accept-first is enabled, stop searching immediately
                  if (acceptFirst) {
                    std::cerr << "Accept-first enabled: stopping search early after max-depth lookup" << std::endl;
                    goto search_complete;
                  }
                }
              }
            } else {
              // Get precursor population for priority queue
              int precursorPop = synthesis.component.base.GetPop();
              
              // Add precursor to search queue with priority based on population
              searchQueue.push({precursorPop, depth + 1, synthesis.precursorApgcode});
            }
          }
        }
      } catch (const std::exception&) {
        continue;
      }
    }
    
    search_complete:
    // Run Dijkstra on mini database to find optimal synthesis
    auto results = miniDb.Dijkstra();
    unsigned foundCost = std::numeric_limits<unsigned>::max();

    auto targetIt = results.find(target);
    if (targetIt != results.end()) {
      foundCost = targetIt->second.cost;

      if (foundCost < targetCost) {
        // Output the synthesis components by following the path
        std::string current = target;
        std::vector<std::string> synthesisPath;
      
        while (!current.empty() && current != "") {
          auto resultIt = results.find(current);
          if (resultIt == results.end() || resultIt->second.predecessor.empty()) break;
        
          synthesisPath.push_back(resultIt->second.componentLine);
          current = resultIt->second.predecessor;
        }
      
        // Output components in synthesis order
        for (auto it = synthesisPath.rbegin(); it != synthesisPath.rend(); ++it) {
          std::cout << *it << std::endl;
        }
      }

    }
    
    if (foundCost != std::numeric_limits<unsigned>::max()) {
      successCount++;
      if (hasExisting && foundCost < targetCost) {
        improvedCount++;
        std::cerr << "✓ Improved synthesis for " << target 
                  << " (cost " << targetCost << " → " << foundCost << ")" << std::endl;
      } else if (!hasExisting) {
        std::cerr << "✓ Found new synthesis for " << target 
                  << " (cost " << foundCost << ")" << std::endl;
      } else {
        std::cerr << "✗ No improvement found for " << target
                  << " (keeping existing cost " << targetCost << ")" << std::endl;
      }
    } else {
      std::cerr << "✗ No synthesis found for " << target << std::endl;
    }
  }
  
  std::cerr << "Synthesis complete: " << successCount << "/" << filteredObjects.size() 
            << " targets processed";
  if (improvedCount > 0) {
    std::cerr << " (" << improvedCount << " improved)";
  }
  std::cerr << std::endl;
}


std::vector<std::string> readTargetFile(const std::string& filename) {
    std::vector<std::string> targets;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        throw std::runtime_error("Could not open target file: " + filename);
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
        
        if (!line.empty()) {
            targets.push_back(line);
        }
    }
    
    return targets;
}

std::vector<std::string> getMostExpensiveTargets(const std::unordered_map<std::string, SearchResult>& dijkstraResults, int countPerClass, int maxPopulation, int specificPopulation, bool includePseudo, bool verbose) {
    // Group patterns by population, tracking their Dijkstra cost
    std::unordered_map<int, std::vector<std::pair<unsigned, std::string>>> byPopulation;
    
    for (const auto& [apgcode, searchResult] : dijkstraResults) {
        if (apgcode.empty() || apgcode.substr(0, 2) != "xs") continue;
        
        // Extract population from apgcode (e.g., "xs19_..." -> 19)
        size_t underscorePos = apgcode.find('_');
        if (underscorePos == std::string::npos) continue;
        
        std::string populationStr = apgcode.substr(2, underscorePos - 2);
        int population;
        try {
            population = std::stoi(populationStr);
        } catch (const std::exception&) {
            continue;
        }
        
        // Filter by population based on mode
        if (specificPopulation >= 0) {
            // Specific population mode: only include exact match
            if (population != specificPopulation) continue;
        } else {
            // Maximum population mode: skip populations above the maximum
            if (population > maxPopulation) continue;
        }
        
        // Filter pseudo still lifes unless explicitly included
        if (!includePseudo) {
            try {
                LifeState pattern = LifeState::DecodeApgcode(apgcode);
                if (pattern.IsPseudoStillLife()) {
                    continue; // Skip pseudo still lifes
                }
            } catch (const std::exception&) {
                // If we can't decode, assume it's not pseudo and include it
            }
        }
        
        // Use Dijkstra cost (optimal synthesis cost)
        unsigned cost = static_cast<unsigned>(searchResult.cost);
        if (cost != std::numeric_limits<unsigned>::max()) {
            byPopulation[population].emplace_back(cost, apgcode);
        }
    }
    
    std::vector<std::string> result;
    
    // For each population class, sort by cost (descending) and take top N
    for (auto& [population, patterns] : byPopulation) {
        if (verbose) {
            std::cerr << "Population " << population << ": " << patterns.size() << " patterns" << std::endl;
        }
        
        // Sort by cost (descending - most expensive first)
        std::sort(patterns.begin(), patterns.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });
        
        // Take top countPerClass
        int count = std::min(countPerClass, static_cast<int>(patterns.size()));
        for (int i = 0; i < count; i++) {
            result.push_back(patterns[i].second);
        }
        
        if (verbose) {
            std::cerr << "Selected " << count << " most expensive patterns from population " << population;
            if (count > 0) {
                std::cerr << " (costs " << patterns[count-1].first << " to " << patterns[0].first << ")";
            }
            std::cerr << std::endl;
        }
    }
    
    if (verbose) {
        std::string pseudoNote = includePseudo ? "" : ", excluding pseudo still lifes";
        if (specificPopulation >= 0) {
            std::cerr << "Total selected: " << result.size() << " patterns (population = " << specificPopulation << pseudoNote << ")" << std::endl;
        } else {
            std::cerr << "Total selected: " << result.size() << " patterns (populations <= " << maxPopulation << pseudoNote << ")" << std::endl;
        }
    }
    
    return result;
}

int main(int argc, char* argv[]) {
    CLI::App app{"Transfer synthesis tool"};
    
    // Basic arguments
    std::string transferPath;

    app.add_option("transfer_path", transferPath, "Directory or file containing .sjk files for transfer templates")
        ->required();

    // Options
    bool verbose = false;
    app.add_flag("-v,--verbose", verbose, "Enable verbose output");
    
    // Cost database option
    std::string costPath;
    auto cost_opt = app.add_option("--cost-db", costPath, 
        "Directory or file containing cost database (defaults to transfer_path)");
    
    // Skip existing option
    bool skipExisting = false;
    app.add_flag("--skip-existing", skipExisting, 
        "Skip targets that already have complete synthesis paths");
    
    // Search options
    int maxDepth = 1;
    app.add_option("--max-depth", maxDepth, "Maximum search depth (1 = single-step synthesis, >1 = multi-step)")
        ->default_val(1);
    
    int maxPrecursorPop = 30;
    app.add_option("--max-precursor-pop", maxPrecursorPop, "Maximum population of precursor patterns")
        ->default_val(30);
    
    // Mutually exclusive target selection
    auto target_group = app.add_option_group("target_selection", "Target selection (exactly one required)");
    
    std::string targetFile;
    target_group->add_option("--target-file", targetFile,
        "File containing target apgcodes (one per line)");
    
    int expensiveCount = 1000;
    auto expensive_opt = target_group->add_option("--most-expensive", expensiveCount,
        "Target the N most expensive syntheses of each population class")
        ->default_val(1000);
    
    // Population selection for most-expensive mode
    int maxPopulation = 60;
    auto max_pop_opt = app.add_option("--max-population", maxPopulation, 
        "Maximum population to consider (only with --most-expensive)")
        ->default_val(60);
    
    int specificPopulation = -1;
    auto specific_pop_opt = app.add_option("--population", specificPopulation, 
        "Target specific population only (only with --most-expensive)");
    
    // Both population options need --most-expensive and are mutually exclusive
    max_pop_opt->needs(expensive_opt);
    specific_pop_opt->needs(expensive_opt);
    max_pop_opt->excludes(specific_pop_opt);
    specific_pop_opt->excludes(max_pop_opt);
    
    // Option to include pseudo still lifes (excluded by default in --most-expensive)
    bool includePseudo = false;
    app.add_flag("--include-pseudo", includePseudo, 
        "Include pseudo still lifes as targets (only with --most-expensive)")
        ->needs(expensive_opt);
    
    // Option to accept first synthesis found instead of searching for optimal
    bool acceptFirst = skipExisting;
    app.add_flag("--accept-first", acceptFirst, 
        "Stop search as soon as any synthesis is found");
    
    // Make exactly one target selection required
    target_group->require_option(1);

    // Add validation: --most-expensive requires exactly one population option
    app.callback([&]() {
        if (*expensive_opt) {
            if (!(*max_pop_opt || *specific_pop_opt)) {
                throw CLI::ValidationError("--most-expensive requires either --max-population or --population");
            }
        }
    });
    
    // Parse command line
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        return app.exit(e);
    }
    
    try {
        // Get list of transfer component files
        std::vector<std::string> transferComponentFiles = GetSJKFiles(transferPath);
        
        if (transferComponentFiles.empty()) {
            std::cerr << "Error: No .sjk files found in transfer path " << transferPath << std::endl;
            return 1;
        }
        
        if (verbose) {
            std::cout << "Found " << transferComponentFiles.size() << " transfer component files" << std::endl;
        }
        
        // Set up cost database (use specified path or default to transfer path)
        std::string actualCostPath = *cost_opt ? costPath : transferPath;
        
        if (verbose) {
            std::cerr << "Running Dijkstra's algorithm on " << actualCostPath << "..." << std::endl;
        }
        
        std::vector<std::string> costComponentFiles = GetSJKFiles(actualCostPath);
        if (costComponentFiles.empty()) {
            std::cerr << "Error: No .sjk files found in cost path " << actualCostPath << std::endl;
            return 1;
        }
        
        ComponentDatabase costDb;
        costDb.LoadFromFiles(costComponentFiles, verbose);
        std::unordered_map<std::string, SearchResult> dijkstraResults = costDb.Dijkstra();
        
        if (verbose) {
            std::cerr << "Dijkstra found paths to " << dijkstraResults.size() << " objects" << std::endl;
        }

        // Get target objects based on mode
        std::vector<std::string> targets;
        if (*expensive_opt) {
            if (verbose) {
                std::cerr << "Finding most expensive targets from Dijkstra results..." << std::endl;
            }
            
            targets = getMostExpensiveTargets(dijkstraResults, expensiveCount, maxPopulation, specificPopulation, includePseudo, verbose);
        } else {
            // Read targets from file
            targets = readTargetFile(targetFile);
        }
        
        if (targets.empty()) {
            std::cerr << "Error: No targets found" << std::endl;
            return 1;
        }
        
        if (verbose) {
            std::cerr << "Loaded " << targets.size() << " target objects" << std::endl;
        }
        
        // Run synthesis
        if (verbose) {
            std::cout << "Starting synthesis..." << std::endl;
        }

        TransferSynthesis::RunSynthesis(
            transferComponentFiles,
            targets,
            dijkstraResults,
            maxDepth,
            maxPrecursorPop,
            skipExisting,
            acceptFirst
        );

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
