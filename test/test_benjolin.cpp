// Native checks for the Benjolin build: the engine with the Hypster off,
// and the three-page panel (src/benjolin/benjolin_map.h) the firmware runs.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
#include "benjolin_map.h"

using namespace hypjolin;
namespace bj = benjolin;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...)                                              \
  do {                                                                \
    if (cond) ++g_pass;                                               \
    else { ++g_fail; std::printf("FAIL %s:%d ", __FILE__, __LINE__);  \
           std::printf(__VA_ARGS__); std::printf("\n"); }             \
  } while (0)

// A panel from stored pot positions (0..1), the way the SDK reads them.
static bj::Panel FromStored(const float s[bj::kNumPages][bj::kNumPots]) {
  bj::Panel p;
  for (int pg = 0; pg < bj::kNumPages; ++pg)
    for (int k = 0; k < bj::kNumPots; ++k) p.v[pg][k] = bj::ValueOf(bj::kSpec[pg][k], s[pg][k]);
  return p;
}

static Params Benjolin(const bj::Panel& pn, uint8_t& l, uint8_t& r) {
  return bj::ToParams(pn, false, false, l, r);
}

// ── Hypster off ──────────────────────────────────────────────────────────

static void TestHypsterOff() {
  uint8_t l, r;
  Params p = Benjolin(bj::Defaults(), l, r);
  CHECK(!p.hypster, "the Benjolin panel turns the Hypster off");
  CHECK(p.src[kDestRate] == kSrcOff && p.src[kDestGain] == kSrcOff && p.src[kDestDamp] == kSrcOff,
        "the Hypster's inputs are unpatched");

  // Even with RATE at the top and the Hypster's own inputs patched, nothing
  // runs: its state stays where Reset() left it and X..U read 0.
  p.rate = 1.f; p.fine = 1.f; p.gain = 1.f;
  p.src[kDestRate] = kSrcTriA; p.amt[kDestRate] = 1.f;
  static Engine e;
  e.Init();
  Hypster fresh; fresh.Reset();
  std::vector<float> a(24), b(24);
  bool zero = true;
  for (int blk = 0; blk < 2000; ++blk) {
    e.Process(p, nullptr, nullptr, a.data(), b.data(), kSrcLopass, kSrcXor, 24);
    for (uint8_t s = kSrcX; s <= kSrcU; ++s) zero &= e.Source(s) == 0.f;
  }
  CHECK(zero, "X, Y, Z, U read 0");
  CHECK(e.hyp().x() == fresh.x() && e.hyp().y() == fresh.y() && e.hyp().z() == fresh.z() &&
            e.hyp().u() == fresh.u(),
        "Hypster state untouched: Process never ran, whatever RATE says");

  // The flag removes the Hypster and nothing else: with nothing routed from
  // it, the Benjolin's outputs match the Hypster-on engine sample for sample.
  Params on = Benjolin(bj::Defaults(), l, r);
  on.hypster = true;
  Params off = on;
  off.hypster = false;
  static Engine e1, e2;
  e1.Init(); e2.Init();
  std::vector<float> l1(24), r1(24), l2(24), r2(24);
  bool same = true;
  for (int blk = 0; blk < 4000; ++blk) {
    e1.Process(on, nullptr, nullptr, l1.data(), r1.data(), kSrcLopass, kSrcRungler, 24);
    e2.Process(off, nullptr, nullptr, l2.data(), r2.data(), kSrcLopass, kSrcRungler, 24);
    for (int i = 0; i < 24; ++i) same &= l1[i] == l2[i] && r1[i] == r2[i];
  }
  CHECK(same, "Benjolin outputs identical with the Hypster on or off when unrouted");
}

// Per-block cost. Only asserted in the unsanitised build (make fast):
// sanitiser overhead swamps the ratios.
static double NsPerBlock(const Params& p) {
  static Engine e;
  e.Init();
  float a[24], b[24];
  for (int i = 0; i < 2000; ++i) e.Process(p, nullptr, nullptr, a, b, kSrcLopass, kSrcXor, 24);
  double best = 1e30;
  for (int rep = 0; rep < 5; ++rep) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20000; ++i) e.Process(p, nullptr, nullptr, a, b, kSrcLopass, kSrcXor, 24);
    const auto t1 = std::chrono::steady_clock::now();
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / 20000.0;
    if (ns < best) best = ns;
  }
  return best;
}

