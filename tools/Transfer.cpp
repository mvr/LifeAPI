#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <limits>

#include "../Component.hpp"
#include "../ComponentTemplate.hpp"
#include "ComponentDatabase.hpp"
#include "CLI11/include/CLI/CLI.hpp"

class TransferSynthesis {
public:
  static std::vector<ComponentTemplate>
  LoadComponentTemplates(const std::vector<std::string> &filePaths,
                         bool verbose = false);


  static std::unordered_set<std::string> ApplyTemplates(
      const std::vector<LifeState> &patterns,
      const std::vector<ComponentTemplate> &templates,
      const std::unordered_map<std::string, SearchResult> *minPaths = nullptr);

  static void SynthesiseThings(
      const std::vector<std::string> &transferComponentFiles,
      const std::vector<std::string> &objects, const std::string &outfile,
      int chunkSize = 64,
      const std::unordered_map<std::string, SearchResult> *minPaths = nullptr);
};


std::vector<ComponentTemplate> TransferSynthesis::LoadComponentTemplates(
    const std::vector<std::string> &filePaths, bool verbose) {
  ComponentDatabase db;
  db.LoadFromFiles(filePaths, verbose);

  if(verbose)
    std::cerr << "Extracting templates from components" << std::endl;
  unsigned total_components = 0;

  std::unordered_map<uint64_t, std::pair<ComponentTemplate, unsigned>> uniqueTemplates;

  for (const auto &[inputApgcode, components] : db.db) {
    if (inputApgcode == "") continue;
    if (inputApgcode.substr(0, 2) != "xs") continue;

    // LifeState inputState = LifeState::DecodeApgcode(inputApgcode);
    // if (inputState.StillComponents().size() > 1) {
    //   if(verbose)
    //     std::cerr << "Skipping components with disconnected input " << inputApgcode << std::endl;
    //   continue;
    // }

    for (const auto& [cost, outputApgcode, componentLine] : components) {
      if (outputApgcode == "") continue;
      if (outputApgcode == inputApgcode) continue;
      if (outputApgcode.substr(0, 2) != "xs") continue;

      try {
        Component comp = Component::FromSJK(componentLine);

        // We can't handle pure cleanup steps
        if ((comp.out & ~comp.base).IsEmpty())
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
        
        // Only add/replace if this is a lower cost template
        if (it == uniqueTemplates.end() || cost < it->second.second) {
          // std::cout << componentLine << std::endl;
          // std::cout << canonicalTempl.RLE() << std::endl;
          uniqueTemplates[minHash] = std::make_pair(canonicalTempl, cost);
        }
      } catch (const std::exception&) {
        // Skip invalid components
        continue;
      }

      total_components++;
    }
  }

  // Convert to vector
  std::vector<ComponentTemplate> result;
  result.reserve(uniqueTemplates.size());
  for (const auto& [hash, templatePair] : uniqueTemplates) {
    result.push_back(templatePair.first);
  }

  if (verbose) {
    std::cerr << "Loaded " << total_components << " total components" << std::endl;
    std::cerr << "Loaded " << result.size() << " templates" << std::endl;
  }

  return result;
}

