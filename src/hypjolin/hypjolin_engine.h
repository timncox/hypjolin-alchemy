// Hypjolin engine: a Benjolin (Rob Hordijk) and a Hypster (Ian Fritz)
// cross-patched, after Triglav Modular's Hypjolin (anti-copyright, 2023,
// github.com/triglav-modular/Hypjolin). Portable: no Daisy or SDK headers.
//
// Units: knob params are 0..1 unless noted. Internally the Hypster runs in
// volts (its four states swing about ±10 V, as the circuit's do); every
// patch source is normalised to about ±1 (the rungler to 0..1).
#pragma once
#include <cstdint>

namespace hypjolin {

constexpr float kSampleRate = 48000.f;

// Patch sources: what the Hypjolin's blue (Benjolin) and purple (Hypster)
// banana jacks put out, minus the -X..-U copies (a negative amount does that).
enum Src : uint8_t {
  kSrcOff,
  kSrcX, kSrcY, kSrcZ, kSrcU,              // Hypster
  kSrcTriA, kSrcTriB, kSrcPulseA, kSrcPulseB,
  kSrcXor, kSrcPwm, kSrcRungler, kSrcLopass,  // Benjolin
  kNumSrc
};

// Patch destinations: the Hypjolin's six black CV inputs, each with its own
// "cv" attenuator on the panel.
enum Dest : uint8_t { kDestOscA, kDestOscB, kDestFilter, kDestRate, kDestGain, kDestDamp, kNumDest };

// What the Benjolin filter hears. PWM is the Benjolin's own normal.
enum FiltIn : uint8_t { kFiltPwm, kFiltExt, kFiltXor, kFiltTriA, kFiltTriB, kFiltX, kFiltY, kNumFiltIn };

struct Params {
  // Benjolin
  float osc_a = 0.66f, osc_b = 0.45f;      // 18 octaves each
  float run_a = 0.25f, run_b = 0.35f, run_f = 0.40f;
  float loop_offset = 0.5f;                // 0.5 = centre
  bool loop = false;                       // the LOOP switch
  float cutoff = 0.55f, reso = 0.45f;
  uint8_t filt_in = kFiltPwm;
  float drive = 0.35f;
  // Hypster
  float rate = 0.30f, fine = 0.5f;         // fine: ±half an octave
  float gain = 0.45f, damp = 0.35f;        // GAIN 0.25..3, DAMP 0..2
  float shape = 0.5f;                      // 0 slim .. 1 fat
  bool symm = true;
  // The patchbay: one source and a bipolar amount (-1..1) per input.
  uint8_t src[kNumDest] = {kSrcX, kSrcOff, kSrcY, kSrcRungler, kSrcOff, kSrcOff};
  float amt[kNumDest] = {0.30f, 0.f, 0.25f, 0.30f, 0.f, 0.f};
  // External CV into the same inputs, in volts, summed after the patch.
  float ext_cv[kNumDest] = {0, 0, 0, 0, 0, 0};
  bool ext_clock = false;                  // rungler clocked by ext edges, not osc B
};

// Per-input scaling: what one volt does at each destination.
//   osc A/B, filter, rate: one octave per volt
//   gain: +0.3 per volt, damp: +0.2 per volt
// A patch source at full scale (±1) with amount ±1 is ±5 V.
constexpr float kPatchVolts = 5.f;

// Knob → value mappings (exposed for the tests and the UI readouts).
float OscHz(float knob);           // 0.03 Hz · 2^(18·knob)
float CutoffHz(float knob);        // 20 Hz · 2^(10·knob)
float RateHz(float knob);          // 0.05 Hz · 2^(17·knob)
float GainValue(float knob);       // 0.25 .. 3
float DampValue(float knob);       // 0 .. 2

class Hypster {
 public:
  void Reset();
  // One sample. rate_hz is the nominal rate; gain, damp as GainValue/DampValue.
  void Process(float rate_hz, float gain, float damp, float shape, bool symm);
  float x() const { return s_[0]; }
  float y() const { return s_[1]; }
  float z() const { return s_[2]; }
  float u() const { return s_[3]; }
  void Kick(float v) { s_[0] += v; }

 private:
  void Deriv(const float* q, float* dq) const;
  float s_[4];
  float g_ = 1, b_ = 1, shape_ = 0.5f;
  bool symm_ = true;
  uint32_t seed_ = 22222u;
};

class Rungler {
 public:
  void Reset() { reg_ = 0x5A; last_clk_ = false; }
  void Clock(bool clk, bool data);  // shift data in on a rising edge
  bool last_bit() const { return reg_ >> 7; }
  float Value() const;  // bits 5,6,7 → 0..1 in eighths (0, 1/7 .. 1)
  uint8_t reg() const { return reg_; }

 private:
  uint8_t reg_ = 0x5A;
  bool last_clk_ = false;
};

class Engine {
 public:
  void Init();
  // Process one block. ext_in: the filter's EXT input (J1), clk_in: the
  // rungler clock (J2) used when ext_clock is set. out: two audio outputs,
  // chosen by out_src. Sources are left in Source() after the block.
  void Process(const Params& p, const float* ext_in, const float* clk_in,
               float* out_l, float* out_r, uint8_t src_l, uint8_t src_r, int n);
  float Source(uint8_t s) const { return s < kNumSrc ? src_[s] : 0.f; }
  const Hypster& hyp() const { return hyp_; }
  const Rungler& rungler() const { return run_; }
  void KickHypster() { hyp_.Kick(0.5f); }
  // The frequencies the last sample used (for readouts and tests).
  float last_hz_a() const { return hz_a_; }
  float last_hz_b() const { return hz_b_; }
  float last_cutoff_hz() const { return hz_f_; }
  float last_rate_hz() const { return hz_r_; }

 private:
  float FilterIn(const Params& p, float ext) const;
  Hypster hyp_;
  Rungler run_;
  float ph_a_ = 0, ph_b_ = 0.25f;
  // Filter: a 2-pole state-variable with a saturating integrator.
  float ic1_ = 0, ic2_ = 0;
  bool last_ext_clk_ = false;
  float src_[kNumSrc] = {};
  float hz_a_ = 0, hz_b_ = 0, hz_f_ = 0, hz_r_ = 0;
};

}  // namespace hypjolin