static void TestCost() {
  uint8_t l, r;
  Params ben = Benjolin(bj::Defaults(), l, r);
  Params ben_top = ben; ben_top.rate = 1.f; ben_top.fine = 1.f;
  Params hyp;                    // Hypjolin's defaults
  Params hyp_top = hyp; hyp_top.rate = 1.f; hyp_top.fine = 1.f;
  const double nb = NsPerBlock(ben), nbt = NsPerBlock(ben_top);
  const double nh = NsPerBlock(hyp), nht = NsPerBlock(hyp_top);
  std::printf("  ns per 24-sample block (host, best of 5): Benjolin %.0f (RATE top %.0f), "
              "Hypjolin %.0f (RATE top %.0f)\n", nb, nbt, nh, nht);
#ifdef TIMING_CHECKS
  CHECK(nbt < 1.5 * nb && nb < 1.5 * nbt, "Benjolin cost does not follow RATE (%.0f vs %.0f)", nb, nbt);
  CHECK(nht > 3.0 * nbt, "the check can see RATE: Hypjolin at the top costs more (%.0f vs %.0f)", nht, nbt);
  CHECK(nb < nh, "Benjolin cheaper than Hypjolin at defaults (%.0f vs %.0f)", nb, nh);
#endif
}

// ── The three pages ──────────────────────────────────────────────────────

