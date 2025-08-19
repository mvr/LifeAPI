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
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "../Component.hpp"
#include "../ComponentTemplate.hpp"
#include "ComponentDatabase.hpp"
#include "TemplateCache.hpp"
#include "CLI11/include/CLI/CLI.hpp"

template<typename T>
class ThreadSafeQueue {
private:
    std::priority_queue<T, std::vector<T>, std::greater<T>> queue_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<int> waiting_threads_{0};
    std::atomic<int> active_threads_{0};
    std::atomic<bool> shutdown_{false};

public:
    void push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_) return;
        queue_.push(item);
        condition_.notify_one();
    }

    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        while (true) {
            if (shutdown_) {
                return false;
            }
            
            if (!queue_.empty()) {
                item = queue_.top();
                queue_.pop();
                return true;
            }
            
            // Queue is empty, increment waiting threads
            ++waiting_threads_;
            
            // Check if all threads are waiting (meaning no one is actively processing)
            if (waiting_threads_ == active_threads_) {
                // All threads are waiting and queue is empty - time to shutdown
                shutdown_ = true;
                --waiting_threads_;
                condition_.notify_all();
                return false;
            }
            
            // Wait for either new items or shutdown
            condition_.wait(lock, [this] { 
                return !queue_.empty() || shutdown_; 
            });
            
            --waiting_threads_;
        }
    }
    
    void register_thread() {
        ++active_threads_;
    }
    
    void unregister_thread() {
        --active_threads_;
    }
    
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
    int waiting_threads() const {
        return waiting_threads_.load();
    }
    
    int active_threads() const {
        return active_threads_.load();
    }
    
    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        condition_.notify_all();
    }
};

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
      const LifeState &targetPattern,
      const std::vector<ComponentTemplate> &templates,
      const std::unordered_map<std::string, SearchResult> &minPaths);

  // Filter target objects, optionally skipping those with existing synthesis paths
  static std::vector<std::string> FilterTargetObjects(
      const std::vector<std::string>& objects,
      const std::unordered_map<std::string, SearchResult>& minPaths,
      bool skipExisting = false);

  // Check if a synthesis result is promising (filters out hopeless cases like distant blocks/tubs)
  static bool IsSparse(const LifeState &state);
  static bool IsSparse(const LifeState &state, const std::vector<LifeState> &components);
  static bool IsPromising(const SynthesisResult& synthesis);

  static void RunSynthesis(
      const std::vector<std::string> &transferComponentFiles,
      const std::vector<std::string> &objects,
      const std::unordered_map<std::string, SearchResult> &minPaths,
      int maxDepth = 1,
      int maxPrecursorPop = 30,
      bool skipExisting = false,
      bool acceptFirst = false,
      int numThreads = 1,
      unsigned minTemplateOccurrences = 1,
      bool outputVisitedApgcodes = false,
      const std::string& cacheDir = "./template_cache",
      bool useCache = true);

private:
  using QueueEntry = std::tuple<int, int, std::string>; // population, depth, apgcode
};

