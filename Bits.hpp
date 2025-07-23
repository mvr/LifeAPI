#pragma once

#include <cstdint>
#include <array>

// GCC
#ifdef __GNUC__
#ifndef __clang__

constexpr uint64_t reverse_uint64_t(uint64_t x) {
  const uint64_t h1 = 0x5555555555555555ULL;
  const uint64_t h2 = 0x3333333333333333ULL;
  const uint64_t h4 = 0x0F0F0F0F0F0F0F0FULL;
  const uint64_t v1 = 0x00FF00FF00FF00FFULL;
  const uint64_t v2 = 0x0000FFFF0000FFFFULL;
  x = ((x >> 1) & h1) | ((x & h1) << 1);
  x = ((x >> 2) & h2) | ((x & h2) << 2);
  x = ((x >> 4) & h4) | ((x & h4) << 4);
  x = ((x >> 8) & v1) | ((x & v1) << 8);
  x = ((x >> 16) & v2) | ((x & v2) << 16);
  x = (x >> 32) | (x << 32);
  return x;
}

#define __builtin_bitreverse64 ::reverse_uint64_t
#endif
#endif

constexpr unsigned longest_run_uint64_t(uint64_t x) {
  if(x == 0)
    return 0;

  if(x == ~0ULL)
    return 64;

  std::array<uint64_t, 6> pow2runs = {0};
  for (unsigned n = 0; n < 6; n++) {
    pow2runs[n] = x;
    x &= std::rotl(x, 1 << n);
  }

  unsigned last = 5;
  for (unsigned n = 0; n < 6; n++) {
    if (pow2runs[n] == 0) {
      last = n-1;
      break;
    }
  }

  x = pow2runs[last];
  unsigned count = 1 << last;

  for (int n = 5; n >= 0; n--) {
    uint64_t y = (x & std::rotl(x, 1 << n));
    if (y != 0 && (unsigned)n < last) {
      count += 1 << n;
      x = y;
    }
  }

  return count;
}

constexpr unsigned populated_width_uint64_t(uint64_t x) {
  if (x == 0)
    return 0;

  // First, shift to try and make it 2^n-1
  int lzeroes = std::countr_zero(x);
  x = std::rotr(x, lzeroes);
  int tones = std::countl_one(x);
  x = std::rotl(x, tones);

  if ((x & (x + 1)) == 0)
    return std::countr_one(x);

  // Otherwise do the long way
  return 64 - longest_run_uint64_t(~x);
}

constexpr unsigned longest_run_uint32_t(uint32_t x) {
  if(x == 0)
    return 0;

  if(x == ~0U)
    return 32;

  std::array<uint32_t, 5> pow2runs = {0};
  for (unsigned n = 0; n < 5; n++) {
    pow2runs[n] = x;
    x &= std::rotl(x, 1 << n);
  }

  unsigned last = 4;
  for (unsigned n = 0; n < 5; n++) {
    if (pow2runs[n] == 0) {
      last = n-1;
      break;
    }
  }

  x = pow2runs[last];
  unsigned count = 1 << last;

  for (int n = 4; n >= 0; n--) {
    uint64_t y = (x & std::rotl(x, 1 << n));
    if (y != 0 && (unsigned)n < last) {
      count += 1 << n;
      x = y;
    }
  }

  return count;
}

constexpr unsigned populated_width_uint32_t(uint32_t x) {
  if (x == 0)
    return 0;

  // First, shift to try and make it 2^n-1
  int lzeroes = std::countr_zero(x);
  x = std::rotr(x, lzeroes);
  int tones = std::countl_one(x);
  x = std::rotl(x, tones);

  if ((x & (x + 1)) == 0)
    return std::countr_one(x);

  return 32 - longest_run_uint32_t(~x);
}

constexpr uint64_t convolve_uint64_t(uint64_t x, uint64_t y) {
  if(y == 0)
    return 0;

  uint64_t result = 0;
  while (x != 0) {
    int lsb = std::countr_zero(x);
    result |= std::rotl(y, lsb);
    x &= ~(((uint64_t)1) << lsb);
  }
  return result;
}

// Unbelievable that there is no standard way to do this
[[nodiscard]] inline uint64_t combine_hashes(uint64_t a, uint64_t b) {
  return a ^ (b + 0x9e3779b9 + (a << 6) + (a >> 2));
}