static void TestPages() {
  CHECK(bj::kNumPages == 3, "three pages");
  // JOLIN and FILTER's continuous knobs land on their Params, one at a time.
  float s[3][6];
  for (auto& pg : s) for (float& v : pg) v = 0.f;
  struct { int page, pot; float Params::*field; } lin[] = {
      {bj::kPageJolin, 0, &Params::osc_a}, {bj::kPageJolin, 1, &Params::osc_b},
      {bj::kPageJolin, 2, &Params::run_a}, {bj::kPageJolin, 3, &Params::run_b},
      {bj::kPageJolin, 4, &Params::run_f}, {bj::kPageJolin, 5, &Params::loop_offset},
      {bj::kPageFilter, 0, &Params::cutoff}, {bj::kPageFilter, 1, &Params::reso},
      {bj::kPageFilter, 3, &Params::drive}};
  for (auto& m : lin) {
    float t[3][6];
    for (int a = 0; a < 3; ++a) for (int b = 0; b < 6; ++b) t[a][b] = s[a][b];
    t[m.page][m.pot] = 0.73f;
    uint8_t l, r;
    const Params p = Benjolin(FromStored(t), l, r);
    CHECK(std::fabs(p.*m.field - 0.73f) < 1e-6f, "page %d pot %d -> its param", m.page, m.pot + 1);
    int moved = 0;
    for (auto& o : lin) if (std::fabs(p.*o.field - (o.field == m.field ? 0.73f : 0.f)) > 1e-6f) ++moved;
    CHECK(moved == 0, "page %d pot %d moves nothing else", m.page, m.pot + 1);
  }

  // FILTER selectors: INPUT has the five Benjolin inputs, OUT L/R the eight
  // outputs, never a Hypster one.
  const uint8_t want_in[5] = {kFiltPwm, kFiltExt, kFiltXor, kFiltTriA, kFiltTriB};
  for (int z = 0; z < 5; ++z) {
    float t[3][6] = {};
    t[bj::kPageFilter][2] = (z + 0.5f) / 5.f;
    uint8_t l, r;
    CHECK(Benjolin(FromStored(t), l, r).filt_in == want_in[z], "INPUT zone %d", z);
  }
  const uint8_t want_out[8] = {kSrcTriA, kSrcTriB, kSrcPulseA, kSrcPulseB,
                               kSrcXor, kSrcPwm, kSrcRungler, kSrcLopass};
  for (int z = 0; z < 8; ++z) {
    float t[3][6] = {};
    t[bj::kPageFilter][4] = (z + 0.5f) / 8.f;
    t[bj::kPageFilter][5] = (7 - z + 0.5f) / 8.f;
    uint8_t l, r;
    Benjolin(FromStored(t), l, r);
    CHECK(l == want_out[z] && r == want_out[7 - z], "OUT L/R zone %d", z);
  }
  {
    float t[3][6] = {};
    t[bj::kPageFilter][4] = t[bj::kPageFilter][5] = 1.f;  // the very end
    uint8_t l, r;
    Benjolin(FromStored(t), l, r);
    CHECK(l == kSrcLopass && r == kSrcLopass, "OUT at full = Lopass, the last zone");
  }

  // PATCH: rows are Osc A, Osc B, Filter; source left, amount right
  // (noon off, -1..1).
  const uint8_t dest[3] = {kDestOscA, kDestOscB, kDestFilter};
  const uint8_t want_src[9] = {kSrcOff, kSrcTriA, kSrcTriB, kSrcPulseA, kSrcPulseB,
                               kSrcXor, kSrcPwm, kSrcRungler, kSrcLopass};
  for (int row = 0; row < 3; ++row) {
    for (int z = 0; z < 9; ++z) {
      float t[3][6];
      for (int a = 0; a < 3; ++a) for (int b = 0; b < 6; ++b) t[a][b] = 0.5f;
      t[bj::kPagePatch][2 * row] = (z + 0.5f) / 9.f;
      t[bj::kPagePatch][2 * row + 1] = 0.9f;
      uint8_t l, r;
      const Params p = Benjolin(FromStored(t), l, r);
      bool others = true;
      for (int d = 0; d < kNumDest; ++d)
        if (d != dest[row]) others &= p.amt[d] == 0.f;
      CHECK(p.src[dest[row]] == want_src[z] && std::fabs(p.amt[dest[row]] - 0.8f) < 1e-5f && others,
            "PATCH row %d zone %d -> dest %d", row, z, dest[row]);
    }
  }
  {
    float t[3][6] = {};
    for (int row = 0; row < 3; ++row) t[bj::kPagePatch][2 * row + 1] = 0.5f;
    uint8_t l, r;
    const Params p = Benjolin(FromStored(t), l, r);
    CHECK(p.amt[kDestOscA] == 0.f && p.amt[kDestOscB] == 0.f && p.amt[kDestFilter] == 0.f,
          "PATCH amounts at noon are off");
  }

  // First boot: Defaults() survive the trip through stored positions, and
  // say what DESIGN.md says.
  const bj::Panel d = bj::Defaults();
  float st[3][6];
  for (int pg = 0; pg < 3; ++pg)
    for (int k = 0; k < 6; ++k) st[pg][k] = bj::StoredOf(bj::kSpec[pg][k], d.v[pg][k]);
  const bj::Panel back = FromStored(st);
  bool round = true;
  for (int pg = 0; pg < 3; ++pg)
    for (int k = 0; k < 6; ++k) round &= std::fabs(back.v[pg][k] - d.v[pg][k]) < 1e-5f;
  CHECK(round, "defaults round-trip through stored pot positions");
  uint8_t l, r;
  const Params p = Benjolin(back, l, r);
  const Params e;
  CHECK(p.osc_a == e.osc_a && p.osc_b == e.osc_b && p.run_a == e.run_a && p.run_b == e.run_b &&
            p.run_f == e.run_f && p.cutoff == e.cutoff && p.reso == e.reso && p.drive == e.drive,
        "JOLIN / FILTER defaults are Hypjolin's");
  CHECK(p.filt_in == kFiltPwm && l == kSrcLopass && r == kSrcXor, "INPUT PWM, OUT Lopass / XOR");
  CHECK(p.src[kDestOscA] == kSrcTriB && std::fabs(p.amt[kDestOscA] - 0.15f) < 1e-5f &&
            p.src[kDestOscB] == kSrcPulseA && p.amt[kDestOscB] == 0.f &&
            p.src[kDestFilter] == kSrcTriB && std::fabs(p.amt[kDestFilter] - 0.10f) < 1e-5f,
        "PATCH defaults: tri B -> osc A 0.15, osc B off, tri B -> filter 0.10");
}

// ── Jacks ────────────────────────────────────────────────────────────────

