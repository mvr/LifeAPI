#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <algorithm>
#include <iostream>
#include <array>
#include <cstdint>
#include <tuple>

#include "../ComponentTemplate.hpp"

enum class SplitField {
  StateLive,
  Count0Live,
  Count1Live,
  Count2Live,
  Count3Live,
};

struct TemplateFeature {
  int x;
  int y;
  SplitField field;
  bool expectsLive; // true for out/count, false for knownOff
};

struct TemplateLeafEntry {
  int templateIndex;
  int dx = 0, dy = 0;
  bool fixedTranslation = false;
};

struct TemplateMatchCell {
  uint8_t x, y;
  uint8_t field; // out, count0, count1, count2, knownOff
};

struct TemplateTreeNode {
  bool leaf = true;
  int dx = 0;
  int dy = 0;
  SplitField field = SplitField::StateLive;
  int yesChild = -1; // target live at (dx,dy)
  int noChild = -1;  // target dead at (dx,dy)
  int maybeChild = -1; // unconstrained templates continue here
  std::vector<TemplateLeafEntry> templates;
};

struct TemplateTree {
  std::vector<TemplateTreeNode> nodes;
  int root = -1;
  std::vector<std::vector<TemplateFeature>> features;
  std::vector<std::vector<TemplateMatchCell>> matchCells;
};

struct TemplateAnchor {
  int templateIndex;
  int x;
  int y;

  bool operator==(const TemplateAnchor& other) const {
    return templateIndex == other.templateIndex && x == other.x && y == other.y;
  }
};

struct TemplateMirrorCache {
  LifeState outMirrored;
  LifeState knownOffMirrored;
  LifeState count0Mirrored;
  LifeState count1Mirrored;
  LifeState count2Mirrored;
};

struct TraversalDebug {
  size_t nodesVisited = 0;
  size_t leavesVisited = 0;
  size_t templatesVisited = 0;
  size_t anchorsFound = 0;
  size_t pruned = 0;
  size_t boundedTemplates = 0;
  size_t fullTemplates = 0;
};

inline std::vector<TemplateFeature> CollectFeatures(const ComponentTemplate& templ) {
  std::vector<TemplateFeature> result;

  auto addLive = [&](const LifeState& ls, SplitField field) {
    for (auto [x, y] : ls.OnCells()) {
      result.push_back({x, y, field, true});
    }
  };

  auto addDead = [&](const LifeState& ls) {
    for (auto [x, y] : ls.OnCells()) {
      result.push_back({x, y, SplitField::StateLive, false});
    }
  };

  addLive(templ.out, SplitField::StateLive);
  // addDead(templ.knownOff & templ.out.ZOI());
  addLive(templ.count.bit0, SplitField::Count0Live);
  addLive(templ.count.bit1, SplitField::Count1Live);
  addLive(templ.count.bit2, SplitField::Count2Live);
  // addLive(templ.count.bit3, SplitField::Count3Live);

  return result;
}

inline std::vector<TemplateMatchCell> CollectMatchCells(const ComponentTemplate& templ) {
  std::vector<TemplateMatchCell> cells;
  uint8_t field = 0;
  for (const auto* mask : {&templ.out, &templ.count.bit0, &templ.count.bit1,
                           &templ.count.bit2, &templ.knownOff}) {
    for (auto [x,y] : mask->OnCells())
      cells.push_back({uint8_t(x),uint8_t(y),field});
    ++field;
  }
  return cells;
}

inline bool MatchesAt(const std::vector<TemplateMatchCell>& cells,
                      const std::array<const LifeState*,5>& fields, int dx, int dy) {
  for (const auto& cell : cells)
    if (fields[cell.field]->GetSafe(dx+cell.x,dy+cell.y) != (cell.field != 4))
      return false;
  return true;
}

inline LifeState FieldMask(const LifeState& targetState,
                           const NeighbourCount& targetCount,
                           SplitField field,
                           int dx,
                           int dy) {
  switch (field) {
  case SplitField::StateLive:
    return targetState.Moved(-dx, -dy);
  case SplitField::Count0Live:
    return targetCount.bit0.Moved(-dx, -dy);
  case SplitField::Count1Live:
    return targetCount.bit1.Moved(-dx, -dy);
  case SplitField::Count2Live:
    return targetCount.bit2.Moved(-dx, -dy);
  case SplitField::Count3Live:
    return targetCount.bit3.Moved(-dx, -dy);
  }
  return LifeState();
}

struct SplitKey {
  int dx;
  int dy;
  SplitField field;

  bool operator==(const SplitKey& other) const {
    return dx == other.dx && dy == other.dy && field == other.field;
  }
};

