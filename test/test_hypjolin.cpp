// Native checks for the Hypjolin engine.
#include <cmath>
#include <cstdio>
#include <vector>
#include "hypjolin_engine.h"

using namespace hypjolin;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...)                                              \
  do {                                                                \
    if (cond) ++g_pass;                                               \
    else { ++g_fail; std::printf("FAIL %s:%d ", __FILE__, __LINE__);  \
           std::printf(__VA_ARGS__); std::printf("\n"); }             \
  } while (0)

static float Crossings(const std::vector<float>& s) {
  double m = 0; for (float v : s) m += v; m /= s.size();
  int c = 0; for (size_t i = 1; i < s.size(); ++i) if (s[i - 1] < m && s[i] >= m) ++c;
  return c * kSampleRate / s.size();
}

// Largest Lyapunov exponent of the Hypster ring, by two nearby copies.
static float Lyapunov(float hz, float g, float d, float shape, bool symm) {
  Hypster a, b; a.Reset(); b.Reset();
  for (int i = 0; i < 48000 * 4; ++i) { a.Process(hz, g, d, shape, symm); b.Process(hz, g, d, shape, symm); }
  b.Kick(1e-4f);
  double sum = 0; int n = 0;
  for (int blk = 0; blk < 400; ++blk) {
    for (int i = 0; i < 480; ++i) { a.Process(hz, g, d, shape, symm); b.Process(hz, g, d, shape, symm); }
    float dx[4] = {b.x() - a.x(), b.y() - a.y(), b.z() - a.z(), b.u() - a.u()};
    double dd = std::sqrt(double(dx[0]) * dx[0] + dx[1] * dx[1] + dx[2] * dx[2] + dx[3] * dx[3]);
    sum += std::log(std::max(dd, 1e-12) / 1e-4); ++n;
    // Re-seed b near a (copy a, then nudge).
    b = a; b.Kick(1e-4f);
  }
  return float(sum / n);  // nats per 10 ms block
}

static void TestMappings() {
  CHECK(std::fabs(OscHz(0) - 0.03f) < 1e-4f, "OscHz(0)");
  CHECK(std::fabs(OscHz(1) / OscHz(0) - 262144.f) < 1.f, "18 octaves");
  CHECK(std::fabs(CutoffHz(1) - 20480.f) < 1.f, "cutoff top");
  CHECK(std::fabs(RateHz(1) - 6553.6f) < 0.1f, "rate top");
  CHECK(GainValue(0) == 0.25f && GainValue(1) == 3.f, "gain range");
  CHECK(DampValue(0) == 0.f && DampValue(1) == 2.f, "damp range");
}

static void TestRungler() {
  Rungler r; r.Reset();
  uint8_t start = r.reg();
  r.Clock(false, true); CHECK(r.reg() == start, "no shift without an edge");
  r.Clock(true, true);  CHECK(r.reg() == uint8_t((start << 1) | 1), "shift on rising edge");
  r.Clock(true, false); CHECK(r.reg() == uint8_t((start << 1) | 1), "no shift while high");
  // DAC: all ones = 1, all zeros = 0.
  for (int i = 0; i < 8; ++i) { r.Clock(false, true); r.Clock(true, true); }
  CHECK(r.Value() == 1.f, "DAC full");
  for (int i = 0; i < 8; ++i) { r.Clock(false, false); r.Clock(true, false); }
  CHECK(r.Value() == 0.f, "DAC zero");
}

