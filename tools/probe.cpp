// Engine probe: Hypster rate calibration, rails, source stats.
//   c++ -O2 -std=c++17 -Isrc/hypjolin tools/probe.cpp src/hypjolin/hypjolin_engine.cpp -o /tmp/probe
#include <cmath>
#include <cstdio>
#include <vector>
#include "hypjolin_engine.h"
using namespace hypjolin;

// Dominant frequency of a signal by autocorrelation-free method: count
// upward mean crossings.
static float CrossHz(const std::vector<float>& s) {
  double m = 0; for (float v : s) m += v; m /= s.size();
  int c = 0; for (size_t i = 1; i < s.size(); ++i) if (s[i - 1] < m && s[i] >= m) ++c;
  return c * kSampleRate / s.size();
}

int main() {
  std::printf("rate calibration (default gain/damp/shape, symm on):\n");
  for (float k : {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f}) {
    Hypster h; h.Reset();
    float hz = RateHz(k);
    int secs_n = int(std::min(48000.f * 40.f, std::max(48000.f * 2.f, 48000.f * 60.f / hz)));
    for (int i = 0; i < 48000; ++i) h.Process(hz, GainValue(0.45f), DampValue(0.35f), 0.5f, true);
    std::vector<float> xs(secs_n);
    float mx = 0;
    for (int i = 0; i < secs_n; ++i) { h.Process(hz, GainValue(0.45f), DampValue(0.35f), 0.5f, true); xs[i] = h.x(); mx = std::max(mx, std::fabs(h.x())); }
    std::printf("  knob %.1f  RateHz %9.3f  x crossings %9.3f Hz  ratio %.2f  |x|max %.1f\n", k, hz, CrossHz(xs), CrossHz(xs) / hz, mx);
  }
  std::printf("gain/damp grid at 100 Hz: x max | crossings\n");
  for (float g : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
    std::printf(" g%.2f", g);
    for (float d : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
      Hypster h; h.Reset();
      std::vector<float> xs(48000);
      float mx = 0;
      for (int i = 0; i < 96000; ++i) { h.Process(100, GainValue(g), DampValue(d), 0.5f, true); if (i >= 48000) { xs[i - 48000] = h.x(); mx = std::max(mx, std::fabs(h.x())); } }
      std::printf("  %4.1f|%6.1f", mx, CrossHz(xs));
    }
    std::printf("\n");
  }
  Engine e; e.Init(); Params p;
  std::vector<float> l(48000), r(48000);
  for (int b = 0; b < 4; ++b) e.Process(p, nullptr, nullptr, l.data(), r.data(), kSrcLopass, kSrcXor, 48000);
  std::printf("source snapshot after 4 s: ");
  for (int s = 1; s < kNumSrc; ++s) std::printf("%d:%.2f ", s, e.Source(s));
  double rms = 0; for (float v : l) rms += v * v; std::printf("\nlopass rms %.3f  hzA %.1f hzB %.1f cut %.0f rate %.2f reg %02x\n",
      std::sqrt(rms / l.size()), e.last_hz_a(), e.last_hz_b(), e.last_cutoff_hz(), e.last_rate_hz(), e.rungler().reg());
}