struct SplitKeyHash {
  std::size_t operator()(const SplitKey& k) const {
    std::size_t h1 = std::hash<int>{}(k.dx);
    std::size_t h2 = std::hash<int>{}(k.dy);
    std::size_t h3 = std::hash<int>{}(static_cast<int>(k.field));
    return ((h1 ^ (h2 << 1)) >> 1) ^ (h3 << 1);
  }
};

struct SplitCandidateStats {
  int yes = 0;
  int no = 0;
  int both = 0;
};

inline double ScoreSplit(const SplitCandidateStats& stats) {
  int definite = stats.yes + stats.no;
  if (definite == 0) return -1.0;
  double balance = 1.0 - std::abs(stats.yes - stats.no) / static_cast<double>(definite);
  return definite * 10.0 + balance * 2.0 - stats.both;
}

struct TemplateEntry {
  int index;
  LifeState mask; // allowed translations consistent with splits on the path
};

inline void SetLeafEntries(TemplateTreeNode& node, const std::vector<TemplateEntry>& entries) {
  node.templates.reserve(entries.size());
  for (const auto& entry : entries) {
    TemplateLeafEntry leaf{entry.index};
    if (entry.mask.GetPop()==1) {
      std::tie(leaf.dx,leaf.dy)=entry.mask.FirstOn();
      leaf.fixedTranslation=true;
    }
    node.templates.push_back(leaf);
  }
}

