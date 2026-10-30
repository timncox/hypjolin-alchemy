// Output fingerprint of the engine: FNV-1a over the raw bytes of both audio
// outputs and every source, block by block, across a handful of settings.
// Build it against two versions of hypjolin_engine.cpp and compare the
// hashes to show a change is bit-identical:
//   c++ -O2 -std=c++17 -Isrc/hypjolin tools/golden.cpp src/hypjolin/hypjolin_engine.cpp -o /tmp/golden
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "hypjolin_engine.h"
using namespace hypjolin;

static uint64_t Fnv(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = static_cast<const uint8_t*>(p);
  for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}

static uint64_t Run(const Params& p, uint8_t sl, uint8_t sr, int blocks) {
  static Engine e;
  e.Init();
  float ext[24], clk[24], l[24], r[24];
  uint64_t h = 1469598103934665603ull;
  for (int b = 0; b < blocks; ++b) {
    for (int i = 0; i < 24; ++i) {
      ext[i] = 0.5f * float((b * 24 + i) % 218) / 218.f - 0.25f;  // a 220 Hz saw
      clk[i] = ((b * 24 + i) / 100) % 2 ? 1.f : 0.f;               // 240 Hz square
    }
    e.Process(p, ext, clk, l, r, sl, sr, 24);
    h = Fnv(h, l, sizeof l);
    h = Fnv(h, r, sizeof r);
    for (uint8_t s = 0; s < kNumSrc; ++s) { float v = e.Source(s); h = Fnv(h, &v, 4); }
  }
  return h;
}

int main() {
  const int kBlocks = 4000;  // 2 s
  Params d;
  std::printf("default        %016llx\n", (unsigned long long)Run(d, kSrcLopass, kSrcXor, kBlocks));
  Params hi = d; hi.rate = 1.f; hi.fine = 1.f;
  std::printf("rate top       %016llx\n", (unsigned long long)Run(hi, kSrcX, kSrcY, kBlocks));
  Params lp = d; lp.loop = true; lp.loop_offset = 0.95f; lp.reso = 1.f; lp.filt_in = kFiltX;
  std::printf("loop+filt X    %016llx\n", (unsigned long long)Run(lp, kSrcLopass, kSrcU, kBlocks));
  Params ex = d; ex.ext_clock = true; ex.filt_in = kFiltExt; ex.src[kDestGain] = kSrcTriA; ex.amt[kDestGain] = 0.7f;
  ex.src[kDestDamp] = kSrcZ; ex.amt[kDestDamp] = -0.5f; ex.symm = false; ex.shape = 0.1f;
  std::printf("ext+patch      %016llx\n", (unsigned long long)Run(ex, kSrcRungler, kSrcZ, kBlocks));
  return 0;
}
