#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <iomanip>

#include "../Component.hpp"
#include "../ComponentTemplate.hpp"
#include "ComponentDatabase.hpp"

class TemplateCache {
public:
    // Main interface - load templates with per-file caching
    std::vector<ComponentTemplate> LoadTemplates(
        const std::vector<std::string>& filePaths,
        bool verbose = false,
        const std::string& cacheDir = "./template_cache",
        bool useCache = true,
        const std::unordered_map<std::string, SearchResult>* costDb = nullptr);
    
    // Cache management
    bool ClearCache(const std::string& cacheDir = "./template_cache");
    
    // Filter function to determine if a component is useful for synthesis
    static bool IsUsefulComponent(const Component& comp);

    // Per-file template extraction
    std::vector<ComponentTemplate> ExtractTemplatesFromFile(
        const std::string& filePath,
        bool verbose = false,
        const std::unordered_map<std::string, SearchResult>* costDb = nullptr);
    
    // Load templates from a single file with caching
    std::vector<ComponentTemplate> LoadTemplatesFromFile(
        const std::string& filePath,
        const std::string& cacheDir,
        bool useCache,
        bool verbose = false,
        const std::unordered_map<std::string, SearchResult>* costDb = nullptr);
    
    // Combine and deduplicate templates from multiple files
    static std::vector<ComponentTemplate> CombineAndDeduplicateTemplates(
        std::vector<ComponentTemplate> templates,
        unsigned minOccurrences = 1,
        bool verbose = false,
        bool pruneGenerality = false);

    // Canonicalise a template by trying all symmetries, returning the minimising transform and offset
    static std::tuple<ComponentTemplate, uint64_t, SymmetryTransform, int, int> CanonicaliseTemplate(const ComponentTemplate& templ);
    
    // Cache helper methods
    std::string GenerateCacheKeyForFile(const std::string& filePath);
    bool LoadTemplatesFromCache(const std::string& cacheFile, std::vector<ComponentTemplate>& templates);
    void SaveTemplatesToCache(const std::string& cacheFile, const std::vector<ComponentTemplate>& templates);
    
    // Component loading from single file
    std::vector<Component> LoadComponentsFromFile(const std::string& filePath,
                                                  bool verbose = false,
                                                  const std::unordered_map<std::string, SearchResult>* costDb = nullptr);
};

// Implementation

std::vector<ComponentTemplate> TemplateCache::LoadTemplates(
    const std::vector<std::string>& filePaths,
    bool verbose,
    const std::string& cacheDir,
    bool useCache,
    const std::unordered_map<std::string, SearchResult>* costDb) {

    if (!useCache) {
        if (verbose) {
            std::cerr << "Cache disabled, loading templates directly from all files" << std::endl;
        }
        // Load templates from each file without caching
        std::vector<ComponentTemplate> combined;
        for (const auto& filePath : filePaths) {
            std::vector<ComponentTemplate> fileTemplates = ExtractTemplatesFromFile(filePath, verbose, costDb);
            combined.insert(combined.end(),
                            std::make_move_iterator(fileTemplates.begin()),
                            std::make_move_iterator(fileTemplates.end()));
        }
        return combined;
    }
    
    // Create cache directory if it doesn't exist
    std::filesystem::create_directories(cacheDir);
    
    if (verbose) {
        std::cerr << "Loading templates with per-file caching from " << filePaths.size() << " files" << std::endl;
    }
    
    // Load templates from each file individually (with per-file caching)
    std::vector<std::vector<ComponentTemplate>> allFileTemplates;
    for (const auto& filePath : filePaths) {
        std::vector<ComponentTemplate> fileTemplates = LoadTemplatesFromFile(filePath, cacheDir, useCache, verbose, costDb);
        allFileTemplates.push_back(fileTemplates);
    }
    
    // Combine across all files (dedup later)
    std::vector<ComponentTemplate> combined;
    for (auto& vec : allFileTemplates) {
        combined.insert(combined.end(),
                        std::make_move_iterator(vec.begin()),
                        std::make_move_iterator(vec.end()));
    }
    return combined;
}

