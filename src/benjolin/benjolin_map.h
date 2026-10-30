// The Benjolin build's panel, as data: what each of the 18 knobs on the
// three pages is, its first-boot value, and how a panel becomes the
// engine's Params. Portable (no Daisy or SDK headers), so the native tests
// check the same mapping the firmware runs (test/test_benjolin.cpp).
//
// Knob values here are the SDK's VirtualKnob::Value(): a Selector gives its
// zone index (floor(stored * zones)), a Linear(lo, hi) gives
// lo + stored * (hi - lo). The shim builds its knobs from kSpec.
#pragma once
#include <cstdint>

#include "hypjolin_engine.h"

namespace benjolin {

namespace hj = hypjolin;

constexpr uint8_t kNumPages = 3, kNumPots = 6;
enum : uint8_t { kPageJolin = 0, kPageFilter = 1, kPagePatch = 2 };

// Sources a PATCH row can take: the Benjolin's eight outputs, Off first.
constexpr uint8_t kNumPatchSrc = 9;
constexpr uint8_t kPatchSrc[kNumPatchSrc] = {
    hj::kSrcOff,    hj::kSrcTriA, hj::kSrcTriB,    hj::kSrcPulseA, hj::kSrcPulseB,
    hj::kSrcXor,    hj::kSrcPwm,  hj::kSrcRungler, hj::kSrcLopass};
// The audio outs and J7/J8: the same eight, no Off.
constexpr uint8_t kNumOut = 8;
constexpr uint8_t kOutSrc[kNumOut] = {
    hj::kSrcTriA, hj::kSrcTriB, hj::kSrcPulseA, hj::kSrcPulseB,
    hj::kSrcXor,  hj::kSrcPwm,  hj::kSrcRungler, hj::kSrcLopass};
// Filter INPUT: the first five hj::FiltIn (PWM, Ext, XOR, Tri A, Tri B);
// the two after them are Hypster outputs.
constexpr uint8_t kNumFiltIn = 5;
// The three PATCH rows, top to bottom.
constexpr uint8_t kNumPatchRows = 3;
constexpr uint8_t kPatchDest[kNumPatchRows] = {hj::kDestOscA, hj::kDestOscB, hj::kDestFilter};

// Where each of J3..J6 goes (Settings "Jacks").
enum : uint8_t { kJackOscA, kJackOscB, kJackFilter, kJackLoop, kNumJackDest };
constexpr uint8_t kNumCvIn = 4;
constexpr uint8_t kJackDefault[kNumCvIn] = {kJackOscA, kJackOscB, kJackFilter, kJackLoop};
// LOOP gate thresholds, volts (a Schmitt trigger, as mi::Gate's).
constexpr float kGateOn = 1.5f, kGateOff = 0.5f;

// One knob: zones > 0 is a selector of that many zones, else Linear(lo, hi).
struct KnobSpec {
  uint8_t zones;
  float lo, hi;
};
constexpr KnobSpec kLin = {0, 0.f, 1.f};
constexpr KnobSpec kBip = {0, -1.f, 1.f};
constexpr KnobSpec Sel(uint8_t n) { return {n, 0.f, 0.f}; }

// Pots, front view: [P1] B1 [P2] / [P3] B2 [P4] / [P5] B3 [P6].
//   JOLIN   Osc A   Osc B  / Run>A  Run>B  / Run>F   Loop offset
//   FILTER  Cutoff  Reso   / Input  Drive  / Out L   Out R
//   PATCH   >Osc A  amount / >Osc B amount / >Filter amount
constexpr KnobSpec kSpec[kNumPages][kNumPots] = {
    {kLin, kLin, kLin, kLin, kLin, kLin},
    {kLin, kLin, Sel(kNumFiltIn), kLin, Sel(kNumOut), Sel(kNumOut)},
    {Sel(kNumPatchSrc), kBip, Sel(kNumPatchSrc), kBip, Sel(kNumPatchSrc), kBip},
};

inline int Index(float v, int n) {
  const int i = int(v);
  return i < 0 ? 0 : (i >= n ? n - 1 : i);
}
inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float ClampBi(float v) { return v < -1.f ? -1.f : (v > 1.f ? 1.f : v); }

// A stored pot position (0..1) as the SDK turns it into Value(), and back.
inline float ValueOf(const KnobSpec& k, float stored) {
  stored = Clamp01(stored);
  if (k.zones) {
    float z = stored * float(k.zones);
    return z >= float(k.zones) ? float(k.zones - 1) : float(int(z));
  }
  return k.lo + stored * (k.hi - k.lo);
}
inline float StoredOf(const KnobSpec& k, float value) {
  if (k.zones) return (float(Index(value, k.zones)) + 0.5f) / float(k.zones);
  return Clamp01((value - k.lo) / (k.hi - k.lo));
}

// Index of an hj::Src in a table, or 0.
inline uint8_t IndexOf(const uint8_t* table, uint8_t n, uint8_t src) {
  for (uint8_t i = 0; i < n; ++i)
    if (table[i] == src) return i;
  return 0;
}

// A whole panel's knob values.
struct Panel {
  float v[kNumPages][kNumPots];
};

// First boot: the engine's Benjolin defaults, plus the PATCH page's own:
// triangle B wobbling osc A (the Benjolin's classic cross-patch) and,
// gently, the filter; the osc B row parked on Pulse A at noon (off).
inline Panel Defaults() {
  const hj::Params d;
  Panel p = {{
      {d.osc_a, d.osc_b, d.run_a, d.run_b, d.run_f, d.loop_offset},
      {d.cutoff, d.reso, float(d.filt_in), d.drive, float(IndexOf(kOutSrc, kNumOut, hj::kSrcLopass)),
       float(IndexOf(kOutSrc, kNumOut, hj::kSrcXor))},
      {float(IndexOf(kPatchSrc, kNumPatchSrc, hj::kSrcTriB)), 0.15f,
       float(IndexOf(kPatchSrc, kNumPatchSrc, hj::kSrcPulseA)), 0.f,
       float(IndexOf(kPatchSrc, kNumPatchSrc, hj::kSrcTriB)), 0.10f},
  }};
  return p;
}

// The panel as engine Params. out_l / out_r get the hj::Src for J9 / J10.
inline hj::Params ToParams(const Panel& pn, bool loop, bool ext_clock, uint8_t& out_l,
                           uint8_t& out_r) {
  hj::Params P;
  const float* j = pn.v[kPageJolin];
  P.osc_a = Clamp01(j[0]);
  P.osc_b = Clamp01(j[1]);
  P.run_a = Clamp01(j[2]);
  P.run_b = Clamp01(j[3]);
  P.run_f = Clamp01(j[4]);
  P.loop_offset = Clamp01(j[5]);
  P.loop = loop;

  const float* f = pn.v[kPageFilter];
  P.cutoff = Clamp01(f[0]);
  P.reso = Clamp01(f[1]);
  P.filt_in = uint8_t(Index(f[2], kNumFiltIn));
  P.drive = Clamp01(f[3]);
  out_l = kOutSrc[Index(f[4], kNumOut)];
  out_r = kOutSrc[Index(f[5], kNumOut)];

  // No Hypster: its three inputs are unpatched, and it does not run.
  P.hypster = false;
  for (uint8_t d = 0; d < hj::kNumDest; ++d) {
    P.src[d] = hj::kSrcOff;
    P.amt[d] = 0.f;
    P.ext_cv[d] = 0.f;
  }
  const float* x = pn.v[kPagePatch];
  for (uint8_t r = 0; r < kNumPatchRows; ++r) {
    P.src[kPatchDest[r]] = kPatchSrc[Index(x[2 * r], kNumPatchSrc)];
    P.amt[kPatchDest[r]] = ClampBi(x[2 * r + 1]);
  }
  P.ext_clock = ext_clock;
  return P;
}

// J3..J6, once per audio block: volts into Osc A / Osc B / Filter (1 V/oct),
// or a LOOP gate (high flips the LOOP switch while it is held). gate[] is
// each jack's Schmitt state, kept by the caller between blocks.
inline void ApplyJacks(hj::Params& P, const uint8_t dest[kNumCvIn], const float volts[kNumCvIn],
                       bool gate[kNumCvIn]) {
  bool any_gate = false;
  for (uint8_t i = 0; i < kNumCvIn; ++i) {
    switch (dest[i]) {
      case kJackOscA: P.ext_cv[hj::kDestOscA] += volts[i]; gate[i] = false; break;
      case kJackOscB: P.ext_cv[hj::kDestOscB] += volts[i]; gate[i] = false; break;
      case kJackFilter: P.ext_cv[hj::kDestFilter] += volts[i]; gate[i] = false; break;
      default:
        gate[i] = gate[i] ? volts[i] > kGateOff : volts[i] > kGateOn;
        any_gate = any_gate || gate[i];
        break;
    }
  }
  if (any_gate) P.loop = !P.loop;
}

}  // namespace benjolin