inline int BuildNode(const std::vector<TemplateEntry>& entries,
              TemplateTree& tree,
              unsigned leafSize,
              unsigned depthLimit,
              unsigned depth,
              std::unordered_set<SplitKey, SplitKeyHash>& usedSplits,
              bool verbose,
              std::ostream& log,
              size_t& nodesBuilt,
              double minDefiniteFraction) {
  if (entries.size() == 0) {
    return -1;
  }

  TemplateTreeNode node;

  if (verbose) {
    std::string indent(depth * 2, ' ');
    log << indent << "Processing node depth=" << depth << " entries=" << entries.size() << "\n";
  }

  if (entries.size() <= leafSize || depth >= depthLimit) {
    node.leaf = true;
    SetLeafEntries(node, entries);
    tree.nodes.push_back(std::move(node));
    nodesBuilt++;
    if (verbose) {
      log << "  built " << nodesBuilt << " nodes so far (depth " << depth << ", leaf size " << entries.size() << ")\n";
    }
    return static_cast<int>(tree.nodes.size() - 1);
  }

  // Collect candidate keys, skip already used
  std::unordered_map<SplitKey, int, SplitKeyHash> freq;
  for (const auto& e : entries) {
    for (const auto& f : tree.features[e.index]) {
      SplitKey key{f.x, f.y, f.field};
      if (usedSplits.find(key) != usedSplits.end()) continue;
      freq[key]++;
    }
  }

  bool forceRootSplit = (depth == 0);
  std::vector<std::pair<SplitKey, int>> candidates;
  if (forceRootSplit) {
    candidates.push_back({{0, 0, SplitField::StateLive}, static_cast<int>(entries.size())});
  } else {
    if (freq.empty()) {
      node.leaf = true;
      SetLeafEntries(node, entries);
      tree.nodes.push_back(std::move(node));
      return static_cast<int>(tree.nodes.size() - 1);
    }
    candidates.assign(freq.begin(), freq.end());
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (candidates.size() > 32) candidates.resize(32);
  }

  double bestScore = -1.0;
  SplitKey bestKey = candidates.front().first;
  SplitCandidateStats bestStats{};

  for (const auto& [key, _] : candidates) {
    SplitCandidateStats stats{};
    for (const auto& e : entries) {
      const auto& feats = tree.features[e.index];
      LifeState yesMask;
      LifeState noMask;
      for (const auto& f : feats) {
        if (f.field != key.field) continue;
        if (f.expectsLive) {
          yesMask.SetSafe({key.dx - f.x, key.dy - f.y});
        } else {
          noMask.SetSafe({key.dx - f.x, key.dy - f.y});
        }
      }
      yesMask &= e.mask;
      noMask &= e.mask;
      bool hasYes = !yesMask.IsEmpty();
      bool hasNo = !noMask.IsEmpty();
      if (hasYes) {
        stats.yes++;
      } else if (hasNo) {
        stats.no++;
      } else {
        stats.both++;
      }
    }
    double score = ScoreSplit(stats);
    if (score > bestScore) {
      bestScore = score;
      bestKey = key;
      bestStats = stats;
    }
  }

  if (verbose) {
    std::string indent(depth * 2, ' ');
    log << indent << "Split depth " << depth
        << " at (" << bestKey.dx << "," << bestKey.dy << ") field " << static_cast<int>(bestKey.field)
        << " yes=" << bestStats.yes << " no=" << bestStats.no << " both=" << bestStats.both << "\n";
  }

  int totalHere = bestStats.yes + bestStats.no + bestStats.both;
  int definiteHere = bestStats.yes + bestStats.no;
  if (bestScore < 0 || (bestStats.yes == 0 && bestStats.no == 0) ||
      (totalHere > 0 && definiteHere < static_cast<int>(minDefiniteFraction * totalHere))) {
    node.leaf = true;
    SetLeafEntries(node, entries);
    tree.nodes.push_back(std::move(node));
    return static_cast<int>(tree.nodes.size() - 1);
  }

  std::vector<TemplateEntry> yesEntries;
  std::vector<TemplateEntry> noEntries;
  std::vector<TemplateEntry> maybeEntries;
  yesEntries.reserve(entries.size());
  noEntries.reserve(entries.size());
  maybeEntries.reserve(entries.size());

  for (const auto& e : entries) {
    const auto& feats = tree.features[e.index];
    LifeState yesMask;
    LifeState noMask;
    for (const auto& f : feats) {
      if (f.field != bestKey.field) continue;
      if (f.expectsLive) {
        yesMask.SetSafe({bestKey.dx - f.x, bestKey.dy - f.y});
      } else {
        noMask.SetSafe({bestKey.dx - f.x, bestKey.dy - f.y});
      }
    }
    yesMask &= e.mask;
    noMask &= e.mask;

    bool hasYes = !yesMask.IsEmpty();
    bool hasNo = !noMask.IsEmpty();
    if (hasYes) {
      yesEntries.push_back({e.index, yesMask});
    } else if (hasNo) {
      noEntries.push_back({e.index, noMask});
    } else {
      maybeEntries.push_back(e);
    }
  }

  node.leaf = false;
  node.dx = bestKey.dx;
  node.dy = bestKey.dy;
  node.field = bestKey.field;

  usedSplits.insert(bestKey);
  node.yesChild = BuildNode(yesEntries, tree, leafSize, depthLimit, depth + 1, usedSplits, verbose, log, nodesBuilt, minDefiniteFraction);
  node.noChild = BuildNode(noEntries, tree, leafSize, depthLimit, depth + 1, usedSplits, verbose, log, nodesBuilt, minDefiniteFraction);
  node.maybeChild = BuildNode(maybeEntries, tree, leafSize, depthLimit, depth + 1, usedSplits, verbose, log, nodesBuilt, minDefiniteFraction);
  usedSplits.erase(bestKey);

  if (verbose) {
    std::string indent(depth * 2, ' ');
    log << indent << "Split depth " << depth
        << " at (" << bestKey.dx << "," << bestKey.dy << ") field " << static_cast<int>(bestKey.field)
        << " yes=" << bestStats.yes << " no=" << bestStats.no << " both=" << bestStats.both << "\n";
  }

  tree.nodes.push_back(std::move(node));
  nodesBuilt++;
  return static_cast<int>(tree.nodes.size() - 1);
}

inline TemplateTree BuildTemplateTree(std::vector<ComponentTemplate>& templates,
                               unsigned leafSize,
                               unsigned depthLimit,
                               bool verbose,
                               std::ostream& log,
                               double minDefiniteFraction) {
  TemplateTree tree;
  tree.features.reserve(templates.size());
  tree.matchCells.reserve(templates.size());
  for (auto& templ : templates) {
    templ.NormalisePosition();
    if (!templ.out.IsEmpty()) {
      auto anchor = templ.out.FirstOn();
      templ = templ.Moved({-anchor.first, -anchor.second});
    }
    tree.features.push_back(CollectFeatures(templ));
    tree.matchCells.push_back(CollectMatchCells(templ));
  }

  std::vector<TemplateEntry> rootEntries;
  rootEntries.reserve(templates.size());
  LifeState fullMask = ~LifeState();
  for (int i = 0; i < static_cast<int>(templates.size()); ++i) {
    rootEntries.push_back({i, fullMask});
  }
  std::unordered_set<SplitKey, SplitKeyHash> usedSplits;
  size_t nodesBuilt = 0;
  if (verbose) {
    log << "Building template tree for " << templates.size() << " templates (leafSize=" << leafSize
        << ", depthLimit=" << depthLimit << ", minDefiniteFraction=" << minDefiniteFraction << ")\n";
  }
  tree.root = BuildNode(rootEntries, tree, leafSize, depthLimit, 0, usedSplits, verbose, log, nodesBuilt, minDefiniteFraction);
  if (verbose) {
    log << "Template tree build complete, total nodes: " << nodesBuilt << "\n";
  }
  return tree;
}

