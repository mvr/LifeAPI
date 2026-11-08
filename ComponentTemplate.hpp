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
  // Cells off throughout the recorded reaction that see one/two reaction
  // neighbours after removing the unchanged background. They forbid two/one
  // background neighbours respectively.
  LifeState stable1;
  LifeState stable2;
  GliderSet gliderSet;
  LifeState out;

  bool operator==(const ComponentTemplate&) const = default;
  bool operator<(const ComponentTemplate& other) const;

  static ComponentTemplate FromComponent(const Component &component);
  LifeState MatchReverse(const LifeState &state, const NeighbourCount &count) const;
  LifeState MatchReverse(const LifeState &state) const;
  LifeState MatchReverseMirrored(const LifeState &stateMirrored, const NeighbourCount &countMirrored) const;

  ComponentTemplate Transformed(SymmetryTransform t) const;
  ComponentTemplate Moved(std::pair<int, int> p) const;

  void NormalisePosition();
  std::pair<int, int> NormalisingOffset() const;
  // Try to shift glider set to fit in 64x64 torus without wrapping
  void ShiftToFitTorus();
  // Canonicalise a template: find the symmetry/offset that minimises base/out hash
  // Optionally compute the symmetry-invariant identity hash in the same pass.
  std::tuple<ComponentTemplate, uint64_t, SymmetryTransform, int, int> Canonicalise(uint64_t* matchingHash = nullptr) const;

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
  std::array<LifeState, 9> everCount;

  unsigned gen = 0;

  bool done = false;
  while (!done) {
    LifeState prev = state;
    NeighbourCount count(state);

    everActive |= state ^ component.base;
    everDifferentNeighbours |= count.Difference(startCount);
    everMoreThanOne |= count.bit1 | count.bit2 | count.bit3;
    for (unsigned neighbours = 1; neighbours <= 8; ++neighbours)
      everCount[neighbours] |= count.WithExactly(neighbours);

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

  LifeState partialBase = component.base & relevant;
  LifeState partialOut = component.out & relevant;
  LifeState fixed = component.base & ~partialBase;
  if (fixed != (component.out & ~partialOut) || !(fixed & everActive).IsEmpty())
    throw std::runtime_error("Template background changes during the reaction");

  // The fixed neighbour count is constant. Subtract it from the recorded
  // total counts without replaying the reaction or storing every generation.
  NeighbourCount fixedCount(fixed);
  LifeState stable1, stable2;
  for (unsigned background = 0; background <= 7; ++background) {
    LifeState cells = fixedCount.WithExactly(background);
    stable1 |= cells & everCount[background + 1];
    if (background <= 6) stable2 |= cells & everCount[background + 2];
  }
  LifeState alwaysOff = ~component.base & ~everActive & everActive.ZOI();
  stable1 &= alwaysOff;
  stable2 &= alwaysOff;

  return {partialBase,
          knownOff,
          count,
          stable1,
          stable2,
          component.gliderSet,
          partialOut};
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

std::pair<int, int> ComponentTemplate::NormalisingOffset() const {
  const auto& anchor = base.IsEmpty() ? out : base;
  if (anchor.IsEmpty()) return {0,0};
  auto [xs,ys] = anchor.ToroidalBoundsStarts();
  if (std::has_single_bit(xs) && std::has_single_bit(ys))
    return {-int(std::countr_zero(xs)), -int(std::countr_zero(ys))};

  // Tied gaps must be resolved using the geometry, then the full template,
  // rather than their absolute positions on the torus.
  uint64_t bestHash = ~uint64_t(0);
  ComponentTemplate best;
  std::pair<int,int> offset;
  bool found = false;
  for (uint64_t xChoices=xs; xChoices; xChoices &= xChoices-1)
    for (uint64_t yChoices=ys; yChoices; yChoices &= yChoices-1) {
      std::pair<int,int> candidate{-int(std::countr_zero(xChoices)), -int(std::countr_zero(yChoices))};
      auto t = Moved(candidate);
      auto hash = combine_hashes(t.base.GetHash(),t.out.GetHash());
      if (!found || hash < bestHash || (hash == bestHash && t < best)) {
        found = true; bestHash = hash; best = std::move(t); offset = candidate;
      }
    }
  return offset;
}

void ComponentTemplate::NormalisePosition() {
  *this = Moved(NormalisingOffset());
}

void ComponentTemplate::ShiftToFitTorus() {
  auto offset = gliderSet.OffsetToFitTorus();
  *this = Moved(offset);
}

bool ComponentTemplate::operator<(const ComponentTemplate& other) const {
    // Requirements first, then the salvo, for a stable representative.
    for (auto [a,b] : {std::pair{&base,&other.base}, {&out,&other.out}, {&knownOff,&other.knownOff},
                       {&count.bit0,&other.count.bit0}, {&count.bit1,&other.count.bit1}, {&count.bit2,&other.count.bit2},
                       {&stable1,&other.stable1}, {&stable2,&other.stable2},
                       {&gliderSet.se,&other.gliderSet.se}, {&gliderSet.sw,&other.gliderSet.sw},
                       {&gliderSet.nw,&other.gliderSet.nw}, {&gliderSet.ne,&other.gliderSet.ne},
                       {&count.bit3,&other.count.bit3}}) {
        for (unsigned i=0;i<N;++i)
            if (a->state[i]!=b->state[i]) return a->state[i]<b->state[i];
    }
    return false;
}

std::tuple<ComponentTemplate, uint64_t, SymmetryTransform, int, int> ComponentTemplate::Canonicalise(uint64_t* matchingHash) const {
    if (matchingHash) *matchingHash = ~uint64_t(0);
    uint64_t minHash = 0;
    ComponentTemplate best = *this;
    SymmetryTransform bestT = SymmetryTransform::Identity;
    int bestDx = 0;
    int bestDy = 0;

    using enum SymmetryTransform;
    for (auto transform : {Identity, ReflectAcrossX, ReflectAcrossYeqX, ReflectAcrossY,
                           Rotate90, Rotate180OddBoth, Rotate270, ReflectAcrossYeqNegXP1}) {
        ComponentTemplate t = this->Transformed(transform);
        auto [dx,dy] = t.NormalisingOffset();
        t = t.Moved({dx, dy});
        if (matchingHash) *matchingHash = std::min(*matchingHash, t.GetHashNonSymmetrised());
        uint64_t hash = combine_hashes(t.base.GetHash(), t.out.GetHash());
        if (minHash == 0 || hash < minHash || (hash == minHash && t < best)) {
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
  hash = combine_hashes(hash, stable1.GetHash());
  hash = combine_hashes(hash, stable2.GetHash());
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
