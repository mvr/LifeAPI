#include <cuda/std/tuple>

// an inlined device function:
#define _DI_ __attribute__((always_inline)) __device__ inline

// #include "LifeAPI/LifeAPI.hpp"

// Specific integer types:
#define uint64_cu unsigned long long int
#define uint32_cu unsigned int

static_assert(sizeof(uint64_cu) == sizeof(uint64_t),
              "uint64_cu must be an unsigned 64-bit integer");

static_assert(sizeof(uint32_cu) == sizeof(uint32_t),
              "uint32_cu must be an unsigned 32-bit integer");

struct LifeStateCU {
  uint4 state;

  // Constructor
  _DI_ LifeStateCU() : state{0, 0, 0, 0} {}

  // Constructor from uint4
  _DI_ explicit LifeStateCU(uint4 initial_state) : state(initial_state) {}

  _DI_ static LifeStateCU load(const uint64_t (&state)[64]);
  _DI_ static LifeStateCU load_from_hash(const unsigned int *hash_ptr);

  _DI_ void save(uint64_t *state) const;

  _DI_ bool get(int x, int y) const;
  _DI_ bool get(cuda::std::tuple<int, int> cell) const { return get(cuda::std::get<0>(cell), cuda::std::get<1>(cell)); }
  _DI_ void set(int x, int y);
  _DI_ void set(cuda::std::tuple<int, int> cell) { set(cuda::std::get<0>(cell), cuda::std::get<1>(cell)); }
  _DI_ void erase(int x, int y);
  _DI_ void erase(cuda::std::tuple<int, int> cell) { erase(cuda::std::get<0>(cell), cuda::std::get<1>(cell)); }
  _DI_ cuda::std::tuple<int, int> first_on() const;
  _DI_ uint64_t row(int y) const;

  _DI_ bool empty() const;
  _DI_ int pop() const;

  _DI_ bool operator==(LifeStateCU other) const { return (*this ^ other).empty(); }
  
  _DI_ LifeStateCU operator~() const { return LifeStateCU({~state.x, ~state.y, ~state.z, ~state.w}); }
  _DI_ LifeStateCU operator|(LifeStateCU other) const { return LifeStateCU({state.x | other.state.x, state.y | other.state.y, state.z | other.state.z, state.w | other.state.w }); }
  _DI_ LifeStateCU operator&(LifeStateCU other) const { return LifeStateCU({state.x & other.state.x, state.y & other.state.y, state.z & other.state.z, state.w & other.state.w }); }
  _DI_ LifeStateCU operator^(LifeStateCU other) const { return LifeStateCU({state.x ^ other.state.x, state.y ^ other.state.y, state.z ^ other.state.z, state.w ^ other.state.w }); }
  _DI_ void operator|=(LifeStateCU other) { state.x |= other.state.x; state.y |= other.state.y; state.z |= other.state.z; state.w |= other.state.w; }
  _DI_ void operator&=(LifeStateCU other) { state.x &= other.state.x; state.y &= other.state.y; state.z &= other.state.z; state.w &= other.state.w; }
  _DI_ void operator^=(LifeStateCU other) { state.x ^= other.state.x; state.y ^= other.state.y; state.z ^= other.state.z; state.w ^= other.state.w; }
  
  _DI_ LifeStateCU hmirrored_even() const;
  _DI_ LifeStateCU vmirrored_even() const;
  _DI_ LifeStateCU mirrored_even() const;
  _DI_ LifeStateCU mirrored() const;

  // NOTE: I have change this from qufince to mean translation by (rh, rv) much, so
  // (0, 0) -> (rh, rv). I think silk is the same. Question for apg: why was
  // rotate_torus the reverse?
  _DI_ void rotate_torus_inplace(int rh, int rv);
  _DI_ void rotate_torus_inplace(cuda::std::tuple<int, int> cell) { rotate_torus_inplace(cuda::std::get<0>(cell), cuda::std::get<1>(cell)); }
  _DI_ LifeStateCU rotate_torus(int rh, int rv) const;
  _DI_ LifeStateCU rotate_torus(cuda::std::tuple<int, int> cell) const { return rotate_torus(cuda::std::get<0>(cell), cuda::std::get<1>(cell)); };

  _DI_ LifeStateCU convolve(LifeStateCU other) const;
  _DI_ LifeStateCU match_live(LifeStateCU other) const;
  _DI_ LifeStateCU match(LifeStateCU live, LifeStateCU dead) const;
  _DI_ LifeStateCU zoi() const;

  // Advance operations
  _DI_ LifeStateCU advance() const;
  _DI_ void advance_inplace();
  _DI_ bool escaped_bounding_box();
  _DI_ int advance_initial();
  // _DI_ int advance_tile_inplace(int rh, int rv);

  _DI_ cuda::std::pair<LifeStateCU, LifeStateCU> interaction_counts() const;

  _DI_ void print() const;
};