// LOOP on, offset at an end: the register only recirculates (period 8 or 16).
static void TestLoopLocks() {
  for (float off : {0.f, 1.f}) {
    Engine e; e.Init(); Params p;
    p.loop = true; p.loop_offset = off;
    p.run_a = p.run_b = 0;
    for (int d = 0; d < kNumDest; ++d) p.amt[d] = 0;
    p.osc_b = 0.6f;  // ~52 Hz clock
    std::vector<float> l(4800);
    e.Process(p, nullptr, nullptr, l.data(), nullptr, kSrcRungler, kSrcRungler, 4800);
    // Record the register at each clock for 64 clocks.
    std::vector<uint8_t> regs;
    uint8_t last = e.rungler().reg();
    for (int i = 0; i < 48000 * 3 && regs.size() < 64; ++i) {
      e.Process(p, nullptr, nullptr, l.data(), nullptr, kSrcRungler, kSrcRungler, 1);
      if (e.rungler().reg() != last) { last = e.rungler().reg(); regs.push_back(last); }
    }
    bool periodic16 = regs.size() >= 48;
    for (size_t i = 16; i < regs.size() && periodic16; ++i) periodic16 = regs[i] == regs[i - 16];
    // A register of all-same bits never changes value; that counts as locked too.
    CHECK(periodic16 || regs.size() < 4, "LOOP offset %.0f locks (period 16), %zu changes", off, regs.size());
  }
  // LOOP off: osc A free-running against osc B makes new patterns.
  Engine e; e.Init(); Params p;
  p.run_a = p.run_b = 0; p.osc_b = 0.6f; p.osc_a = 0.7f;
  for (int d = 0; d < kNumDest; ++d) p.amt[d] = 0;
  std::vector<uint8_t> regs; uint8_t last = 0; std::vector<float> l(1);
  for (int i = 0; i < 48000 * 3 && regs.size() < 64; ++i) {
    e.Process(p, nullptr, nullptr, l.data(), nullptr, kSrcRungler, kSrcRungler, 1);
    if (e.rungler().reg() != last) { last = e.rungler().reg(); regs.push_back(last); }
  }
  bool periodic16 = regs.size() >= 48;
  for (size_t i = 16; i < regs.size() && periodic16; ++i) periodic16 = regs[i] == regs[i - 16];
  CHECK(!periodic16, "LOOP off keeps changing");
}

static void TestHypster() {
  // Default panel: bounded, alive, not on the rails.
  {
    Hypster h; h.Reset();
    float mx = 0; int railed = 0;
    for (int i = 0; i < 48000 * 6; ++i) {
      h.Process(100, GainValue(0.45f), DampValue(0.35f), 0.5f, true);
      float m = std::max(std::max(std::fabs(h.x()), std::fabs(h.y())), std::max(std::fabs(h.z()), std::fabs(h.u())));
      if (i > 48000) { mx = std::max(mx, m); if (m > 10.9f) ++railed; }
    }
    CHECK(mx > 2.f && mx < 11.f, "default swings, |max| %.2f", mx);
    CHECK(railed == 0, "default never on the rails (%d)", railed);
  }
  // The limiter keeps DAMP at zero off the rails (Fritz's D2..D4).
  {
    Hypster h; h.Reset(); int railed = 0;
    for (int i = 0; i < 48000 * 4; ++i) {
      h.Process(100, GainValue(1.f), 0.f, 0.5f, true);
      if (i > 48000 && std::fabs(h.u()) > 10.9f) ++railed;
    }
    CHECK(railed < 48000 * 3 / 20, "DAMP 0 mostly off the rails (%d)", railed);
  }
  // RATE readout tracks the fundamental (within 25 %) across the range.
  for (float k : {0.3f, 0.5f, 0.7f, 0.9f}) {
    Hypster h; h.Reset();
    const float hz = RateHz(k);
    for (int i = 0; i < 48000; ++i) h.Process(hz, GainValue(0.45f), DampValue(0.35f), 0.5f, true);
    int n = int(std::max(96000.f, 48000.f * 40.f / hz));
    std::vector<float> xs(n);
    for (int i = 0; i < n; ++i) { h.Process(hz, GainValue(0.45f), DampValue(0.35f), 0.5f, true); xs[i] = h.x(); }
    float r = Crossings(xs) / hz;
    CHECK(r > 0.75f && r < 1.25f, "rate %.1f Hz: x at %.2f of it", hz, r);
  }
  // Chaos: the default (symmetric) is chaotic; low GAIN + high DAMP dies.
  {
    float l = Lyapunov(50, GainValue(0.45f), DampValue(0.35f), 0.5f, true);
    CHECK(l > 0.05f, "default chaotic, lyapunov %.3f / block", l);
    Hypster h; h.Reset(); float mx = 0;
    for (int i = 0; i < 48000 * 4; ++i) { h.Process(100, GainValue(0.f), DampValue(1.f), 0.5f, true); if (i > 48000 * 3) mx = std::max(mx, std::fabs(h.x())); }
    CHECK(mx < 0.05f, "low gain, high damp: stops (%.3f)", mx);
    // ...and a kick plus GAIN up brings it back.
    h.Kick(0.5f);
    for (int i = 0; i < 48000 * 4; ++i) { h.Process(100, GainValue(0.45f), DampValue(0.35f), 0.5f, true); if (i > 48000 * 3) mx = std::max(mx, std::fabs(h.x())); }
    CHECK(mx > 2.f, "restarts (%.2f)", mx);
  }
  // SYMM changes the orbit: symmetric spends time both sides of zero.
  {
    float lo[2], hi[2];
    for (int s = 0; s < 2; ++s) {
      Hypster h; h.Reset(); lo[s] = 1e9f; hi[s] = -1e9f;
      for (int i = 0; i < 48000 * 6; ++i) {
        h.Process(100, GainValue(0.45f), DampValue(0.35f), 0.5f, s == 1);
        if (i > 48000) { lo[s] = std::min(lo[s], h.x()); hi[s] = std::max(hi[s], h.x()); }
      }
    }
    CHECK(std::fabs((hi[0] + lo[0]) - (hi[1] + lo[1])) > 0.3f || std::fabs((hi[0] - lo[0]) - (hi[1] - lo[1])) > 0.3f,
          "SYMM changes the orbit (asym %.2f..%.2f, symm %.2f..%.2f)", lo[0], hi[0], lo[1], hi[1]);
  }
}