std::unordered_set<std::string> TransferSynthesis::ApplyTemplates(
    const std::vector<LifeState> &patterns,
    const std::vector<ComponentTemplate> &templates,
    const std::unordered_map<std::string, SearchResult> *minPaths) {

  std::unordered_set<std::string> solutions;

  for (const LifeState &pattern : patterns) {
    // std::cout << "Pattern " << pattern << std::endl;
    for (const ComponentTemplate& templ : templates) {
      // std::cout << "Template " << templ.RLE() << std::endl;
      LifeState matches = templ.MatchReverse(pattern);

      for (auto [x, y] : matches.OnCells()) {
        try {
          LifeState transformedBase = templ.base.Moved(x, y);
          LifeState transformedOut = templ.out.Moved(x, y);

          Component resultComp;
          resultComp.base = (pattern & ~transformedOut) | transformedBase;
          resultComp.gliderSet = templ.gliderSet.Moved({x, y});
          resultComp.out = pattern;

          if (minPaths != nullptr) {
            std::string inputApgcode = resultComp.base.EncodeApgcode();
            std::string outputApgcode = resultComp.out.EncodeApgcode();

            auto outputIt = minPaths->find(outputApgcode);
            if (outputIt == minPaths->end()) continue; // Ideally this wouldn't happen, but the torus wrap can cause problems for large outputs
            auto inputIt = minPaths->find(inputApgcode);
            if (inputIt == minPaths->end()) continue;
            if (inputIt->second.cost + resultComp.Cost() >= outputIt->second.cost) continue;
          }

          // TODO: There should be a better way to do this
          if (resultComp.Realise().Stepped(200) != resultComp.out)
            continue;

          // Rewind in steps of 16 until all salvos are outside the base pattern bounding box
          auto [minX, minY, maxX, maxY] = resultComp.base.XYBounds();
          
          GliderSet rewoundGliders = resultComp.gliderSet;
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
          rewoundComp.base = resultComp.base;
          rewoundComp.gliderSet = rewoundGliders;
          rewoundComp.out = resultComp.out;
            
          if (rewoundComp.Realise().Stepped(totalRewind) != resultComp.Realise())
            continue;


          std::string compStr = resultComp.ToSJK();
          // std::cerr << templ.RLE() << std::endl;
          // std::cerr << templ.GetHash() << std::endl;
          // {
          //   // Calculate hash for all orientations and use the minimum
          //   ComponentTemplate canonicalTempl;

          //   using enum SymmetryTransform;
          //   for (auto transform :
          //          {Identity, ReflectAcrossX, ReflectAcrossYeqX, ReflectAcrossY,
          //           ReflectAcrossYeqNegXP1, Rotate90, Rotate270, Rotate180OddBoth}) {
          //     ComponentTemplate transformedTempl = templ.Transformed(transform);
          //     transformedTempl.NormalisePosition();
          //     std::cerr << transformedTempl.RLE() << std::endl;
          //     std::cerr << transformedTempl.count.bit0 << std::endl;
          //     std::cerr << transformedTempl.count.bit1 << std::endl;
          //     std::cerr << transformedTempl.count.bit2 << std::endl;
          //     std::cerr << transformedTempl.GetHash() << std::endl;
          //   }
          // }
          // std::cerr << resultComp.Realise() << std::endl;
          std::cout << resultComp.Realise() << std::endl;
          solutions.insert(compStr);

        } catch (const std::exception&) {
          continue;
        }
      }
    }
  }

  return solutions;
}