_DI_ LifeStateCU LifeStateCU::load(const uint64_t (&in)[64]) {
  const uint4 *u4ptr = (const uint4 *)&in;
  uint4 result = u4ptr[threadIdx.x & 31];
  return LifeStateCU(result);
}

_DI_ void LifeStateCU::save(uint64_t *out) const {
  uint4 *u4ptr = (uint4 *)out;
  u4ptr[threadIdx.x & 31] = state;
}

_DI_ LifeStateCU LifeStateCU::load_from_hash(const unsigned int *hash_ptr) {
  unsigned int h = 0;

  int laneId = threadIdx.x & 31;

  if ((laneId >= 12) && (laneId < 20)) {
    h = hash_ptr[laneId - 12];
  }

  // elegantly perform necessary endianness change:
  uint4 result;
  result.x = (h & 0x0000ff00u) << 16;
  result.y = (h & 0x000000ffu);
  result.z = (h & 0xff000000u);
  result.w = (h & 0x00ff0000u) >> 16;

  return LifeStateCU(result);
}

_DI_ uint64_t LifeStateCU::row(int y) const {
  int src = (y & 63) >> 1;

  if (y & 1) {
    uint32_t lo = __shfl_sync(0xffffffffu, state.z, src);
    uint32_t hi = __shfl_sync(0xffffffffu, state.w, src);
    return (uint64_t)hi << 32 | lo;
  } else {
    uint32_t lo = __shfl_sync(0xffffffffu, state.x, src);
    uint32_t hi = __shfl_sync(0xffffffffu, state.y, src);
    return (uint64_t)hi << 32 | lo;
  }
}

_DI_ bool LifeStateCU::get(int x, int y) const {
  uint64_t r = row(y);
  return (r & (1ull << x)) != 0;
}

// _DI_ void LifeStateCU::set(int x, int y) {

// }

// mvrnote: I think this is the first place we actually fix that x
// is the origin...
_DI_ void LifeStateCU::erase(int x, int y) {
  bool should_act = (threadIdx.x & 31) == (y >> 1);
  unsigned int bit = 1u << (x & 31);

  // These operations only have an effect when should_act is true
  state.x &= ~(bit & (should_act && !(y & 1) && !(x & 32) ? 0xFFFFFFFF : 0));
  state.y &= ~(bit & (should_act && !(y & 1) && (x & 32) ? 0xFFFFFFFF : 0));
  state.z &= ~(bit & (should_act && (y & 1) && !(x & 32) ? 0xFFFFFFFF : 0));
  state.w &= ~(bit & (should_act && (y & 1) && (x & 32) ? 0xFFFFFFFF : 0));

  // if ((threadIdx.x & 31) == (y>>1)) {
  //   if (y & 1) {
  //     if (x & 32) {
  //       state.w &= ~(1 << (x-32));
  //     } else {
  //       state.z &= ~(1 << x);
  //     }
  //   } else {
  //     if (x & 32) {
  //       state.y &= ~(1 << (x-32));
  //     } else {
  //       state.x &= ~(1 << x);
  //     }
  //   }
  // }
  __syncwarp();
}

_DI_ cuda::std::tuple<int, int> LifeStateCU::first_on() const {
  int x_low = __ffsll((uint64_t) state.y << 32 | state.x) - 1;
  int x_high = __ffsll((uint64_t) state.w << 32 | state.z) - 1;

  bool use_high = ((state.x | state.y) == 0);
  int x = use_high ? x_high : x_low;

  int y_base = (threadIdx.x & 31) << 1;
  int y = y_base + (use_high ? 1 : 0);

  uint32_t mask = __ballot_sync(0xffffffffu, state.x | state.y | state.z | state.w);
  int first_lane = __ffs(mask) - 1;

  y = __shfl_sync(0xffffffff, y, first_lane); // mvrnote: This order might reduce instruction dependency?
  x = __shfl_sync(0xffffffff, x, first_lane);

  return {x, y};
}

_DI_ bool LifeStateCU::empty() const {
  return __ballot_sync(0xffffffffu, state.x | state.y | state.z | state.w) == 0;
}

_DI_ int LifeStateCU::pop() const {
  int val = __popc(state.x) + __popc(state.y) + __popc(state.z) + __popc(state.w);
  for (int offset = 16; offset > 0; offset /= 2)
    val += __shfl_down_sync(0xffffffff, val, offset);
  return __shfl_sync(0xffffffff, val, 0);
}



_DI_ LifeStateCU LifeStateCU::hmirrored_even() const {
  uint4 r;
  r.x = __brev(state.y);
  r.y = __brev(state.x);
  r.z = __brev(state.w);
  r.w = __brev(state.z);
  return LifeStateCU(r);
}

_DI_ LifeStateCU LifeStateCU::vmirrored_even() const {
  int laneId = threadIdx.x & 31;
  int src = 31 - laneId;
  uint4 r;
  r.x = __shfl_sync(0xffffffffu, state.z, src);
  r.y = __shfl_sync(0xffffffffu, state.w, src);
  r.z = __shfl_sync(0xffffffffu, state.x, src);
  r.w = __shfl_sync(0xffffffffu, state.y, src);
  return LifeStateCU(r);
}

