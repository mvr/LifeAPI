#pragma once

#include "LifeAPI.hpp"
#include "LifeHistory.hpp"
#include "Symmetry.hpp"
#include "NeighbourCount.hpp"
#include "Component.hpp"

struct ComponentTemplate {
  LifeState base;
  LifeState knownOff;
  NeighbourCount count; // This should be more refined and use a full LifeStable
  LifeState stable1; // Off cells that see 1 active neighbour (so shouldn't have 2 stable neighbours)
  LifeState stable2;
  GliderSet gliderSet;
  LifeState out;

  bool operator==(const ComponentTemplate&) const = default;

  static ComponentTemplate FromComponent(const Component &component);
  LifeState MatchReverse(const LifeState &state, const NeighbourCount &count) const;
  LifeState MatchReverse(const LifeState &state) const;
  LifeState MatchReverseMirrored(const LifeState &stateMirrored, const NeighbourCount &countMirrored) const;

  ComponentTemplate Transformed(SymmetryTransform t) const;
  ComponentTemplate Moved(std::pair<int, int> p) const;

  void NormalisePosition();
  // Try to shift glider set to fit in 64x64 torus without wrapping
  void ShiftToFitTorus();
  // Canonicalise a template: find the symmetry/offset that minimises base/out hash
  std::tuple<ComponentTemplate, uint64_t, SymmetryTransform, int, int> Canonicalise() const;

  uint64_t GetHashNonSymmetrised() const;
  uint64_t GetHash() const;

  // Debugging only:
  LifeHistory ToHistory() const;
  std::string RLE() const { return ToHistory().RLEWHeader(); };
};

ComponentTemplate ComponentTemplate::FromComponent(const Component &component) {
  LifeState state = component.Realise();
  LifeState everActive;

  NeighbourCount startCount(component.base);
  LifeState everDifferentNeighbours;
  LifeState everMoreThanOne;
  LifeState everOne;
  LifeState everTwo;

  unsigned gen = 0;

  bool done = false;
  while (!done) {
    LifeState prev = state;
    NeighbourCount count(state);

    everActive |= state ^ component.base;
    everDifferentNeighbours |= count.Difference(startCount);
    everMoreThanOne |= count.bit1 | count.bit2 | count.bit3;
    everOne |= count.bit0 & ~count.bit1 & ~count.bit2 & ~count.bit3;
    everTwo |= ~count.bit0 & count.bit1 & ~count.bit2 & ~count.bit3;

    state.Step();
    gen++;
    if (state == prev)
      done = true;
    if (gen > 300)
      throw std::runtime_error("Component took too long");
  }

  LifeState glancing = ~everActive & everActive.ZOI() & (startCount.bit0 & ~startCount.bit1 & ~startCount.bit2);

  LifeState relevant = everActive.ZOI() & everDifferentNeighbours & everMoreThanOne & ~glancing;

  NeighbourCount count(state);
  count.bit0 &= relevant;
  count.bit1 &= relevant;
  count.bit2 &= relevant;
  count.bit3 &= relevant;

  LifeState knownOff = ~component.base & ~component.out & everActive.ZOI();

  LifeState stable1 = everOne & ~component.base & ~everActive & everActive.ZOI();
  LifeState stable2 = everTwo & ~component.base & ~everActive & everActive.ZOI();

  return {component.base & relevant,
          knownOff,
          count,
          stable1,
          stable2,
          component.gliderSet,
          component.out & relevant};
}

LifeState ComponentTemplate::MatchReverse(const LifeState &state, const NeighbourCount &stateCount) const {
  LifeState candidates = state.MatchLiveAndDead(out, knownOff);
  if(candidates.IsEmpty())
    return LifeState();

  candidates &= stateCount.bit0.MatchLive(count.bit0);
  if(candidates.IsEmpty())
    return LifeState();
  candidates &= stateCount.bit1.MatchLive(count.bit1);
  if(candidates.IsEmpty())
    return LifeState();
  candidates &= stateCount.bit2.MatchLive(count.bit2);

  return candidates;
}

LifeState ComponentTemplate::MatchReverse(const LifeState &state) const {
  NeighbourCount stateCount(state);
  return MatchReverse(state, stateCount);
}

