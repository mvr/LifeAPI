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
  GliderSet gliderSet;
  LifeState out;

  static ComponentTemplate FromComponent(const Component &component);
  LifeState MatchReverse(const LifeState &state, const NeighbourCount &count) const;
  LifeState MatchReverse(const LifeState &state) const;

  ComponentTemplate Transformed(SymmetryTransform t) const;
  ComponentTemplate Moved(std::pair<int, int> p) const;

  void NormalisePosition();
  // Try to shift glider set to fit in 64x64 torus without wrapping
  void ShiftToFitTorus();

  uint64_t GetHash() const;

  // Debugging only:
  std::string RLE() const;
};

ComponentTemplate ComponentTemplate::FromComponent(const Component &component) {
  LifeState state = component.Realise();
  LifeState everActive;

  NeighbourCount startCount(state);
  LifeState everDifferentNeighbours;

  unsigned gen = 0;

  bool done = false;
  while (!done) {
    LifeState prev = state;

    everActive |= state ^ component.base;
    everDifferentNeighbours |= NeighbourCount(state).Difference(startCount);

    state.Step();
    gen++;
    if (state == prev)
      done = true;
    if (gen > 200)
      throw std::runtime_error("Component took too long");
  }

  LifeState relevant = everActive.ZOI() & everDifferentNeighbours;

  NeighbourCount count(state);
  count.bit0 &= relevant;
  count.bit1 &= relevant;
  count.bit2 &= relevant;
  count.bit3 &= relevant;

  return {component.base & relevant,
          ~component.base & ~component.out & everActive.ZOI(),
          count,
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

ComponentTemplate ComponentTemplate::Transformed(SymmetryTransform t) const {
  return {
    base.Transformed(t),
    knownOff.Transformed(t),
    count.Transformed(t),
    gliderSet.Transformed(t),
    out.Transformed(t),
  };
}

ComponentTemplate ComponentTemplate::Moved(std::pair<int, int> p) const {
  return {
    base.Moved(p),
    knownOff.Moved(p),
    count.Moved(p),
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

uint64_t ComponentTemplate::GetHash() const {
  uint64_t hash = base.GetHash();
  hash = combine_hashes(hash, out.GetHash());
  hash = combine_hashes(hash, count.bit0.GetHash());
  hash = combine_hashes(hash, count.bit1.GetHash());
  hash = combine_hashes(hash, count.bit2.GetHash());
  return hash;
}

std::string ComponentTemplate::RLE() const {
  // LifeState marked = count.bit3 | count.bit2 | count.bit1 | count.bit0;
  // LifeState original = base & out;
  // marked &= ~original;
  // LifeHistory history(base | gliderSet.Realise(), LifeState(), marked, original);
  // return history.RLEWHeader();

  LifeState marked = out;
  LifeState original = base & out;
  marked &= ~original;
  LifeHistory history(base | out | gliderSet.Realise(), LifeState(), marked, original);
  return history.RLEWHeader();
}