_DI_ LifeStateCU LifeStateCU::mirrored_even() const {
  return hmirrored_even().vmirrored_even();
}

_DI_ LifeStateCU LifeStateCU::mirrored() const {
  return mirrored_even().rotate_torus(1, 1);
}

_DI_ uint32_t ternary_xor(uint32_t x, uint32_t y, uint32_t z) {
  uint32_t res;
  asm("lop3.b32 %0, %1, %2, %3, 0b10010110;"
      : "=r"(res)
      : "r"(x), "r"(y), "r"(z));
  return res;
}

_DI_ uint32_t ternary_maj(uint32_t x, uint32_t y, uint32_t z) {
  uint32_t res;
  asm("lop3.b32 %0, %1, %2, %3, 0b11101000;"
      : "=r"(res)
      : "r"(x), "r"(y), "r"(z));
  return res;
}

_DI_ uint32_t calculate_f(uint32_t a, uint32_t ar, uint32_t e) {
  uint32_t res;
  asm("lop3.b32 %0, %1, %2, %3, 0b01110110;"
      : "=r"(res)
      : "r"(a), "r"(ar), "r"(e));
  return res;
}

_DI_ uint32_t calculate_g(uint32_t d, uint32_t f, uint32_t a) {
  uint32_t res;
  asm("lop3.b32 %0, %1, %2, %3, 0b11000010;"
      : "=r"(res)
      : "r"(d), "r"(f), "r"(a));
  return res;
}

_DI_ uint32_t calculate_h(uint32_t g, uint32_t bl, uint32_t br) {
  uint32_t res;
  asm("lop3.b32 %0, %1, %2, %3, 0b00010110;"
      : "=r"(res)
      : "r"(g), "r"(bl), "r"(br));
  return res;
}

_DI_ uint32_t calculate_i(uint32_t h, uint32_t f, uint32_t g) {
  uint32_t res;
  asm("lop3.b32 %0, %1, %2, %3, 0b11100000;"
      : "=r"(res)
      : "r"(h), "r"(f), "r"(g));
  return res;
}

/**
 * Advance a 64x64 torus by one generation using a single warp.
 *
 * Each thread holds a 64x2 rectangle of the torus in a uint4
 * datatype (representing a 2x2 array of 32x1 rectangles).
 */
_DI_ uint4 advance(uint4 old) {

  // Precompute the lane indices of the threads representing
  // the 64x2 rectangles immediately above and below this one.
  int upperthread = (threadIdx.x + 31) & 31;
  int lowerthread = (threadIdx.x + 1) & 31;

  // 8 funnel shifts:
  uint4 al;
  uint4 ar;
  al.x = (old.x << 1) | (old.y >> 31);
  al.y = (old.y << 1) | (old.x >> 31);
  al.z = (old.z << 1) | (old.w >> 31);
  al.w = (old.w << 1) | (old.z >> 31);
  ar.x = (old.x >> 1) | (old.y << 31);
  ar.y = (old.y >> 1) | (old.x << 31);
  ar.z = (old.z >> 1) | (old.w << 31);
  ar.w = (old.w >> 1) | (old.z << 31);

  // 8 ternary ops:
  uint4 xor3;
  uint4 maj3;
  xor3.x = ternary_xor(old.x, al.x, ar.x);
  xor3.y = ternary_xor(old.y, al.y, ar.y);
  xor3.z = ternary_xor(old.z, al.z, ar.z);
  xor3.w = ternary_xor(old.w, al.w, ar.w);
  maj3.x = ternary_maj(old.x, al.x, ar.x);
  maj3.y = ternary_maj(old.y, al.y, ar.y);
  maj3.z = ternary_maj(old.z, al.z, ar.z);
  maj3.w = ternary_maj(old.w, al.w, ar.w);

  // 8 shuffles:
  uint4 xor3prime;
  uint4 maj3prime;
  xor3prime.x = __shfl_sync(0xffffffffu, xor3.x, lowerthread);
  xor3prime.y = __shfl_sync(0xffffffffu, xor3.y, lowerthread);
  xor3prime.z = __shfl_sync(0xffffffffu, xor3.z, upperthread);
  xor3prime.w = __shfl_sync(0xffffffffu, xor3.w, upperthread);
  maj3prime.x = __shfl_sync(0xffffffffu, maj3.x, lowerthread);
  maj3prime.y = __shfl_sync(0xffffffffu, maj3.y, lowerthread);
  maj3prime.z = __shfl_sync(0xffffffffu, maj3.z, upperthread);
  maj3prime.w = __shfl_sync(0xffffffffu, maj3.w, upperthread);

  // 24 ternary ops:
  uint4 newstate;
  {
    uint4 d;
    uint4 e;
    uint4 f;
    uint4 g;
    uint4 h;
    d.x = ternary_maj(xor3.z, xor3prime.z, al.x);
    d.y = ternary_maj(xor3.w, xor3prime.w, al.y);
    d.z = ternary_maj(xor3.x, xor3prime.x, al.z);
    d.w = ternary_maj(xor3.y, xor3prime.y, al.w);
    e.x = ternary_xor(xor3.z, xor3prime.z, al.x);
    e.y = ternary_xor(xor3.w, xor3prime.w, al.y);
    e.z = ternary_xor(xor3.x, xor3prime.x, al.z);
    e.w = ternary_xor(xor3.y, xor3prime.y, al.w);
    f.x = calculate_f(old.x, ar.x, e.x);
    f.y = calculate_f(old.y, ar.y, e.y);
    f.z = calculate_f(old.z, ar.z, e.z);
    f.w = calculate_f(old.w, ar.w, e.w);
    g.x = calculate_g(d.x, f.x, old.x);
    g.y = calculate_g(d.y, f.y, old.y);
    g.z = calculate_g(d.z, f.z, old.z);
    g.w = calculate_g(d.w, f.w, old.w);
    h.x = calculate_h(g.x, maj3.z, maj3prime.z);
    h.y = calculate_h(g.y, maj3.w, maj3prime.w);
    h.z = calculate_h(g.z, maj3.x, maj3prime.x);
    h.w = calculate_h(g.w, maj3.y, maj3prime.y);
    newstate.x = calculate_i(h.x, f.x, g.x);
    newstate.y = calculate_i(h.y, f.y, g.y);
    newstate.z = calculate_i(h.z, f.z, g.z);
    newstate.w = calculate_i(h.w, f.w, g.w);
  }
  return newstate;
}