// This avoids the repeated use of Mirrored() inside the Match calls
LifeState ComponentTemplate::MatchReverseMirrored(const LifeState &stateMirrored, const NeighbourCount &stateCountMirrored) const {
  LifeState candidates = ~out.Convolve(~stateMirrored);
  if (candidates.IsEmpty())
    return LifeState();

  candidates &= ~count.bit0.Convolve(~stateCountMirrored.bit0);
  if(candidates.IsEmpty())
    return LifeState();

  candidates &= ~count.bit1.Convolve(~stateCountMirrored.bit1);
  if(candidates.IsEmpty())
    return LifeState();

  candidates &= ~count.bit2.Convolve(~stateCountMirrored.bit2);
  if(candidates.IsEmpty())
    return LifeState();

  candidates &= ~knownOff.Convolve(stateMirrored);

  return candidates;
}

ComponentTemplate ComponentTemplate::Transformed(SymmetryTransform t) const {
  return {
    base.Transformed(t),
    knownOff.Transformed(t),
    count.Transformed(t),
    stable1.Transformed(t),
    stable2.Transformed(t),
    gliderSet.Transformed(t),
    out.Transformed(t),
  };
}

ComponentTemplate ComponentTemplate::Moved(std::pair<int, int> p) const {
  return {
    base.Moved(p),
    knownOff.Moved(p),
    count.Moved(p),
    stable1.Moved(p),
    stable2.Moved(p),
    gliderSet.Moved(p),
    out.Moved(p),
  };
}

void ComponentTemplate::NormalisePosition() {
  std::array<int, 4> bounds = base.XYBounds();

  if (base.IsEmpty())
    bounds = out.XYBounds();

  *this = Moved({-bounds[0], -bounds[1]});
}

void ComponentTemplate::ShiftToFitTorus() {
  auto offset = gliderSet.OffsetToFitTorus();
  *this = Moved(offset);
}

std::tuple<ComponentTemplate, uint64_t, SymmetryTransform, int, int> ComponentTemplate::Canonicalise() const {
    uint64_t minHash = 0;
    ComponentTemplate best = *this;
    SymmetryTransform bestT = SymmetryTransform::Identity;
    int bestDx = 0;
    int bestDy = 0;

    using enum SymmetryTransform;
    for (auto transform : {Identity, ReflectAcrossX, ReflectAcrossYeqX, ReflectAcrossY,
                           Rotate90, Rotate180OddBoth, Rotate270, ReflectAcrossYeqNegXP1}) {
        ComponentTemplate t = this->Transformed(transform);
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

uint64_t ComponentTemplate::GetHashNonSymmetrised() const {
  uint64_t hash = base.GetHash();
  hash = combine_hashes(hash, knownOff.GetHash());
  hash = combine_hashes(hash, out.GetHash());
  hash = combine_hashes(hash, count.bit0.GetHash());
  hash = combine_hashes(hash, count.bit1.GetHash());
  hash = combine_hashes(hash, count.bit2.GetHash());
  return hash;
}

uint64_t ComponentTemplate::GetHash() const {
  // Calculate hash for all orientations and use the minimum
  uint64_t minHash = 0;

  using enum SymmetryTransform;
  for (auto transform : {Identity, ReflectAcrossX, ReflectAcrossYeqX, ReflectAcrossY,
                         Rotate90, Rotate180OddBoth, Rotate270, ReflectAcrossYeqNegXP1}) {
    ComponentTemplate transformedTempl = Transformed(transform);
    transformedTempl.NormalisePosition();
    uint64_t hash = transformedTempl.GetHashNonSymmetrised();

    if (minHash == 0 || hash < minHash) {
      minHash = hash;
    }
  }
  return minHash;
}

LifeHistory ComponentTemplate::ToHistory() const {
  // LifeState marked = count.bit3 | count.bit2 | count.bit1 | count.bit0;
  // LifeState original = base & out;
  // marked &= ~original;
  // LifeHistory history(base | gliderSet.Realise(), LifeState(), marked,
  // original); return history;

  LifeState marked = count.bit3 | count.bit2 | count.bit1 | count.bit0;
  LifeHistory history(out, LifeState(), marked, LifeState());
  return history;

  // LifeState marked = out;
  // LifeState original = base & out;
  // marked &= ~original;
  // LifeHistory history(base | gliderSet.Realise(), LifeState(), out);
  // return history;
}