bool TemplateCache::ClearCache(const std::string& cacheDir) {
    try {
        if (std::filesystem::exists(cacheDir)) {
            std::filesystem::remove_all(cacheDir);
            return true;
        }
        return true; // Already cleared
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<ComponentTemplate> TemplateCache::ExtractTemplatesFromFile(
    const std::string& filePath,
    bool verbose,
    const std::unordered_map<std::string, SearchResult>* costDb) {
    
    if (verbose) {
        std::cerr << "Extracting templates from " << filePath << std::endl;
    }
    
    // Load components from single file
    std::vector<Component> components = LoadComponentsFromFile(filePath, verbose, costDb);
    
    // Extract unique templates (no occurrence filtering at file level)
    std::unordered_map<uint64_t, ComponentTemplate> uniqueTemplates;
    
    for (const Component& comp : components) {
        try {
            if (!IsUsefulComponent(comp)) {
                if (verbose) {
                    std::cerr << "Skipping non-useful component: " << comp.Realise() << std::endl;
                }
                continue;
            }
            
            ComponentTemplate templ = ComponentTemplate::FromComponent(comp);
            uniqueTemplates[templ.GetHash()] = templ;

        } catch (const std::exception& e) {
            if (verbose) {
                std::cerr << "Error processing component: " << e.what() << std::endl;
            }
        }
    }
    
    // Convert to vector
    std::vector<ComponentTemplate> result;
    for (const auto& [hash, templ] : uniqueTemplates) {
        result.push_back(templ);
    }
    
    if (verbose) {
        std::cerr << "Extracted " << result.size() << " unique templates from " 
                  << components.size() << " components in " << filePath << std::endl;
    }
    
    return result;
}

std::vector<ComponentTemplate> TemplateCache::LoadTemplatesFromFile(
    const std::string& filePath,
    const std::string& cacheDir,
    bool useCache,
    bool verbose,
    const std::unordered_map<std::string, SearchResult>* costDb) {
    
    if (!useCache) {
        return ExtractTemplatesFromFile(filePath, verbose, costDb);
    }
    
    std::string cacheKey = GenerateCacheKeyForFile(filePath);
    std::string cacheFile = cacheDir + "/file_" + cacheKey + ".bin";
    
    std::vector<ComponentTemplate> templates;
    if (std::filesystem::exists(cacheFile)) {
        if (verbose) {
            std::cerr << "Loading templates from cache for " << filePath << std::endl;
        }
        
        if (LoadTemplatesFromCache(cacheFile, templates)) {
            if (verbose) {
                std::cerr << "Loaded " << templates.size() << " cached templates from " << filePath << std::endl;
            }
            return templates;
        } else {
            if (verbose) {
                std::cerr << "Cache load failed for " << filePath << ", regenerating" << std::endl;
            }
        }
    } else if (verbose) {
        std::cerr << "No cache found for " << filePath << ", generating templates" << std::endl;
    }
    
    templates = ExtractTemplatesFromFile(filePath, verbose, costDb);
    
    if (verbose) {
        std::cerr << "Caching " << templates.size() << " templates for " << filePath << std::endl;
    }
    SaveTemplatesToCache(cacheFile, templates);
    
    return templates;
}

std::vector<ComponentTemplate> TemplateCache::CombineAndDeduplicateTemplates(
    std::vector<ComponentTemplate> templates,
    unsigned minOccurrences,
    bool verbose,
    bool pruneGenerality) {

    auto isSubset = [](const LifeState& a, const LifeState& b) {
        return (a & ~b).IsEmpty();
    };

    auto isMoreGeneral = [&](const ComponentTemplate& a, const ComponentTemplate& b) {
        return // isSubset(a.knownOff, b.knownOff) &&
               isSubset(a.count.bit0, b.count.bit0) &&
               isSubset(a.count.bit1, b.count.bit1) &&
               isSubset(a.count.bit2, b.count.bit2) &&
               isSubset(a.count.bit3, b.count.bit3);
    };

    if (verbose) {
        std::cerr << "Combining and deduplicating " << templates.size()
                  << " templates across all files" << std::endl;
    }
    
    // Count occurrences
    std::unordered_map<uint64_t, std::tuple<ComponentTemplate, unsigned>> templateCounts; // template, count
    for (const auto& templ : templates) {
        uint64_t hash = templ.GetHash();
        auto it = templateCounts.find(hash);
        if (it == templateCounts.end()) {
            templateCounts[hash] = std::make_tuple(templ, 1);
        } else {
            auto& [existingTempl, count] = it->second;
            count++;
        }
    }
    
    // Filter by minimum occurrences, then group by canonical base/out
    std::vector<ComponentTemplate> candidates;
    candidates.reserve(templateCounts.size());
    unsigned totalTemplates = templateCounts.size();
    
    for (const auto& [hash, tuple] : templateCounts) {
        const auto& [templ, count] = tuple;
        if (count >= minOccurrences) {
            candidates.push_back(templ);
        }
    }

    std::unordered_map<uint64_t, std::vector<ComponentTemplate>> groups;
    for (const auto& templ : candidates) {
        auto [canon, hash, t, dx, dy] = CanonicaliseTemplate(templ);
        (void)t; (void)dx; (void)dy;
        groups[hash].push_back(std::move(canon));
    }

    std::vector<ComponentTemplate> result;
    result.reserve(candidates.size());

    for (auto& [key, vec] : groups) {
        if (!pruneGenerality) {
            for (auto& t : vec) result.push_back(std::move(t));
            continue;
        }
        std::vector<ComponentTemplate> kept;
        for (auto& t : vec) {
            bool dominated = false;
            for (const auto& k : kept) {
                if (isMoreGeneral(k, t)) {
                    dominated = true;
                    break;
                }
            }
            if (dominated) continue;
            // Drop any kept templates that t generalises
            kept.erase(std::remove_if(kept.begin(), kept.end(),
                                      [&](const ComponentTemplate& other) { return isMoreGeneral(t, other); }),
                       kept.end());
            kept.push_back(std::move(t));
        }
        for (auto& t : kept) {
            result.push_back(std::move(t));
        }
    }
    
    if (verbose) {
        std::cerr << "Combined " << totalTemplates << " unique templates, filtered to " 
                  << result.size() << " (occurring ≥" << minOccurrences << " times"
                  << (pruneGenerality ? ", after generality pruning" : "")
                  << ")" << std::endl;
    }
    
    return result;
}

// Canonicalise a template: find the symmetry/offset that minimises base/out hash
std::tuple<ComponentTemplate, uint64_t, SymmetryTransform, int, int> TemplateCache::CanonicaliseTemplate(const ComponentTemplate& templ) {
    uint64_t minHash = 0;
    ComponentTemplate best = templ;
    SymmetryTransform bestT = SymmetryTransform::Identity;
    int bestDx = 0;
    int bestDy = 0;

    using enum SymmetryTransform;
    for (auto transform : {Identity, ReflectAcrossX, ReflectAcrossYeqX, ReflectAcrossY,
                           Rotate90, Rotate180OddBoth, Rotate270, ReflectAcrossYeqNegXP1}) {
        ComponentTemplate t = templ.Transformed(transform);
        auto bounds = t.base.IsEmpty() ? t.out.XYBounds() : t.base.XYBounds();
        int dx = -bounds[0];
        int dy = -bounds[1];
        t = t.Moved({dx, dy});
        uint64_t hash = combine_hashes(t.base.GetHash(), t.out.GetHash());
        if (minHash == 0 || hash < minHash) {
            minHash = hash;
            best = t;
            bestT = transform;
            bestDx = dx;
            bestDy = dy;
        }
    }
    return {best, minHash, bestT, bestDx, bestDy};
}

inline bool PassesCostFilter(const std::string& inData, const std::string& outData,
                             const std::unordered_map<std::string, SearchResult>* costDb) {
    if (!costDb) return true;
    auto itIn = costDb->find(inData);
    auto itOut = costDb->find(outData);
    if (itIn == costDb->end() || itOut == costDb->end()) return true;
    bool tooSimplifying = itOut->second.cost + 12 < itIn->second.cost;
    bool tooBasic = itIn->second.cost < 10 || itOut->second.cost < 10;
    return !(tooSimplifying || tooBasic);
}

std::vector<Component> TemplateCache::LoadComponentsFromFile(const std::string& filePath, bool verbose,
                                                  const std::unordered_map<std::string, SearchResult>* costDb) {
    std::vector<Component> components;
    
    if (verbose) {
        std::cerr << "Reading components from " << filePath << std::endl;
    }
    
    std::ifstream file(filePath);
    if (!file.is_open()) {
        if (verbose) {
            std::cerr << "Warning: Could not open " << filePath << std::endl;
        }
        return components;
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
            // Check apgcodes before creating component
            auto [inData, gliderData, outData] = Component::SplitSJKLine(line);

            if (inData.empty() || outData.empty())
              continue;

            if (inData == outData)
              continue;

            if (inData.substr(0, 2) != "xs" || outData.substr(0, 2) != "xs")
              continue;

            if (!PassesCostFilter(inData, outData, costDb))
              continue;

            Component comp = Component::FromSJK(line);
            components.push_back(comp);
        } catch (const std::exception& e) {
            if (verbose) {
                std::cerr << "Error parsing component line " << line << ": " << e.what() << std::endl;
            }
        }
    }
    
    if (verbose) {
        std::cerr << "Loaded " << components.size() << " components from " << filePath << std::endl;
    }
    
    return components;
}

std::string TemplateCache::GenerateCacheKeyForFile(const std::string& filePath) {
    std::ostringstream key;
    
    // Include file path and modification time in hash
    key << filePath;
    if (std::filesystem::exists(filePath)) {
        auto ftime = std::filesystem::last_write_time(filePath);
        key << std::chrono::duration_cast<std::chrono::seconds>(ftime.time_since_epoch()).count();
    }
    
    // Simple hash of the key string
    std::hash<std::string> hasher;
    size_t hashValue = hasher(key.str());
    
    std::ostringstream hexStream;
    hexStream << std::hex << hashValue;
    return hexStream.str();
}

bool TemplateCache::LoadTemplatesFromCache(const std::string& cacheFile, std::vector<ComponentTemplate>& templates) {
    try {
        std::ifstream file(cacheFile, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        // Read number of templates
        size_t count;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (file.fail()) {
            return false;
        }
        
        templates.clear();
        templates.reserve(count);
        
        for (size_t i = 0; i < count; i++) {
            ComponentTemplate templ;
            
            // Read each field as RLE string
            size_t dataSize;
            
            // Read base
            file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
            std::vector<char> data(dataSize);
            file.read(data.data(), dataSize);
            templ.base = LifeState::Parse(std::string(data.begin(), data.end())).Moved(32, 32);
            
            // Read knownOff  
            file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
            data.resize(dataSize);
            file.read(data.data(), dataSize);
            templ.knownOff = LifeState::Parse(std::string(data.begin(), data.end())).Moved(32, 32);
            
            // Read out
            file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
            data.resize(dataSize);
            file.read(data.data(), dataSize);
            templ.out = LifeState::Parse(std::string(data.begin(), data.end())).Moved(32, 32);
            
            // Read gliderSet (4 LifeStates: se, sw, nw, ne)
            file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
            data.resize(dataSize);
            file.read(data.data(), dataSize);
            LifeState se = LifeState::Parse(std::string(data.begin(), data.end())).Moved(32, 32);
            
            file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
            data.resize(dataSize);
            file.read(data.data(), dataSize);
            LifeState sw = LifeState::Parse(std::string(data.begin(), data.end())).Moved(32, 32);
            
            file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
            data.resize(dataSize);
            file.read(data.data(), dataSize);
            LifeState nw = LifeState::Parse(std::string(data.begin(), data.end())).Moved(32, 32);
            
            file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
            data.resize(dataSize);
            file.read(data.data(), dataSize);
            LifeState ne = LifeState::Parse(std::string(data.begin(), data.end())).Moved(32, 32);
            
            templ.gliderSet = GliderSet(se, sw, nw, ne);
            
            // Read NeighbourCount (4 LifeStates: bit0, bit1, bit2, bit3)
            file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
            data.resize(dataSize);
            file.read(data.data(), dataSize);
            LifeState bit0 = LifeState::Parse(std::string(data.begin(), data.end())).Moved(32, 32);
            
            file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
            data.resize(dataSize);
            file.read(data.data(), dataSize);
            LifeState bit1 = LifeState::Parse(std::string(data.begin(), data.end())).Moved(32, 32);
            
            file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
            data.resize(dataSize);
            file.read(data.data(), dataSize);
            LifeState bit2 = LifeState::Parse(std::string(data.begin(), data.end())).Moved(32, 32);
            
            file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
            data.resize(dataSize);
            file.read(data.data(), dataSize);
            LifeState bit3 = LifeState::Parse(std::string(data.begin(), data.end())).Moved(32, 32);
            
            templ.count = NeighbourCount(bit3, bit2, bit1, bit0);
            
            if (file.fail()) {
                return false;
            }
            
            templates.push_back(templ);
        }
        
        return true;
        
    } catch (const std::exception&) {
        return false;
    }
}

void TemplateCache::SaveTemplatesToCache(const std::string& cacheFile, const std::vector<ComponentTemplate>& templates) {
    try {
        std::ofstream file(cacheFile, std::ios::binary);
        if (!file.is_open()) {
            return;
        }
        
        // Write number of templates
        size_t count = templates.size();
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        
        for (const auto& templ : templates) {
            // Write each field as RLE string
            std::string baseRLE = templ.base.RLE();
            size_t dataSize = baseRLE.size();
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            file.write(baseRLE.data(), dataSize);
            
            std::string knownOffRLE = templ.knownOff.RLE();
            dataSize = knownOffRLE.size();
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            file.write(knownOffRLE.data(), dataSize);
            
            std::string outRLE = templ.out.RLE();
            dataSize = outRLE.size();
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            file.write(outRLE.data(), dataSize);
            
            // Write gliderSet (4 LifeStates: se, sw, nw, ne)
            std::string seRLE = templ.gliderSet.se.RLE();
            dataSize = seRLE.size();
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            file.write(seRLE.data(), dataSize);
            
            std::string swRLE = templ.gliderSet.sw.RLE();
            dataSize = swRLE.size();
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            file.write(swRLE.data(), dataSize);
            
            std::string nwRLE = templ.gliderSet.nw.RLE();
            dataSize = nwRLE.size();
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            file.write(nwRLE.data(), dataSize);
            
            std::string neRLE = templ.gliderSet.ne.RLE();
            dataSize = neRLE.size();
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            file.write(neRLE.data(), dataSize);
            
            // Write NeighbourCount (4 LifeStates: bit3, bit2, bit1, bit0)
            std::string bit0RLE = templ.count.bit0.RLE();
            dataSize = bit0RLE.size();
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            file.write(bit0RLE.data(), dataSize);
            
            std::string bit1RLE = templ.count.bit1.RLE();
            dataSize = bit1RLE.size();
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            file.write(bit1RLE.data(), dataSize);
            
            std::string bit2RLE = templ.count.bit2.RLE();
            dataSize = bit2RLE.size();
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            file.write(bit2RLE.data(), dataSize);
            
            std::string bit3RLE = templ.count.bit3.RLE();
            dataSize = bit3RLE.size();
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            file.write(bit3RLE.data(), dataSize);
        }
        
    } catch (const std::exception&) {
        // Silently fail cache writes
    }
}

bool TemplateCache::IsUsefulComponent(const Component &comp) {
  // We can't handle pure cleanup steps
  if ((comp.out & ~comp.base).IsEmpty()) {
    LifeState deletion = comp.base & ~comp.out;
    if(deletion == comp.base.StillComponentContaining(deletion))
      return false;
  }

  // Filter out components where all input still lifes are small
  // (these are likely syntheses replicating soups)
  auto baseComponents = comp.base.StillComponents();
  if (baseComponents.size() > 0) {
    bool hasLargeComponent = false;
    for (const auto& component : baseComponents) {
      if (component.GetPop() > 6) {
        hasLargeComponent = true;
        break;
      }
    }
    if (!hasLargeComponent) {
      return false;
    }
  }

  // Filter out components which create a new small still life
  auto outComponents = comp.out.StillComponents();
  if (outComponents.size() > 1) {
    for (const auto& component : outComponents) {
      if (!(component & ~comp.base).IsEmpty() && component.GetPop() <= 8) {
        return false;
      }
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
          return false;
    }
    
    // Require some fraction of the base to remain inactive
    {
        static constexpr double minInactiveFraction = 0.3;
        unsigned basePop = comp.base.GetPop();
        if (basePop > 0) {
            unsigned inactivePop = (comp.base & ~everActive).GetPop();
            if (inactivePop < minInactiveFraction * basePop) {
                return false;
            }
        }
    }

    // Filter out components which cause disconnected changes
    auto diffComponents = everActive.Components(LifeState::ConstantParse("5o$5o$5o$5o$5o!", -2, -2));
    if (diffComponents.size() > 1) {
        return false;
    }

    // Boat-bit filter: if a glider only creates a boat, drop the component
    static const LifeState boatOrients[4] = {
        LifeState::ConstantParse("b2o$obo$bo!"),
        LifeState::ConstantParse("bo$obo$b2o!"),
        LifeState::ConstantParse("bo$obo$2o!"),
        LifeState::ConstantParse("2o$obo$bo!"),
    };

    LifeState boatMask;
    LifeState boatMaskInBase;
    for (const auto& boat : boatOrients) {
        LifeState matchesOut = comp.out.MatchLive(boat);
        for (auto [x, y] : matchesOut.OnCells()) {
            boatMask |= boat.Moved(x, y);
        }
        LifeState matchesBase = comp.base.MatchLive(boat);
        for (auto [x, y] : matchesBase.OnCells()) {
            boatMaskInBase |= boat.Moved(x, y);
        }
    }
    boatMask &= ~boatMaskInBase; // Only consider boats that are new in the output

    if (!boatMask.IsEmpty()) {
        auto gliders = comp.gliderSet.ExtractGliders();
        for (const auto& glider : gliders) {
            GliderSet reduced = comp.gliderSet.RemoveGlider(glider);
            LifeState altOut = Component::RunComponentOutcome(comp.base, reduced);
            LifeState diffRemoved = comp.out & ~altOut;
            LifeState diffAdded = altOut & ~comp.out;
            if (diffAdded.IsEmpty() && !diffRemoved.IsEmpty() && (diffRemoved & ~boatMask).IsEmpty()) {
                return false;
            }
        }
    }
    
    return true;
}