_DI_ cuda::std::pair<LifeStateCU, LifeStateCU> LifeStateCU::interaction_counts() const {
  int upperthread = (threadIdx.x + 31) & 31;
  int lowerthread = (threadIdx.x + 1) & 31;

  // 8 funnel shifts:
  uint4 al;
  uint4 ar;
  al.x = (state.x << 1) | (state.y >> 31);
  al.y = (state.y << 1) | (state.x >> 31);
  al.z = (state.z << 1) | (state.w >> 31);
  al.w = (state.w << 1) | (state.z >> 31);
  ar.x = (state.x >> 1) | (state.y << 31);
  ar.y = (state.y >> 1) | (state.x << 31);
  ar.z = (state.z >> 1) | (state.w << 31);
  ar.w = (state.w >> 1) | (state.z << 31);

  // 8 ternary ops:
  uint4 xor3;
  uint4 maj3;
  xor3.x = ternary_xor(state.x, al.x, ar.x);
  xor3.y = ternary_xor(state.y, al.y, ar.y);
  xor3.z = ternary_xor(state.z, al.z, ar.z);
  xor3.w = ternary_xor(state.w, al.w, ar.w);
  maj3.x = ternary_maj(state.x, al.x, ar.x);
  maj3.y = ternary_maj(state.y, al.y, ar.y);
  maj3.z = ternary_maj(state.z, al.z, ar.z);
  maj3.w = ternary_maj(state.w, al.w, ar.w);

  // 8 shuffles:
  uint4 xor3prime;
  uint4 maj3prime;
  xor3prime.x = __shfl_sync(0xffffffffu, xor3.x, lowerthread);
  xor3prime.y = __shfl_sync(0xffffffffu, xor3.y, lowerthread);
  xor3prime.z = __shfl_sync(0xffffffffu, xor3.z, upperthread);
  xor3prime.w = __shfl_sync(0xffffffffu, xor3.w, upperthread);
  maj3prime.x = __shfl_sync(0xffffffffu, maj3.x, lowerthread);
  maj3prime.y = __shfl_sync(0xffffffffu, maj3.y, lowerthread);
  maj3prime.z = __shfl_sync(0xffffffffu, maj3.z, upperthread);
  maj3prime.w = __shfl_sync(0xffffffffu, maj3.w, upperthread);

  uint4 fs;
  uint4 fc;
  fc.x = ternary_maj(xor3.z, xor3prime.z, xor3.x);
  fc.y = ternary_maj(xor3.w, xor3prime.w, xor3.y);
  fc.z = ternary_maj(xor3.x, xor3prime.x, xor3.z);
  fc.w = ternary_maj(xor3.y, xor3prime.y, xor3.w);
  fs.x = ternary_xor(xor3.z, xor3prime.z, xor3.x);
  fs.y = ternary_xor(xor3.w, xor3prime.w, xor3.y);
  fs.z = ternary_xor(xor3.x, xor3prime.x, xor3.z);
  fs.w = ternary_xor(xor3.y, xor3prime.y, xor3.w);

  uint4 cs;
  uint4 cc;
  cc.x = ternary_maj(maj3.z, maj3prime.z, maj3.x);
  cc.y = ternary_maj(maj3.w, maj3prime.w, maj3.y);
  cc.z = ternary_maj(maj3.x, maj3prime.x, maj3.z);
  cc.w = ternary_maj(maj3.y, maj3prime.y, maj3.w);
  cs.x = ternary_xor(maj3.z, maj3prime.z, maj3.x);
  cs.y = ternary_xor(maj3.w, maj3prime.w, maj3.y);
  cs.z = ternary_xor(maj3.x, maj3prime.x, maj3.z);
  cs.w = ternary_xor(maj3.y, maj3prime.y, maj3.w);

  uint4 out1;
  uint4 out2;
  out1.x = ~state.x & ~cc.x & fs.x & ~cs.x & ~fc.x;
  out1.y = ~state.y & ~cc.y & fs.y & ~cs.y & ~fc.y;
  out1.z = ~state.z & ~cc.z & fs.z & ~cs.z & ~fc.z;
  out1.w = ~state.w & ~cc.w & fs.w & ~cs.w & ~fc.w;
  out2.x = ~state.x & ~cc.x & ~fs.x & (cs.x ^ fc.x);
  out2.y = ~state.y & ~cc.y & ~fs.y & (cs.y ^ fc.y);
  out2.z = ~state.z & ~cc.z & ~fs.z & (cs.z ^ fc.z);
  out2.w = ~state.w & ~cc.w & ~fs.w & (cs.w ^ fc.w);

  return {LifeStateCU(out1), LifeStateCU(out2)};
}