void TransferSynthesis::SynthesiseThings(
    const std::vector<std::string> &transferComponentFiles,
    const std::vector<std::string> &objects, const std::string &outfile,
    int chunkSize,
    const std::unordered_map<std::string, SearchResult> *minPaths) {

  // Load component templates for transfer (these are the ones we'll try to apply)
  std::vector<ComponentTemplate> templates = LoadComponentTemplates(transferComponentFiles, true);

  // Filter objects to only xs patterns
  std::vector<std::string> xsObjects;
  for (const auto& obj : objects) {
    if (obj.substr(0, 2) == "xs") {
      xsObjects.push_back(obj);
    }
  }

  // Generate all orientations
  std::vector<LifeState> allPatterns;
  for (const auto& target : xsObjects) {
    auto orientations = LifeState::DecodeApgcode(target).SymmetryOrbit();
    allPatterns.insert(allPatterns.end(), orientations.begin(), orientations.end());
  }

  std::cerr << "Processing " << allPatterns.size() << " patterns" << std::endl;

  std::ofstream outStream(outfile);

  for (size_t i = 0; i < allPatterns.size(); i += chunkSize) {
    size_t j = std::min(i + chunkSize, allPatterns.size());

    std::vector<LifeState> chunk(allPatterns.begin() + i, allPatterns.begin() + j);
    auto solutions = ApplyTemplates(chunk, templates, minPaths);

    for (const auto& solution : solutions) {
      outStream << solution << std::endl;
    }

    std::cerr << "" << j << " patterns complete" << std::endl;
  }
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

std::vector<std::string> getMostExpensiveTargets(const ComponentDatabase& db, int countPerClass, int maxPopulation, bool verbose) {
    // Group patterns by population, tracking their minimum cost
    std::unordered_map<int, std::vector<std::pair<unsigned, std::string>>> byPopulation;
    
    for (const auto& [apgcode, components] : db.db) {
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
        
        // Skip populations above the maximum
        if (population > maxPopulation) continue;
        
        // Find minimum cost for this apgcode
        unsigned minCost = std::numeric_limits<unsigned>::max();
        for (const auto& [cost, outputApgcode, componentLine] : components) {
            minCost = std::min(minCost, cost);
        }
        
        if (minCost != std::numeric_limits<unsigned>::max()) {
            byPopulation[population].emplace_back(minCost, apgcode);
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
        std::cerr << "Total selected: " << result.size() << " patterns (populations <= " << maxPopulation << ")" << std::endl;
    }
    
    return result;
}

int main(int argc, char* argv[]) {
    CLI::App app{"Transfer synthesis tool for Conway's Game of Life patterns"};
    
    // Basic arguments
    std::string transferPath;
    std::string outputFile;
    
    app.add_option("transfer_path", transferPath, "Directory or file containing .sjk files for transfer templates")
        ->required();
    app.add_option("output_file", outputFile, "Output file for synthesis results")
        ->required();
    
    // Options
    bool verbose = false;
    app.add_flag("-v,--verbose", verbose, "Enable verbose output");
    
    int chunkSize = 64;
    app.add_option("-c,--chunk-size", chunkSize, "Process patterns in chunks of size N")
        ->default_val(64);
    
    // Dijkstra option
    std::string costPath;
    auto dijkstra_opt = app.add_option("--use-dijkstra", costPath, 
        "Use Dijkstra paths for optimization with cost directory or file");
    
    // Mutually exclusive target selection
    auto target_group = app.add_option_group("target_selection", "Target selection (exactly one required)");
    
    std::string targetFile;
    target_group->add_option("--target-file", targetFile,
        "File containing target apgcodes (one per line)");
    
    int expensiveCount = 1000;
    auto expensive_opt = target_group->add_option("--most-expensive", expensiveCount,
        "Target the N most expensive syntheses of each population class")
        ->default_val(1000);
    
    // Population limit for most-expensive mode
    int maxPopulation = 60;
    app.add_option("--max-population", maxPopulation, 
        "Maximum population to consider (only with --most-expensive)")
        ->default_val(60)
        ->needs(expensive_opt);
    
    // Make exactly one target selection required
    target_group->require_option(1);
    
    // most-expensive requires dijkstra
    expensive_opt->needs(dijkstra_opt);
    
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
        
        // Get target objects based on mode
        std::vector<std::string> targets;
        if (*expensive_opt) {
            // Load cost database to find most expensive targets
            if (verbose) {
                std::cerr << "Loading cost database to find most expensive targets..." << std::endl;
            }
            
            std::vector<std::string> costComponentFiles = GetSJKFiles(costPath);
            if (costComponentFiles.empty()) {
                std::cerr << "Error: No .sjk files found in cost path " << costPath << std::endl;
                return 1;
            }
            
            ComponentDatabase costDb;
            costDb.LoadFromFiles(costComponentFiles, verbose);
            targets = getMostExpensiveTargets(costDb, expensiveCount, maxPopulation, verbose);
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
        
        // Run Dijkstra if requested
        std::unique_ptr<std::unordered_map<std::string, SearchResult>> minPaths;
        if (*dijkstra_opt) {
            if (verbose) {
                std::cerr << "Running Dijkstra's algorithm on " << costPath << "..." << std::endl;
            }
            
            std::vector<std::string> costComponentFiles = GetSJKFiles(costPath);
            if (costComponentFiles.empty()) {
                std::cerr << "Error: No .sjk files found in cost path " << costPath << std::endl;
                return 1;
            }
            
            ComponentDatabase db;
            db.LoadFromFiles(costComponentFiles, verbose);
            auto dijkstraResult = db.Dijkstra();
            minPaths = std::make_unique<std::unordered_map<std::string, SearchResult>>(std::move(dijkstraResult));
            
            if (verbose) {
                std::cerr << "Dijkstra found paths to " << minPaths->size() << " objects" << std::endl;
            }
        }
        
        // Run synthesis
        if (verbose) {
            std::cout << "Starting synthesis..." << std::endl;
        }
        
        TransferSynthesis::SynthesiseThings(
            transferComponentFiles, 
            targets, 
            outputFile, 
            chunkSize, 
            minPaths.get()
        );

        if (verbose)
          std::cerr << "Results written to " << outputFile << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