std::vector<TransferSynthesis::SynthesisResult> TransferSynthesis::FindSynthesisSteps(
    const LifeState& targetPattern,
    const std::vector<ComponentTemplate>& templates,
    const std::unordered_map<std::string, SearchResult>& minPaths) {

  std::vector<SynthesisResult> results;
  std::string targetApgcode = targetPattern.EncodeApgcode();

  NeighbourCount stateCount(targetPattern);

  for (const ComponentTemplate& templ : templates) {
    LifeState matches = templ.MatchReverse(targetPattern, stateCount);
    
    for (auto [x, y] : matches.OnCells()) {
      try {
        LifeState transformedBase = templ.base.Moved(x, y);
        LifeState transformedOut = templ.out.Moved(x, y);
        
        Component resultComp;
        resultComp.base = (targetPattern & ~transformedOut) | transformedBase;
        resultComp.gliderSet = templ.gliderSet.Moved({x, y});
        resultComp.out = targetPattern;

        resultComp.ShiftToFitTorus();

        if (!resultComp.SanityCheck()) continue;

        std::string precursorApgcode = resultComp.base.EncodeApgcode();

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

bool TransferSynthesis::IsSparse(const LifeState &state) {
  return IsSparse(state, state.StillComponents());
}

bool TransferSynthesis::IsSparse(const LifeState &state, const std::vector<LifeState> &components) {
  // Single component is not sparse
  if (components.size() <= 1) {
    return false;
  }

  // // Bounding box strategy: check if components are too distant
  // auto originalBounds = state.XYBounds();
  // int originalWidth = originalBounds[2] - originalBounds[0];
  // int originalHeight = originalBounds[3] - originalBounds[1];
  // int originalArea = originalWidth * originalHeight;

  // // Density check: if pattern is too sparse overall, likely not promising
  // int population = state.GetPop();
  // double density = (double)population / originalArea;
  // if (density < 0.05) {
  //   return true;
  // }

  LifeState largest;
  unsigned largest_pop = 0;

  for (const auto &component : components) {
    unsigned pop = component.GetPop();
    if (pop > largest_pop) {
      largest = component;
      largest_pop = pop;
    }
  }

  LifeState nearby = state.ComponentContaining(
      state & largest.Convolve(
                    LifeState::ConstantParse("7o$7o$7o$7o$7o$7o$7o!", -3, -3)),
      LifeState::ConstantParse("5o$5o$5o$5o$5o!", -2, -2));

  if (!(state & ~nearby).IsEmpty())
    return true;

  // ALSO OLD
  // auto newBounds = largest.XYBounds();
  // int newWidth = newBounds[2] - newBounds[0];
  // int newHeight = newBounds[3] - newBounds[1];
  // int newArea = newWidth * newHeight;

  // double areaShrinkage = 1.0 - (double)newArea / originalArea;
  // if (areaShrinkage > 0.3) {
  //   return true;
  // }


  // OLD
  // for (const auto& component : components) {
  //   LifeState remaining = state & ~component;
  //   if (remaining.IsEmpty()) continue;

  //   auto newBounds = remaining.XYBounds();
  //   int newWidth = newBounds[2] - newBounds[0];
  //   int newHeight = newBounds[3] - newBounds[1];
  //   int newArea = newWidth * newHeight;

  //   double areaShrinkage = 1.0 - (double)newArea / originalArea;
  //   if (areaShrinkage > 0.4) {
  //     return true;
  //   }
  // }

  return false;
}

bool TransferSynthesis::IsPromising(const SynthesisResult& synthesis) {

  // // Filter out patterns with very small components (blocks, etc.)
  // for (const auto& component : components) {
  //   if (component.GetPop() <= 4) {
  //     std::cerr << "Unpromising small component " << synthesis.precursorApgcode << std::endl;
  //     return false;
  //   }
  // }

  // Pure additions are always fine:
  if ((synthesis.component.base & ~synthesis.component.out).IsEmpty()) {
    return true;
  }

  auto base_components = synthesis.component.base.StillComponents();
  auto out_components = synthesis.component.out.StillComponents();

  if (IsSparse(synthesis.component.base, base_components) &&
      IsSparse(synthesis.component.out, out_components)) {
    // std::cerr << "Unpromising density " << synthesis.precursorApgcode << std::endl;

    return false;
  }

  LifeState base_largest;
  unsigned base_largest_pop = 0;

  for (const auto &component : base_components) {
    unsigned pop = component.GetPop();
    if (pop > base_largest_pop) {
      base_largest = component;
      base_largest_pop = pop;
    }
  }

  // Steps that interact with the largest component without touching
  // any smaller ones are not promising
  if (base_components.size() > 1 && !((synthesis.component.base ^ synthesis.component.out) & synthesis.component.base.ZOI()).IsEmpty()) {
    LifeState smaller_components = synthesis.component.base & ~base_largest;
    if ((smaller_components & ~synthesis.component.out).IsEmpty())
      return false;
  }

  // Steps that just shove a small still life around are not promising
  if((base_largest & ~synthesis.component.out).IsEmpty()) {
    LifeState diff = (synthesis.component.base ^ synthesis.component.out).ZOI();
    LifeState before = synthesis.component.base.ComponentContaining(diff & synthesis.component.base);
    LifeState after = synthesis.component.out.ComponentContaining(diff & synthesis.component.out);

    // bool beforeSmall = before.GetPop() <= 8 && before.Stepped() == before;
    // bool afterSmall = after.GetPop() <= 8 && after.Stepped() == after;

    bool beforeSmall = before.Stepped() == before;
    bool afterSmall = after.Stepped() == after;

    if (beforeSmall && afterSmall)
      return false;
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
    bool acceptFirst,
    int numThreads,
    unsigned minTemplateOccurrences,
    bool outputVisitedApgcodes,
    const std::string& cacheDir,
    bool useCache) {

  // Load component templates using cache
  TemplateCache templateCache;
  std::vector<ComponentTemplate> templates = templateCache.LoadTemplates(
    transferComponentFiles, true, minTemplateOccurrences, cacheDir, useCache);

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
  int targetIndex = 0;
  
  for (const auto& target : filteredObjects) {
    targetIndex++;
    // Determine target cost based on existing synthesis
    unsigned targetCost = std::numeric_limits<unsigned>::max();
    bool hasExisting = false;
    
    auto it = minPaths.find(target);
    if (it != minPaths.end()) {
      targetCost = it->second.cost;
      hasExisting = true;
    }
    
    std::cerr << "[" << targetIndex << "/" << filteredObjects.size() << "] Searching for " << target;
    if (hasExisting) {
      std::cerr << " (trying to beat cost " << targetCost << ")";
    } else {
      std::cerr << " (new synthesis)";
    }
    std::cerr << std::endl;
    
    // Build mini database for multi-step synthesis
    miniDb.db.clear();
    std::unordered_set<std::string> processed;
    
    // Thread-safe queue for multithreaded processing
    ThreadSafeQueue<QueueEntry> searchQueue;
    
    // Mutexes for shared data structures
    std::mutex processedMutex;
    std::mutex miniDbMutex;
    std::mutex cerrMutex;
    std::atomic<bool> shouldStop{false};
    
    // Collection for all queue apgcodes (if enabled)
    std::unordered_set<std::string> allVisitedApgcodes;
    std::mutex visitedApgcodesMutex;
    
    // Get population of target
    int targetPop = 0;
    try {
      LifeState targetPattern = LifeState::DecodeApgcode(target);
      targetPop = targetPattern.GetPop();
    } catch (const std::exception&) {
      continue;
    }
    
    searchQueue.push({targetPop, 0, target});
    
    {
      std::lock_guard<std::mutex> lock(cerrMutex);
      std::cerr << "  Starting search with " << numThreads << " threads" << std::endl;
    }
    
    // Reset process count for this target
    static std::atomic<int> processCount{0};
    processCount.store(0);
    
    // Create worker lambda
    auto workerLambda = [&]() {
      // Register this thread with the queue
      searchQueue.register_thread();
      
      QueueEntry entry;
      while (searchQueue.pop(entry)) {
        if (shouldStop.load()) {
          break;
        }
        
        auto [population, depth, currentApgcode] = entry;

        // Collect apgcode if output is enabled
        if (outputVisitedApgcodes) {
          std::lock_guard<std::mutex> lock(visitedApgcodesMutex);
          allVisitedApgcodes.insert(currentApgcode);
        }

        // Check if already processed
        {
          std::lock_guard<std::mutex> lock(processedMutex);
          if (processed.find(currentApgcode) != processed.end()) {
            continue;
          }
          processed.insert(currentApgcode);
        }
        
        // Only show verbose output for interesting cases or periodically
        int currentCount = ++processCount;
        if (depth < 2 || currentCount % 50 == 0) {
          std::lock_guard<std::mutex> lock(cerrMutex);
          std::string indent(2 + depth, ' '); // Base indent + 1 space per depth level
          std::cerr << indent << "Processing depth " << depth << ", pop " << population << ", queue size " << searchQueue.size() << ": " << currentApgcode << std::endl;
        }

        // Skip max-depth patterns since database lookups are now done immediately
        if (depth >= maxDepth) {
          continue;
        }
        
        // Check database for early termination (non-max-depth patterns)
        auto it = minPaths.find(currentApgcode);
        if (it != minPaths.end()) {
          // Add to mini database as a seed
          {
            std::lock_guard<std::mutex> lock(miniDbMutex);
            miniDb.db[""].emplace_back(it->second.cost, currentApgcode, ">>" + currentApgcode);
          }
          if (it->second.cost + depth <= targetCost) {
            std::lock_guard<std::mutex> lock(cerrMutex);
            std::string indent(2 + depth, ' ');
            std::cerr << indent << "Found in database: " << currentApgcode << " (cost " << it->second.cost << ")" << std::endl;
          }
          
          // If accept-first is enabled and we found a synthesis, stop searching
          if (acceptFirst) {
            {
              std::lock_guard<std::mutex> lock(cerrMutex);
              std::cerr << "  Accept-first: stopping search early" << std::endl;
            }
            shouldStop.store(true);
            searchQueue.shutdown();
            break;
          }
        }

        // Find synthesis steps and add to mini database
        try {
          LifeState targetPattern = LifeState::DecodeApgcode(currentApgcode);
          auto orientations = targetPattern.SymmetryOrbit();

          for (const LifeState &pattern : orientations) {
            auto syntheses = FindSynthesisSteps(pattern, templates, minPaths);

            for (auto& synthesis : syntheses) {
              // If we're at the lowest depth, allow anything to be looked up in the database
              int maxPop = depth == maxDepth - 1 ? std::numeric_limits<int>::max() : maxPrecursorPop;
              if (synthesis.component.base.GetPop() > maxPop)
                continue;

              // Filter out unpromising synthesis results
              if (depth + 1 < maxDepth && !IsPromising(synthesis)) {
                continue;
              }

              synthesis.component.ShiftToFitTorus();

              // Add synthesis to mini database
              {
                std::lock_guard<std::mutex> lock(miniDbMutex);
                miniDb.db[synthesis.precursorApgcode].emplace_back(
                  synthesis.component.Cost(), 
                  synthesis.targetApgcode, 
                  synthesis.component.Realise().RLE()
                );
              }

              // If we're at max depth, do database lookup immediately
              if (depth + 1 >= maxDepth) {
                if (outputVisitedApgcodes) {
                  std::lock_guard<std::mutex> lock(visitedApgcodesMutex);
                  allVisitedApgcodes.insert(synthesis.precursorApgcode);
                }
                bool wasProcessed = false;
                {
                  std::lock_guard<std::mutex> lock(processedMutex);
                  wasProcessed = processed.find(synthesis.precursorApgcode) != processed.end();
                }
                
                if (!wasProcessed) {
                  auto dbIt = minPaths.find(synthesis.precursorApgcode);
                  if (dbIt != minPaths.end()) {
                    // Add directly to mini database as a seed
                    {
                      std::lock_guard<std::mutex> processedLock(processedMutex);
                      processed.insert(synthesis.precursorApgcode);
                      
                      std::lock_guard<std::mutex> dbLock(miniDbMutex);
                      miniDb.db[""].emplace_back(dbIt->second.cost, synthesis.precursorApgcode, ">>" + synthesis.precursorApgcode);
                    }
                    if (dbIt->second.cost + depth <= targetCost) {
                      std::lock_guard<std::mutex> lock(cerrMutex);
                      std::string indent(2 + (depth + 1), ' '); // depth + 1 since this is a precursor
                      std::cerr << indent << "Found at max depth: " << synthesis.precursorApgcode << " (cost " << dbIt->second.cost << ")" << std::endl;
                    }
                    
                    // If accept-first is enabled, stop searching immediately
                    if (acceptFirst) {
                      {
                        std::lock_guard<std::mutex> lock(cerrMutex);
                        std::cerr << "  Accept-first: stopping after max-depth lookup" << std::endl;
                      }
                      shouldStop.store(true);
                      searchQueue.shutdown();
                      break;
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
            
            if (shouldStop.load()) {
              break;
            }
          }
        } catch (const std::exception&) {
          continue;
        }
        
        if (shouldStop.load()) {
          break;
        }
      }
      
      // Unregister this thread from the queue
      searchQueue.unregister_thread();
    };
    
    // Create worker threads
    std::vector<std::thread> workers;
    for (int i = 0; i < numThreads; ++i) {
      workers.emplace_back(workerLambda);
    }
    
    // Wait for all workers to complete
    for (auto& worker : workers) {
      worker.join();
    }
    
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
        std::cerr << "  ✓ Improved: cost " << targetCost << " → " << foundCost << std::endl;
      } else if (!hasExisting) {
        std::cerr << "  ✓ New synthesis: cost " << foundCost << std::endl;
      } else {
        std::cerr << "  = No improvement (cost " << targetCost << ")" << std::endl;
      }
    } else {
      std::cerr << "  ✗ No synthesis found" << std::endl;
    }
    
    if (outputVisitedApgcodes) {
      std::cerr << "Queue apgcodes for " << target << ":" << std::endl;
      for (const auto& apgcode : allVisitedApgcodes) {
        std::cout << apgcode << std::endl;
      }
    }
  }
  
  std::cerr << "Synthesis complete: " << successCount << "/" << filteredObjects.size() 
            << " targets synthesised";
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

std::vector<std::string> getMostExpensiveTargets(const std::unordered_map<std::string, SearchResult>& dijkstraResults, int countPerClass, int minPopulation, int maxPopulation, int specificPopulation, bool includePseudo, bool verbose) {
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
            // Range mode: skip populations outside the range
            if (population < minPopulation || population > maxPopulation) continue;
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
            std::cerr << "Total selected: " << result.size() << " patterns (populations " << minPopulation << "-" << maxPopulation << pseudoNote << ")" << std::endl;
        }
    }
    
    return result;
}

int main(int argc, char* argv[]) {
    CLI::App app{"Stomp: synthesis transfer tool"};
    
    // Basic arguments
    std::string componentsPath;

    app.add_option("components_path", componentsPath, "Directory or file containing .sjk files for component templates")
        ->required();

    // Options
    bool verbose = false;
    app.add_flag("-v,--verbose", verbose, "Enable verbose output");
    
    // Cost database option
    std::string costPath;
    auto cost_opt = app.add_option("--cost-db", costPath, 
        "Directory or file containing cost database (defaults to components_path)");
    
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
    
    int numThreads = 1;
    app.add_option("--threads", numThreads, "Number of worker threads for parallel processing")
        ->default_val(1);
    
    unsigned minTemplateOccurrences = 1;
    app.add_option("--min-template-occurrences", minTemplateOccurrences, "Minimum times a template must occur to be used")
        ->default_val(1);

    bool outputVisitedApgcodes = false;
    app.add_flag("--output-visited-apgcodes", outputVisitedApgcodes, "Output all apgcodes that were ever processed in the search queue");

    // Template caching options
    std::string cacheDir = "./template_cache";
    app.add_option("--cache-dir", cacheDir, "Directory for template cache")
        ->default_val("./template_cache");
    
    bool noCache = false;
    app.add_flag("--no-cache", noCache, "Disable template caching");
    
    bool clearCache = false;
    app.add_flag("--clear-cache", clearCache, "Clear template cache and exit");
    
    // Mutually exclusive target selection
    auto target_group = app.add_option_group("target_selection", "Target selection (exactly one required)");
    
    std::string targetFile;
    target_group->add_option("--target-file", targetFile,
        "File containing target apgcodes (one per line)");
    
    std::string singleTarget;
    target_group->add_option("--target", singleTarget,
        "Single target apgcode to synthesize");
    
    int expensiveCount = 1000;
    auto expensive_opt = target_group->add_option("--most-expensive", expensiveCount,
        "Target the N most expensive syntheses of each population class")
        ->default_val(1000);
    
    // Population selection for most-expensive mode
    int minPopulation = 0;
    auto min_pop_opt = app.add_option("--min-population", minPopulation, 
        "Minimum population to consider (only with --most-expensive)")
        ->default_val(0);
        
    int maxPopulation = 60;
    auto max_pop_opt = app.add_option("--max-population", maxPopulation, 
        "Maximum population to consider (only with --most-expensive)")
        ->default_val(60);
    
    int specificPopulation = -1;
    auto specific_pop_opt = app.add_option("--population", specificPopulation, 
        "Target specific population only (only with --most-expensive)");
    
    // Population options need --most-expensive and are mutually exclusive with --population
    min_pop_opt->needs(expensive_opt);
    max_pop_opt->needs(expensive_opt);
    specific_pop_opt->needs(expensive_opt);
    max_pop_opt->excludes(specific_pop_opt);
    min_pop_opt->excludes(specific_pop_opt);
    specific_pop_opt->excludes(max_pop_opt);
    specific_pop_opt->excludes(min_pop_opt);
    
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

    // Add validation: --most-expensive requires at least one population option
    app.callback([&]() {
        if (*expensive_opt) {
            if (!(*min_pop_opt || *max_pop_opt || *specific_pop_opt)) {
                throw CLI::ValidationError("--most-expensive requires population constraint (--min-population, --max-population, or --population)");
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
        // Handle clear cache option
        if (clearCache) {
            TemplateCache cache;
            if (cache.ClearCache(cacheDir)) {
                std::cerr << "Template cache cleared: " << cacheDir << std::endl;
            } else {
                std::cerr << "Failed to clear template cache: " << cacheDir << std::endl;
                return 1;
            }
            return 0;
        }
        
        // Get list of transfer component files
        std::vector<std::string> transferComponentFiles = GetSJKFiles(componentsPath);
        
        if (transferComponentFiles.empty()) {
            std::cerr << "Error: No .sjk files found in components path " << componentsPath << std::endl;
            return 1;
        }
        
        if (verbose) {
            std::cout << "Found " << transferComponentFiles.size() << " transfer component files" << std::endl;
        }
        
        // Set up cost database (use specified path or default to component path)
        std::string actualCostPath = *cost_opt ? costPath : componentsPath;
        
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
            
            targets = getMostExpensiveTargets(dijkstraResults, expensiveCount, minPopulation, maxPopulation, specificPopulation, includePseudo, verbose);
        } else if (!singleTarget.empty()) {
            // Single target specified
            targets.push_back(singleTarget);
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
            acceptFirst,
            numThreads,
            minTemplateOccurrences,
            outputVisitedApgcodes,
            cacheDir,
            !noCache
        );

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