_DI_ LifeStateCU LifeStateCU::advance() const {
  return LifeStateCU(::advance(state));
}

_DI_ void LifeStateCU::advance_inplace() {
  state = ::advance(state);
}

_DI_ void rotate_torus_inplace(uint4 &t, int rh, int rv) {
  if (rv & 63) {
    // translate vertically:
    uint4 d;
    d.x = (rv & 1) ? t.z : t.x;
    d.y = (rv & 1) ? t.w : t.y;
    d.z = (rv & 1) ? t.x : t.z;
    d.w = (rv & 1) ? t.y : t.w;
    int upperthread = (((-rv) >> 1) + threadIdx.x) & 31;
    int lowerthread = (((-rv + 1) >> 1) + threadIdx.x) & 31;
    t.x = __shfl_sync(0xffffffffu, d.x, upperthread);
    t.y = __shfl_sync(0xffffffffu, d.y, upperthread);
    t.z = __shfl_sync(0xffffffffu, d.z, lowerthread);
    t.w = __shfl_sync(0xffffffffu, d.w, lowerthread);
  }

  if (rh & 63) {
    // translate horizontally:
    uint4 d;
    d.x = (rh & 32) ? t.y : t.x;
    d.y = (rh & 32) ? t.x : t.y;
    d.z = (rh & 32) ? t.w : t.z;
    d.w = (rh & 32) ? t.z : t.w;
    int sa = rh & 31;
    t.x = (d.x << sa) | (d.y >> (32 - sa));
    t.y = (d.y << sa) | (d.x >> (32 - sa));
    t.z = (d.z << sa) | (d.w >> (32 - sa));
    t.w = (d.w << sa) | (d.z >> (32 - sa));
  }
}

_DI_ void LifeStateCU::rotate_torus_inplace(int rh, int rv) {
  ::rotate_torus_inplace(state, rh, rv);
}

_DI_ LifeStateCU LifeStateCU::rotate_torus(int rh, int rv) const {
  LifeStateCU res = *this;
  ::rotate_torus_inplace(res.state, rh, rv);
  return res;
}

// Slow and dumb
// `other` should have lower population
_DI_ LifeStateCU LifeStateCU::convolve(LifeStateCU other) const {
  // TODO: This could store the nonzero uint4 and handle it at once, likely a bit faster
  LifeStateCU r;
  while (!other.empty()) {
    auto cell = other.first_on();
    other.erase(cell);
    r |= rotate_torus(cell);
    if ((~r).empty()) return r;
  }
  return r;  
}

_DI_ LifeStateCU LifeStateCU::match_live(LifeStateCU live) const {
  return ~(~*this).convolve(live.mirrored());
}

_DI_ LifeStateCU LifeStateCU::match(LifeStateCU live, LifeStateCU dead) const {
  LifeStateCU live_matches = match_live(live);
  if (live_matches.empty()) return LifeStateCU();
  return live_matches & ~convolve(dead.mirrored());
}

_DI_ LifeStateCU LifeStateCU::zoi() const {
  LifeStateCU vert = rotate_torus(0, -1) | *this | rotate_torus(0, 1);
  return vert.rotate_torus(-1, 0) | vert | vert.rotate_torus(1, 0);
}

/**
 * Determine whether the pattern is too large to fit in a single tile.
 */