inline void TraverseTemplateTree(
    int nodeIdx,
    const TemplateTree& tree,
    const LifeState& targetState,
    const NeighbourCount& targetCount,
    const LifeState& targetStateMirrored,
    const NeighbourCount& targetCountMirrored,
    const std::vector<ComponentTemplate>& templates,
    const LifeState& candidateMask,
    std::vector<TemplateAnchor>& results,
    TraversalDebug* dbg = nullptr,
    unsigned depth = 0) {
  if (nodeIdx < 0)
    return;

  if (candidateMask.IsEmpty()) {
    if (dbg) dbg->pruned++;
    return;
  }

  if (dbg) dbg->nodesVisited++;

  const TemplateTreeNode& node = tree.nodes[nodeIdx];
  if (node.leaf) {
    if (dbg) dbg->leavesVisited++;

    // Tree anchors and template placements differ by the entry's translation.
    // Check small candidate sets directly; retain convolution for larger sets
    // and entries with more than one possible relative translation.
    constexpr unsigned maxDirectCandidates = 8;
    unsigned candidateCount = candidateMask.GetPop();
    const std::array<const LifeState*,5> fields{
        &targetState,&targetCount.bit0,&targetCount.bit1,&targetCount.bit2,&targetState};
    for (const auto& entry : node.templates) {
      int templIdx = entry.templateIndex;
      if (dbg) dbg->templatesVisited++;
      LifeState matches;
      if (entry.fixedTranslation && candidateCount <= maxDirectCandidates) {
        if (dbg) dbg->boundedTemplates++;
        for (auto [x,y] : candidateMask.OnCells()) {
          x+=entry.dx; y+=entry.dy;
          if (MatchesAt(tree.matchCells[templIdx],fields,x,y))
            matches.SetSafe(-x,-y);
        }
      } else {
        if (dbg) dbg->fullTemplates++;
        matches = templates[templIdx].MatchReverseMirrored(targetStateMirrored,targetCountMirrored);
      }
      for (auto [x,y] : matches.OnCells()) {
        results.push_back({templIdx,-x,-y});
        if (dbg) dbg->anchorsFound++;
      }
    }

    return;
  }

  LifeState fieldMask = FieldMask(targetState, targetCount, node.field, node.dx, node.dy);

  TraverseTemplateTree(node.yesChild, tree, targetState, targetCount, targetStateMirrored, targetCountMirrored, templates, candidateMask & fieldMask, results, dbg, depth + 1);
  TraverseTemplateTree(node.noChild, tree, targetState, targetCount, targetStateMirrored, targetCountMirrored, templates, candidateMask & ~fieldMask, results, dbg, depth + 1);
  TraverseTemplateTree(node.maybeChild, tree, targetState, targetCount, targetStateMirrored, targetCountMirrored, templates, candidateMask, results, dbg, depth + 1);
}

inline void TraverseTemplateTree(
    const TemplateTree &tree, const LifeState &targetState,
    const std::vector<ComponentTemplate> &templates,
    std::vector<TemplateAnchor> &results,
    TraversalDebug *dbg = nullptr) {
  NeighbourCount targetCount(targetState);
  LifeState targetStateMirrored = targetState.Mirrored();
  NeighbourCount targetCountMirrored(targetStateMirrored);
  TraverseTemplateTree(tree.root, tree, targetState, targetCount, targetStateMirrored, targetCountMirrored, templates, ~LifeState(), results, dbg, 0);
}

struct TemplateTreeStats {
  size_t nodes = 0;
  size_t leaves = 0;
  size_t maxDepth = 0;
};

inline void AccumStats(const TemplateTree& tree, int nodeIdx, size_t depth, TemplateTreeStats& stats) {
  if (nodeIdx < 0) return;
  stats.nodes++;
  const auto& node = tree.nodes[nodeIdx];
  if (node.leaf) {
    stats.leaves++;
    if (depth > stats.maxDepth) stats.maxDepth = depth;
    return;
  }
  AccumStats(tree, node.yesChild, depth + 1, stats);
  AccumStats(tree, node.noChild, depth + 1, stats);
  AccumStats(tree, node.maybeChild, depth + 1, stats);
}

inline TemplateTreeStats ComputeStats(const TemplateTree& tree) {
  TemplateTreeStats stats;
  AccumStats(tree, tree.root, 0, stats);
  return stats;
}