static void TestEngine() {
  Engine e; e.Init(); Params p;
  std::vector<float> l(48000), r(48000);
  bool finite = true; double rms = 0, rr = 0;
  for (int b = 0; b < 4; ++b) {
    e.Process(p, nullptr, nullptr, l.data(), r.data(), kSrcLopass, kSrcXor, 48000);
    for (int i = 0; i < 48000; ++i) { finite &= std::isfinite(l[i]) && std::isfinite(r[i]); if (b == 3) { rms += l[i] * l[i]; rr += r[i] * r[i]; } }
  }
  CHECK(finite, "outputs finite");
  rms = std::sqrt(rms / 48000); rr = std::sqrt(rr / 48000);
  CHECK(rms > 0.05 && rms < 1.2, "lopass level %.3f", rms);
  CHECK(rr > 0.5, "xor level %.3f", rr);
  for (int s = 1; s < kNumSrc; ++s) CHECK(std::isfinite(e.Source(s)) && std::fabs(e.Source(s)) <= 1.5f, "source %d in range", s);

  // Patch: X into osc A moves osc A's pitch over time; amount 0 holds it.
  {
    Engine a; a.Init(); Params q; q.run_a = 0;
    for (int d = 0; d < kNumDest; ++d) q.amt[d] = 0;
    float mn = 1e9f, mx = 0;
    for (int i = 0; i < 48000; ++i) { a.Process(q, nullptr, nullptr, l.data(), nullptr, kSrcLopass, kSrcLopass, 1); mn = std::min(mn, a.last_hz_a()); mx = std::max(mx, a.last_hz_a()); }
    CHECK(mx / mn < 1.001f, "no patch, osc A steady");
    q.src[kDestOscA] = kSrcX; q.amt[kDestOscA] = 0.5f; q.rate = 0.5f;
    mn = 1e9f; mx = 0;
    for (int i = 0; i < 48000 * 2; ++i) { a.Process(q, nullptr, nullptr, l.data(), nullptr, kSrcLopass, kSrcLopass, 1); mn = std::min(mn, a.last_hz_a()); mx = std::max(mx, a.last_hz_a()); }
    CHECK(mx / mn > 2.f, "X patched into osc A moves it (%.2fx)", mx / mn);
  }
  // External CV: +1 V on osc A = one octave.
  {
    Engine a; a.Init(); Params q; q.run_a = 0;
    for (int d = 0; d < kNumDest; ++d) q.amt[d] = 0;
    a.Process(q, nullptr, nullptr, l.data(), nullptr, kSrcLopass, kSrcLopass, 24);
    float f0 = a.last_hz_a();
    q.ext_cv[kDestOscA] = 1.f;
    a.Process(q, nullptr, nullptr, l.data(), nullptr, kSrcLopass, kSrcLopass, 24);
    CHECK(std::fabs(a.last_hz_a() / f0 - 2.f) < 0.01f, "1 V/oct on osc A (%.3f)", a.last_hz_a() / f0);
  }
  // External clock: J2 edges clock the rungler, osc B doesn't.
  {
    Engine a; a.Init(); Params q; q.ext_clock = true; q.osc_b = 0.9f;
    std::vector<float> clk(4800, 0.f);
    uint8_t r0 = a.rungler().reg();
    a.Process(q, nullptr, clk.data(), l.data(), nullptr, kSrcLopass, kSrcLopass, 4800);
    CHECK(a.rungler().reg() == r0, "no J2 edges, no shift");
    int edges = 0;
    for (int i = 0; i < 4800; ++i) { clk[i] = (i / 240) % 2 ? 1.f : -1.f; }
    for (int i = 1; i < 4800; ++i) if (clk[i] > 0 && clk[i - 1] < 0) ++edges;
    std::vector<uint8_t> seen;
    for (int i = 0; i < 4800; ++i) {
      a.Process(q, nullptr, &clk[i], l.data(), nullptr, kSrcLopass, kSrcLopass, 1);
      if (seen.empty() || seen.back() != a.rungler().reg()) seen.push_back(a.rungler().reg());
    }
    CHECK(int(seen.size()) - 1 <= edges && seen.size() > 1, "J2 clocks it (%zu changes, %d edges)", seen.size() - 1, edges);
  }
  // Filter EXT: a 100 Hz sine at J1 comes through the low-pass.
  {
    Engine a; a.Init(); Params q; q.filt_in = kFiltExt; q.cutoff = 0.8f; q.reso = 0.f; q.drive = 0.2f;
    std::vector<float> in(48000);
    for (int i = 0; i < 48000; ++i) in[i] = 0.5f * std::sin(2 * 3.14159265f * 100 * i / kSampleRate);
    a.Process(q, in.data(), nullptr, l.data(), nullptr, kSrcLopass, kSrcLopass, 48000);
    double s = 0; for (int i = 24000; i < 48000; ++i) s += l[i] * l[i];
    CHECK(std::sqrt(s / 24000) > 0.1, "ext in passes the filter (%.3f)", std::sqrt(s / 24000));
  }
  // Every rate and every corner stays finite (the audio thread must never NaN).
  {
    Engine a; a.Init(); Params q;
    for (float k : {0.f, 1.f}) {
      q.osc_a = q.osc_b = q.cutoff = q.reso = q.rate = q.gain = q.damp = q.drive = k;
      q.run_a = q.run_b = q.run_f = 1;
      for (int d = 0; d < kNumDest; ++d) { q.src[d] = kSrcX + d; q.amt[d] = k ? 1.f : -1.f; q.ext_cv[d] = k ? 5.f : -5.f; }
      a.Process(q, nullptr, nullptr, l.data(), r.data(), kSrcLopass, kSrcX, 48000);
      bool ok = true; for (int i = 0; i < 48000; ++i) ok &= std::isfinite(l[i]) && std::isfinite(r[i]);
      CHECK(ok, "corner %.0f finite", k);
    }
  }
}

int main() {
  TestMappings();
  TestRungler();
  TestLoopLocks();
  TestHypster();
  TestEngine();
  std::printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