_DI_ bool escaped_bounding_box(uint4 &b) {

  unsigned int xz = b.x | b.z;
  unsigned int yw = b.y | b.w;

  int laneId = threadIdx.x & 31;
  bool hasMask = (laneId >= 3) && (laneId < 29);
  unsigned int xzmask = (hasMask) ? 0x0000003fu : 0xffffffffu;
  unsigned int ywmask = (hasMask) ? 0xfc000000u : 0xffffffffu;

  if (__ballot_sync(0xffffffffu, (xzmask & xz) | (ywmask & yw)) == 0) {
    // currently inside bounding box:
    return false;
  }

  // We're outside the 52x52 region, but it might be possible to
  // recentre the pattern such that it fits again. We examine the
  // horizontal and vertical extent:

  unsigned int active_threads = __ballot_sync(0xffffffffu, xz | yw);

  int vt;
  if ((active_threads & 0xe0000007u) == 0) {
    vt = 0;
  } else if ((active_threads & 0xf0000003u) == 0) {
    vt = -2;
  } else if ((active_threads & 0xc000000fu) == 0) {
    vt = 2;
  } else if ((active_threads & 0xf8000001u) == 0) {
    vt = -4;
  } else if ((active_threads & 0x8000001fu) == 0) {
    vt = 4;
  } else if ((active_threads & 0xfc000000u) == 0) {
    vt = -6;
  } else if ((active_threads & 0x0000003fu) == 0) {
    vt = 6;
  } else {
    // pattern is too tall:
    return true;
  }

  xz |= __shfl_xor_sync(0xffffffffu, xz, 1);
  yw |= __shfl_xor_sync(0xffffffffu, yw, 1);
  xz |= __shfl_xor_sync(0xffffffffu, xz, 2);
  yw |= __shfl_xor_sync(0xffffffffu, yw, 2);
  xz |= __shfl_xor_sync(0xffffffffu, xz, 4);
  yw |= __shfl_xor_sync(0xffffffffu, yw, 4);
  xz |= __shfl_xor_sync(0xffffffffu, xz, 8);
  yw |= __shfl_xor_sync(0xffffffffu, yw, 8);
  xz |= __shfl_xor_sync(0xffffffffu, xz, 16);
  yw |= __shfl_xor_sync(0xffffffffu, yw, 16);

  int ht;
  if (xz == 0) {
    ht = 16;
  } else if (yw == 0) {
    ht = -16;
  } else {
    int rightborder = __clz(yw);
    int leftborder = __clz(__brev(xz));

    if (leftborder + rightborder < 12) {
      // pattern is too wide:
      return true;
    }

    ht = (leftborder - rightborder) >> 1;
  }

  // recentre pattern:
  rotate_torus_inplace(b, ht, vt);
  return false;
}

_DI_ bool LifeStateCU::escaped_bounding_box() {
  return ::escaped_bounding_box(state);
}

// /**
//  * Advances a single tile and checks whether it has settled down.
//  * If it settles down, return 0; otherwise, return the current
//  * generation count.
//  */
// _DI_ int advance_initial(uint4 &a) {

//     uint4 b = a;

//     // Run for 30 generations without any checks. It is impossible for
//     // the pattern to escape the 52x52 bounding box in this time period,
//     // and only 1.6% of soups stabilise this quickly so we do not give
//     // up much by skipping the stabilisation checks:
//     #ifdef C1_SYMMETRY
//     #define initial_gens 30
//     #else
//     #define initial_gens 10
//     #endif

//     #pragma unroll 2
//     for (int i = 0; i < initial_gens; i++) {
//         b = advance_torus(b);
//     }

//     int age = initial_gens - 6;

//     // Run until generation 600, checking for bounding box escapement
//     // and 2-periodicity. This loop should fit comfortably in the GPU
//     // icache. About 36% of soups stabilise in this loop.
//     do {

//         age += 6;
//         a = b;

//         b = advance_torus(b);
//         b = advance_torus(b);

//         if (hh::ballot_32((a.x ^ b.x) | (a.y ^ b.y) | (a.z ^ b.z) | (a.w ^
//         b.w)) == 0) {
//             // periodic with period 2:
//             return 0;
//         }

//         b = advance_torus(b);
//         b = advance_torus(b);
//         b = advance_torus(b);
//         b = advance_torus(b);

//         if (escaped_bounding_box(b)) {
//             break;
//         }
//     } while (age < 600);

//     // Conveniently, a and b differ by 6 generations, so we can check
//     // for soups which stabilise with the production of a pulsar.
//     if (hh::ballot_32((a.x ^ b.x) | (a.y ^ b.y) | (a.z ^ b.z) | (a.w ^ b.w))
//     == 0) {
//         // periodic with period 6:
//         return 0;
//     }

//     // ================================================================
//     // |                  CHECK FOR ESCAPING GLIDERS                  |
//     // ================================================================

//     // Take intersection of two snapshots separated by 8 generations:
//     uint4 c;
//     b = advance_torus(b);
//     b = advance_torus(b);
//     c.x = a.x & b.x;
//     c.y = a.y & b.y;
//     c.z = a.z & b.z;
//     c.w = a.w & b.w;