static void TestJacks() {
  uint8_t l, r;
  Params base = Benjolin(bj::Defaults(), l, r);
  bool gate[4] = {};
  float volts[4] = {1.f, -2.f, 0.5f, 0.f};
  Params p = base;
  bj::ApplyJacks(p, bj::kJackDefault, volts, gate);
  CHECK(p.ext_cv[kDestOscA] == 1.f && p.ext_cv[kDestOscB] == -2.f && p.ext_cv[kDestFilter] == 0.5f,
        "J3-J5 add volts to Osc A / Osc B / Filter");
  CHECK(!p.loop, "J6 low: LOOP as switched");
  const float j6[] = {5.f, 1.0f, 0.3f, 1.0f, 2.f};
  const bool want[] = {true, true, false, false, true};  // Schmitt 1.5 / 0.5
  for (int i = 0; i < 5; ++i) {
    volts[3] = j6[i];
    p = base;
    bj::ApplyJacks(p, bj::kJackDefault, volts, gate);
    CHECK(p.loop == want[i], "J6 gate at %.1f V -> LOOP %d", j6[i], want[i]);
  }
  // With the switch on, the gate turns LOOP off while held.
  volts[3] = 5.f;
  p = base; p.loop = true;
  bj::ApplyJacks(p, bj::kJackDefault, volts, gate);
  CHECK(!p.loop, "J6 high flips a LOOP that is on");
  // Re-pointed: J6 to Osc A adds to it and stops being a gate.
  const uint8_t all_a[4] = {bj::kJackOscA, bj::kJackOscA, bj::kJackOscA, bj::kJackOscA};
  float v1[4] = {1.f, 1.f, 1.f, 1.f};
  p = base;
  bj::ApplyJacks(p, all_a, v1, gate);
  CHECK(p.ext_cv[kDestOscA] == 4.f && !p.loop, "all four jacks summed into Osc A");
}

// ── B3: a new rungler pattern, which LOOP holds ──────────────────────────

static void TestSeed() {
  uint8_t l, r;
  Params p = Benjolin(bj::Defaults(), l, r);
  p.loop = true;
  p.loop_offset = 0.f;  // fully locked
  p.run_a = p.run_b = 0.f;
  static Engine e;
  e.Init();
  e.SeedRungler(0xC3);
  CHECK(e.rungler().reg() == 0xC3, "SeedRungler loads the register");
  std::vector<uint8_t> regs;
  float a[24], b[24];
  for (int blk = 0; blk < 4000; ++blk) {
    e.Process(p, nullptr, nullptr, a, b, kSrcLopass, kSrcXor, 24);
    regs.push_back(e.rungler().reg());
  }
  // Locked: the register only rotates (inverted or not), so its 8-step
  // cycle keeps reappearing; the seed's bits are still in it.
  bool seen = false;
  for (uint8_t v : regs) {
    for (int k = 0; k < 8; ++k) {
      const uint8_t rot = uint8_t((0xC3 << k) | (0xC3 >> (8 - k)));
      seen |= v == rot || v == uint8_t(~rot);
    }
  }
  CHECK(seen, "with LOOP locked the seeded pattern keeps circulating");
}

// ── Finite over a sweep of every knob ────────────────────────────────────

static void TestSweep() {
  const float corners[] = {0.f, 0.02f, 0.5f, 0.98f, 1.f};
  bool ok = true, bounded = true;
  int runs = 0;
  uint32_t lcg = 12345u;
  auto rnd = [&] { lcg = lcg * 1664525u + 1013904223u; return float(lcg >> 8) / 16777216.f; };
  for (int run = 0; run < 400; ++run) {
    float s[3][6];
    for (auto& pg : s)
      for (float& v : pg) v = run < 200 ? corners[(run + int(&v - &s[0][0]) * 7) % 5] : rnd();
    uint8_t l, r;
    Params p = bj::ToParams(FromStored(s), run & 1, run & 2, l, r);
    for (int d = 0; d < 3; ++d) p.ext_cv[d] = (run % 3 - 1) * 5.f;  // ±5 V or 0 at J3-J5
    static Engine e;
    e.Init();
    float ext[24], clk[24], a[24], b[24];
    for (int blk = 0; blk < 400; ++blk) {  // 0.2 s each
      for (int i = 0; i < 24; ++i) { ext[i] = std::sin(0.1f * (blk * 24 + i)); clk[i] = (blk / 3) & 1; }
      e.Process(p, ext, clk, a, b, l, r, 24);
      for (int i = 0; i < 24; ++i) {
        ok &= std::isfinite(a[i]) && std::isfinite(b[i]);
        bounded &= std::fabs(a[i]) <= 1.f && std::fabs(b[i]) <= 1.f;
      }
      for (uint8_t src = 0; src < kNumSrc; ++src) ok &= std::isfinite(e.Source(src));
    }
    ++runs;
  }
  CHECK(ok, "finite outputs and sources over %d panels (corners + random)", runs);
  CHECK(bounded, "outputs within +-1");
}

int main() {
  TestHypsterOff();
  TestCost();
  TestPages();
  TestJacks();
  TestSeed();
  TestSweep();
  std::printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
