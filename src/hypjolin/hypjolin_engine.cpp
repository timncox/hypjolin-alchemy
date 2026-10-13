#include "hypjolin_engine.h"

#include <algorithm>
#include <cmath>

namespace hypjolin {

namespace {

inline float Clamp(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

// exp2 for the per-sample frequency maths: std::exp2 is fine on the H7's FPU
// at these call counts (four per sample).
inline float Exp2(float v) { return std::exp2(v); }

// Dead zone: zero inside ±t, slope 1 outside (a back-to-back zener pair).
inline float DeadZone(float v, float t) {
  float a = std::fabs(v) - t;
  return a > 0 ? std::copysign(a, v) : 0.f;
}
// One-sided breakpoint (a diode in series with one zener).
inline float Above(float v, float t) { return v > t ? v - t : 0.f; }

constexpr float kZener = 5.4f;  // 4.7 V zener + a diode drop

// The Hypster's four nonlinearities (Fritz 2018: two x - x^3-like from
// back-to-back zeners, two offset breakpoints). Slopes from the resistor
// ratios on his schematic (R7/R8 33k/100k, R9 220k; R51/R54 33k/100k,
// R52 330k; R67/R68 180k/100k; R82..R84). The loop gain kLoop was found
// by simulation: see tools/hyp_explore.cpp and DESIGN.md.
inline float Nl1(float v) { return 0.45f * v - 3.03f * DeadZone(v, kZener); }
inline float Nl2(float v) { return 0.90f * v - 3.03f * DeadZone(v, kZener); }
inline float Nl3(float v) { return 0.56f * v - 2.50f * Above(v, kZener); }
inline float Nl4(float v) { return 0.90f * v - 2.00f * Above(-v, kZener); }
constexpr float kLoop = 2.f;
// Fixed damping of the z, y and x stages (R59 82k, R74 220k, R89 330k
// against the 82k/47k/47k inputs).
constexpr float kDampZ = 1.f, kDampY = 47.f / 220.f, kDampX = 47.f / 330.f;
constexpr float kShapeDepth = 0.5f;
// The optional limiter (D2..D4): past ±9 V a stage is pulled back hard, so
// the ring stays off the rails rather than latching there. Stiff: it needs
// kMaxStep · kLimitPull under 2, which 0.03 · 60 is.
constexpr float kLimitAt = 9.f, kLimitPull = 60.f;
// Normalised time per second at rate 1 Hz: X's crossing rate at the
// default GAIN/DAMP lands close to the RATE readout (measured, test_hypjolin).
constexpr float kRateScale = 21.f;
// RK2 step bound, normalised time. Larger steps change the orbit itself
// (the attractor drifts with RATE), so the top of RATE is set by how many
// substeps fit: 1.3 kHz needs 20.
constexpr float kMaxStep = 0.03f;
constexpr int kMaxSub = 20;

}  // namespace

float OscHz(float k) { return 0.03f * Exp2(18.f * Clamp(k, 0.f, 1.f)); }
float CutoffHz(float k) { return 20.f * Exp2(10.f * Clamp(k, 0.f, 1.f)); }
float RateHz(float k) { return 0.02f * Exp2(16.f * Clamp(k, 0.f, 1.f)); }
float GainValue(float k) { return 0.25f + 2.75f * Clamp(k, 0.f, 1.f); }
float DampValue(float k) { return 2.f * Clamp(k, 0.f, 1.f); }

// ── Hypster ──────────────────────────────────────────────────────────────

void Hypster::Reset() {
  s_[0] = 0.3f; s_[1] = 0.1f; s_[2] = -0.2f; s_[3] = 0.05f;
}

void Hypster::Deriv(const float* q, float* dq) const {
  const float nx = -q[0];  // -X' closes the ring
  const float d = symm_ ? nx : std::max(nx, 0.f);  // SYMM: D9 in or out
  dq[3] = kLoop * g_ * Nl1(nx) - b_ * q[3];
  dq[2] = kLoop * Nl2(q[3]) - kDampZ * q[2];
  dq[1] = kLoop * Nl3(q[2] * 0.5f) - kDampY * q[1] + (1.f - shape_) * d * kShapeDepth;
  dq[0] = kLoop * Nl4(q[1] * 0.5f) - kDampX * q[0] + shape_ * d * kShapeDepth;
  for (int i = 0; i < 4; ++i) dq[i] -= kLimitPull * DeadZone(q[i], kLimitAt);
}

void Hypster::Process(float rate_hz, float gain, float damp, float shape, bool symm) {
  g_ = gain; b_ = damp; shape_ = shape; symm_ = symm;
  float h = rate_hz * kRateScale / kSampleRate;
  int n = 1;
  if (h > kMaxStep) {
    n = std::min(kMaxSub, int(h / kMaxStep) + 1);
    h /= float(n);
  }
  float k1[4], m[4], k2[4];
  for (int it = 0; it < n; ++it) {
    Deriv(s_, k1);
    for (int i = 0; i < 4; ++i) m[i] = s_[i] + 0.5f * h * k1[i];
    Deriv(m, k2);
    for (int i = 0; i < 4; ++i) s_[i] = Clamp(s_[i] + h * k2[i], -11.f, 11.f);
  }
  // The circuit never sits exactly at zero: a whisper of noise keeps the
  // ring able to start again after GAIN has been down at the bottom.
  seed_ = seed_ * 1664525u + 1013904223u;
  s_[0] += (float(int32_t(seed_)) * (1.f / 2147483648.f)) * 1e-4f;
}

// ── Rungler ──────────────────────────────────────────────────────────────

void Rungler::Clock(bool clk, bool data) {
  if (clk && !last_clk_) reg_ = uint8_t((reg_ << 1) | (data ? 1 : 0));
  last_clk_ = clk;
}

float Rungler::Value() const {
  // The Benjolin's 3-bit DAC on the last three stages.
  int v = ((reg_ >> 5) & 1) | (((reg_ >> 6) & 1) << 1) | (((reg_ >> 7) & 1) << 2);
  return float(v) * (1.f / 7.f);
}

// ── Engine ───────────────────────────────────────────────────────────────

void Engine::Init() {
  hyp_.Reset();
  run_.Reset();
  ph_a_ = 0; ph_b_ = 0.25f;
  ic1_ = ic2_ = 0;
  last_ext_clk_ = false;
  for (float& s : src_) s = 0;
}

float Engine::FilterIn(const Params& p, float ext) const {
  switch (p.filt_in) {
    case kFiltExt: return ext;
    case kFiltXor: return src_[kSrcXor];
    case kFiltTriA: return src_[kSrcTriA];
    case kFiltTriB: return src_[kSrcTriB];
    case kFiltX: return src_[kSrcX];
    case kFiltY: return src_[kSrcY];
    default: return src_[kSrcPwm];
  }
}

void Engine::Process(const Params& p, const float* ext_in, const float* clk_in,
                     float* out_l, float* out_r, uint8_t src_l, uint8_t src_r, int n) {
  const float base_a = 18.f * Clamp(p.osc_a, 0, 1);
  const float base_b = 18.f * Clamp(p.osc_b, 0, 1);
  const float base_f = 10.f * Clamp(p.cutoff, 0, 1);
  const float base_r = 16.f * Clamp(p.rate, 0, 1) + (Clamp(p.fine, 0, 1) - 0.5f);  // octaves above 0.02 Hz
  const float gain0 = GainValue(p.gain), damp0 = DampValue(p.damp);
  const float shape = Clamp(p.shape, 0, 1);
  // LOOP offset: ±1.5 at the ends, past the triangle's ±1, so the
  // comparator stops changing and the register only recirculates.
  const float offset = (Clamp(p.loop_offset, 0, 1) - 0.5f) * 3.f;
  const float reso = Clamp(p.reso, 0, 1);
  const float k_damp = 2.f - 2.f * reso;  // 0 at the top: self-oscillates
  const float drive = 0.5f + 4.f * Clamp(p.drive, 0, 1);

  for (int i = 0; i < n; ++i) {
    // Patch: volts at each input = source · amount · 5 V + external CV.
    float v[kNumDest];
    for (int d = 0; d < kNumDest; ++d)
      v[d] = Source(p.src[d]) * Clamp(p.amt[d], -1, 1) * kPatchVolts + p.ext_cv[d];

    const float rung = run_.Value();
    hz_a_ = Clamp(0.03f * Exp2(base_a + 5.f * p.run_a * rung + v[kDestOscA]), 0.01f, 16000.f);
    hz_b_ = Clamp(0.03f * Exp2(base_b + 5.f * p.run_b * rung + v[kDestOscB]), 0.01f, 16000.f);
    hz_f_ = Clamp(20.f * Exp2(base_f + 6.f * p.run_f * rung + v[kDestFilter]), 10.f, 20000.f);
    hz_r_ = Clamp(0.02f * Exp2(base_r + v[kDestRate]), 0.002f, 1400.f);
    const float gain = Clamp(gain0 + 0.3f * v[kDestGain], 0.f, 3.5f);
    const float damp = Clamp(damp0 + 0.2f * v[kDestDamp], 0.f, 2.5f);

    // Oscillators: triangle cores, the pulse is the core's comparator.
    ph_a_ += hz_a_ / kSampleRate; ph_a_ -= std::floor(ph_a_);
    ph_b_ += hz_b_ / kSampleRate; ph_b_ -= std::floor(ph_b_);
    const float tri_a = 1.f - 4.f * std::fabs(ph_a_ - 0.5f);
    const float tri_b = 1.f - 4.f * std::fabs(ph_b_ - 0.5f);
    const bool pa = tri_a > 0, pb = tri_b > 0;

    // Rungler: clocked by pulse B's rising edge (or J2's), fed by pulse A
    // XOR the last stage. LOOP on: osc A's comparator gets the offset,
    // and at either end it stops toggling: the register just loops.
    const bool data_a = p.loop ? (tri_a + offset > 0) : pa;
    bool clk = pb;
    if (p.ext_clock) {
      const float c = clk_in ? clk_in[i] : 0.f;
      // Schmitt: on above 0.3, off below 0.1 (the codec input is ±1).
      clk = last_ext_clk_ ? c > 0.1f : c > 0.3f;
      last_ext_clk_ = clk;
    }
    run_.Clock(clk, data_a != run_.last_bit());

    src_[kSrcTriA] = tri_a;
    src_[kSrcTriB] = tri_b;
    src_[kSrcPulseA] = pa ? 1.f : -1.f;
    src_[kSrcPulseB] = pb ? 1.f : -1.f;
    src_[kSrcXor] = (pa != pb) ? 1.f : -1.f;
    src_[kSrcPwm] = tri_a > tri_b ? 1.f : -1.f;  // the Benjolin's comparator
    src_[kSrcRungler] = run_.Value();

    // Filter: TPT state-variable, tanh on the band state (the "slightly
    // chaotic" Benjolin filter overdrives itself at high resonance).
    const float ext = ext_in ? ext_in[i] : 0.f;
    const float in = std::tanh(FilterIn(p, ext) * drive) * 0.8f;
    const float g = std::tan(3.14159265f * std::min(hz_f_, 0.45f * kSampleRate) / kSampleRate);
    const float a1 = 1.f / (1.f + g * (g + k_damp));
    const float v3 = in - ic2_;
    const float v1 = a1 * ic1_ + g * a1 * v3;
    const float v2 = ic2_ + g * v1;
    ic1_ = std::tanh(2.f * v1 - ic1_);
    ic2_ = 2.f * v2 - ic2_;
    src_[kSrcLopass] = Clamp(v2, -1.5f, 1.5f);

    hyp_.Process(hz_r_, gain, damp, shape, p.symm);
    src_[kSrcX] = hyp_.x() * 0.1f;
    src_[kSrcY] = hyp_.y() * 0.1f;
    src_[kSrcZ] = hyp_.z() * 0.1f;
    src_[kSrcU] = hyp_.u() * 0.1f;

    // 0.8 of full scale (about ±4 V at the jacks), as Tally.
    if (out_l) out_l[i] = Clamp(0.8f * Source(src_l), -1.f, 1.f);
    if (out_r) out_r[i] = Clamp(0.8f * Source(src_r), -1.f, 1.f);
  }
}

}  // namespace hypjolin