//     {
//         uint4 d;
//         d = advance_torus(c);
//         d = advance_torus(d);

//         if (hh::ballot_32((c.x ^ d.x) | (c.y ^ d.y) | (c.z ^ d.z) | (c.w ^
//         d.w)) != 0) {
//             // core does not have period 2:
//             return age;
//         }
//     }

//     {
//         int totpop = __popc(c.x ^ a.x) + __popc(c.y ^ a.y) + __popc(c.z ^
//         a.z) + __popc(c.w ^ a.w); totpop     = totpop << 16; totpop    +=
//         __popc(c.x ^ b.x) + __popc(c.y ^ b.y) + __popc(c.z ^ b.z) +
//         __popc(c.w ^ b.w);

//         totpop += hh::shuffle_xor_32(totpop, 1);
//         totpop += hh::shuffle_xor_32(totpop, 2);
//         totpop += hh::shuffle_xor_32(totpop, 4);
//         totpop += hh::shuffle_xor_32(totpop, 8);
//         totpop += hh::shuffle_xor_32(totpop, 16);

//         if (totpop != 0x00050005u) {
//             // diff has wrong population to be a glider:
//             return age;
//         }
//     }

//     {
//         // Advance by another 6 generations to ensure glider has fully
//         escaped: for (int i = 0; i < 4; i++) {
//             b = advance_torus(b);
//         }

//         int laneId = threadIdx.x & 31;
//         bool hasMask = (laneId >= 3) && (laneId < 29);
//         unsigned int xzmask = (hasMask) ? 0xffffffc0u : 0;
//         unsigned int ywmask = (hasMask) ? 0x03ffffffu : 0;

//         int totpop = 0;

//         for (int i = 0; i < 4; i++) {
//             b = advance_torus(b);
//             b = advance_torus(b);
//             if (hh::ballot_32(((b.x & xzmask) ^ c.x) | ((b.y & ywmask) ^ c.y)
//             | ((b.z & xzmask) ^ c.z) | ((b.w & ywmask) ^ c.w))) { return
//             false; } totpop     = totpop << 8; totpop    += __popc(c.x ^ b.x)
//             + __popc(c.y ^ b.y) + __popc(c.z ^ b.z) + __popc(c.w ^ b.w);
//         }

//         totpop += hh::shuffle_xor_32(totpop, 1);
//         totpop += hh::shuffle_xor_32(totpop, 2);
//         totpop += hh::shuffle_xor_32(totpop, 4);
//         totpop += hh::shuffle_xor_32(totpop, 8);
//         totpop += hh::shuffle_xor_32(totpop, 16);

//         return ((totpop == 0x05050505u) ? 0 : age);
//     }
// }

// _DI_ int LifeStateCU::advance_initial() {
//     return ::advance_initial(state);
// }

// _DI_ int advance_tile_inplace(uint4 &a, int rh, int rv) {

//     uint4 b = advance_torus(a);
//     uint4 c = advance_torus(b);

//     b = advance_torus(c);
//     b = advance_torus(b);
//     b = advance_torus(b);
//     b = advance_torus(b);

//     unsigned int leftdiff;
//     unsigned int rightdiff;

//     {
//         int laneId = threadIdx.x & 31;
//         bool hasMask = (laneId >= 3) && (laneId < 29);
//         unsigned int xzmask = (hasMask) ? 0xffffffc0u : 0;
//         unsigned int ywmask = (hasMask) ? 0x03ffffffu : 0;

//         uint4 xordiff;
//         xordiff.x = (a.x ^ b.x) & xzmask;
//         xordiff.y = (a.y ^ b.y) & ywmask;
//         xordiff.z = (a.z ^ b.z) & xzmask;
//         xordiff.w = (a.w ^ b.w) & ywmask;

//         // update central 52-by-52 tile:
//         a.x ^= xordiff.x;
//         a.y ^= xordiff.y;
//         a.z ^= xordiff.z;
//         a.w ^= xordiff.w;

//         xordiff.x |= xordiff.z;
//         xordiff.y |= xordiff.w;
//         leftdiff  = (xordiff.x >> 6) | (xordiff.y << 26);
//         rightdiff = (xordiff.y << 6) | (xordiff.x >> 26);
//     }

//     int flags = 64;
//     flags |= (hh::ballot_32(rightdiff & 0xfc000000u) ? 1 : 0);
//     flags |= (hh::ballot_32( leftdiff & 0x0000003fu) ? 8 : 0);
//     leftdiff  = hh::ballot_32(leftdiff);
//     rightdiff = hh::ballot_32(rightdiff);

//     if ((leftdiff | rightdiff) == 0) { return 0; }

//     flags |= ((rightdiff & 0x00000038u) ? 2  : 0);
//     flags |= (( leftdiff & 0x00000038u) ? 4  : 0);
//     flags |= (( leftdiff & 0x1c000000u) ? 16 : 0);
//     flags |= ((rightdiff & 0x1c000000u) ? 32 : 0);

