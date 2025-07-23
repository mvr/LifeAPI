#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

#include "../Component.hpp"
#include "../ComponentTemplate.hpp"
#include "ComponentDatabase.hpp"

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

      if (matches.GetPop() > 10) {
        std::cout << templ.RLE() << std::endl;
      }

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
            if (outputIt != minPaths->end()) {
              auto inputIt = minPaths->find(inputApgcode);
              if (inputIt == minPaths->end()) continue;
              if (inputIt->second.cost + resultComp.Cost() >= outputIt->second.cost) continue;
            }
          }

          // TODO: There should be a better way to do this
          if (resultComp.Realise().Stepped(200) != resultComp.out)
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

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [options] <transfer_path> <target_file> <output_file>" << std::endl;
    std::cout << "O << std::endlptions:" << std::endl;
    std::cout << "  -h, --help                      Show this help message" << std::endl;
    std::cout << "  -v, --verbose                   Enable verbose output" << std::endl;
    std::cout << "  -c, --chunk-size N              Process patterns in chunks of size N (default: 64)" << std::endl;
    std::cout << "  --use-dijkstra <cost_path>      Use Dijkstra paths for optimization with cost directory or file" << std::endl;
    std::cout << "A << std::endlrguments:" << std::endl;
    std::cout << "  transfer_path                   Directory or file containing .sjk files for transfer templates" << std::endl;
    std::cout << "  target_file                     File containing target apgcodes (one per line)" << std::endl;
    std::cout << "  output_file                     Output file for synthesis results" << std::endl;
    std::cout << "E << std::endlxample:" << std::endl;
    std::cout << "  " << progName << " Shinjuku/shinjuku/comp targets.txt output.sjk" << std::endl;
    std::cout << "  " << progName << " --use-dijkstra Shinjuku/shinjuku/comp transfer_comp targets.txt output.sjk" << std::endl;
    std::cout << "  " << progName << " --use-dijkstra single_cost.sjk single_transfer.sjk targets.txt output.sjk" << std::endl;
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

int main(int argc, char* argv[]) {
    std::string transferPath;
    std::string targetFile;
    std::string outputFile;
    std::string costPath;
    bool verbose = false;
    bool useDijkstra = false;
    int chunkSize = 64;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-c" || arg == "--chunk-size") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --chunk-size requires a value" << std::endl;
                return 1;
            }
            chunkSize = std::stoi(argv[++i]);
        } else if (arg == "--use-dijkstra") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --use-dijkstra requires a cost directory" << std::endl;
                return 1;
            }
            useDijkstra = true;
            costPath = argv[++i];
        } else if (arg[0] == '-') {
            std::cerr << "Error: Unknown option " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        } else {
            // Positional arguments
            if (transferPath.empty()) {
                transferPath = arg;
            } else if (targetFile.empty()) {
                targetFile = arg;
            } else if (outputFile.empty()) {
                outputFile = arg;
            } else {
                std::cerr << "Error: Too many arguments" << std::endl;
                printUsage(argv[0]);
                return 1;
            }
        }
    }
    
    if (transferPath.empty() || targetFile.empty() || outputFile.empty()) {
        std::cerr << "Error: Missing required arguments" << std::endl;
        printUsage(argv[0]);
        return 1;
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
        
        // Read target objects
        std::vector<std::string> targets = readTargetFile(targetFile);
        if (targets.empty()) {
            std::cerr << "Error: No targets found in " << targetFile << std::endl;
            return 1;
        }
        
        if (verbose) {
            std::cerr << "Loaded " << targets.size() << " target objects" << std::endl;
        }
        
        // Run Dijkstra if requested
        std::unique_ptr<std::unordered_map<std::string, SearchResult>> minPaths;
        if (useDijkstra) {
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