//     // Escaping glider/*WSS detection:
//     if ((rh & 63) || (rv & 63)) {

//         unsigned int xz = b.x | b.z;
//         unsigned int yw = b.y | b.w;

//         if (hh::ballot_32(xz | yw)) {

//             int laneId = threadIdx.x & 31;
//             bool hasMask = (laneId >= 3) && (laneId < 29);
//             unsigned int xzmask = (hasMask) ? 0x0000003fu : 0xffffffffu;
//             unsigned int ywmask = (hasMask) ? 0xfc000000u : 0xffffffffu;

//             if (hh::ballot_32((xz & xzmask) | (yw & ywmask)) == 0) {

//                 rotate_torus_inplace(c, rh, rv);

//                 if (hh::ballot_32((c.x ^ b.x) | (c.y ^ b.y) | (c.z ^ b.z) |
//                 (c.w ^ b.w)) == 0) {

//                     int totpop = __popc(b.x) + __popc(b.y) + __popc(b.z) +
//                     __popc(b.w); totpop += hh::shuffle_xor_32(totpop, 1);
//                     totpop += hh::shuffle_xor_32(totpop, 2);
//                     totpop += hh::shuffle_xor_32(totpop, 4);
//                     totpop += hh::shuffle_xor_32(totpop, 8);
//                     totpop += hh::shuffle_xor_32(totpop, 16);

//                     if (totpop <= 17) {
//                         // either a single LWSS, MWSS, or HWSS, or between
//                         // 1 and 3 gliders. Remove them from the universe:
//                         a.x &= xzmask;
//                         a.y &= ywmask;
//                         a.z &= xzmask;
//                         a.w &= ywmask;
//                     }
//                 }
//             }
//         }
//     }

//     return flags;
// }

// _DI_ int LifeStateCU::advance_tile_inplace(int rh, int rv) {
//     return ::advance_tile_inplace(state, rh, rv);
// }

// __global__ void exhaustFirstTile(uint32_cu *hashes, uint32_cu *interesting,
// uint4 *output) {

//     int pos = (threadIdx.x + blockIdx.x * blockDim.x) >> 5;

//     uint32_t *thishash = hashes + (pos << 3);

//     LifeStateCU a(load_hash(thishash));

//     #ifdef HREFLECT_EVEN
//     a.hreflect_even();
//     #endif
//     #ifdef HREFLECT_ODD
//     a.hreflect_odd();
//     #endif
//     #ifdef VREFLECT_EVEN
//     a.vreflect_even();
//     #endif
//     #ifdef VREFLECT_ODD
//     a.vreflect_odd();
//     #endif
//     #ifdef RESTRICT_C2_1
//     a.restrict_C2_1();
//     #endif
//     #ifdef RESTRICT_C2_2
//     a.restrict_C2_2();
//     #endif
//     #ifdef RESTRICT_C2_4
//     a.restrict_C2_4();
//     #endif

//     int gencount = a.advance_initial();

//     interesting[pos] = gencount;

//     if (gencount && output) {
//         int laneId = threadIdx.x & 31;
//         uint4 *uverse = output + (((uint64_t) pos) << 12);
//         uverse[laneId + 32] = a.raw();

//         LifeStateCU b;
//         if (laneId == 0) { b.raw().y = 127; }

//         uverse[laneId] = b.raw();
//     }
// }

// Prints to match LifeAPI, maybe transposed to what you expect
_DI_ void LifeStateCU::print() const {
  unsigned eol_count = 0;

  for (unsigned i = 0; i < 64; i++) {
    bool s = get((i + 32) & 63, 32);
    char last_val = s ? 'o' : 'b';
    unsigned run_count = 0;

    for (unsigned j = 0; j < 64; j++) {
      bool s = get((i + 32) & 63, (j + 32) & 63);
      char val = s ? 'o' : 'b';
      
      // Flush linefeeds if we find a live cell
      if (val != 'b' && eol_count > 0) {
        if (eol_count > 1)
          if ((threadIdx.x & 31) == 0) printf("%d", eol_count);

        if ((threadIdx.x & 31) == 0) printf("$");

        eol_count = 0;
      }

      // Flush current run if val changes
      if (val != last_val) {
        if (run_count > 1)
            if ((threadIdx.x & 31) == 0) printf("%d", run_count);
        if ((threadIdx.x & 31) == 0) printf("%c", last_val);
        run_count = 0;
      }

      run_count++;
      last_val = val;
    }

    // Flush run of live cells at end of line
    if (last_val != 'b') {
      if (run_count > 1)
        if ((threadIdx.x & 31) == 0) printf("%d", run_count);

      if ((threadIdx.x & 31) == 0) printf("%c", last_val);

      run_count = 0;
    }

    eol_count++;
  }
  if ((threadIdx.x & 31) == 0) printf("!\n");
}
