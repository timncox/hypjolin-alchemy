// Explore the Hypster ring model, scored by the largest Lyapunov exponent
// (two trajectories, renormalised) plus boundedness.
//   c++ -O2 -std=c++17 tools/hyp_explore.cpp -o /tmp/hx && /tmp/hx <mode>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static double dz(double v, double t) { double a = std::fabs(v) - t; return a > 0 ? std::copysign(a, v) : 0; }
static double pos(double v, double t) { return v > t ? v - t : 0; }

struct P {
  int mode = 0;          // 0 piecewise (zener) NLs, 1 smooth x - x^3
  double K = 1;          // overall loop gain
  double G = 1, B = 1;   // stage-u gain and damping
  double shape = 0.5; bool symm = true;
  double d[3] = {1.0, 47.0 / 220, 47.0 / 330};  // fixed damping z, y, x
  int s[4] = {1, 1, 1, 1};
  double sh = 0.5;       // shape feedback depth
};

static double f(const P& p, int k, double v) {
  if (p.mode == 1) {  // x - x^3, unit slope, turns over at |v| = 5.4 / sqrt3 * ..
    double w = v / 6.0;
    return 6.0 * (w - w * w * w) * (k == 0 ? 1.0 : 1.0);
  }
  if (p.mode == 2) switch (k) {  // all odd-symmetric breakpoints
    case 0: return 0.45 * v - 3.03 * dz(v, 5.4);
    case 1: return 0.90 * v - 3.03 * dz(v, 5.4);
    case 2: return 0.56 * v - 2.5 * dz(v, 5.4);
    default: return 0.90 * v - 2.0 * dz(v, 5.4);
  }
  switch (k) {
    case 0: return 0.45 * v - 3.03 * dz(v, 5.4);
    case 1: return 0.90 * v - 3.03 * dz(v, 5.4);
    case 2: return 0.56 * v - 2.5 * pos(v, 5.4);
    default: return 0.90 * v - 2.0 * pos(-v, 5.4);
  }
}

static void deriv(const P& p, const double* q, double* dq) {
  double x = q[0], y = q[1], z = q[2], u = q[3];
  double nx = -x;
  double dd = p.symm ? nx : std::max(nx, 0.0);
  dq[3] = p.K * p.s[0] * p.G * f(p, 0, nx) - p.B * u;
  dq[2] = p.K * p.s[1] * f(p, 1, u) - p.d[0] * z;
  dq[1] = p.K * p.s[2] * f(p, 2, z * (p.mode == 1 ? 1 : 0.5)) - p.d[1] * y + (1 - p.shape) * dd * p.sh;
  dq[0] = p.K * p.s[3] * f(p, 3, y * (p.mode == 1 ? 1 : 0.5)) - p.d[2] * x + p.shape * dd * p.sh;
}

static void step(const P& p, double* q, double dt) {  // RK2 midpoint + clamp
  double k1[4], m[4], k2[4];
  deriv(p, q, k1);
  for (int i = 0; i < 4; ++i) m[i] = q[i] + 0.5 * dt * k1[i];
  deriv(p, m, k2);
  for (int i = 0; i < 4; ++i) q[i] = std::clamp(q[i] + dt * k2[i], -11.0, 11.0);
}

struct Res { double lyap, rng, sat; };

static Res run(const P& p, int N = 300000, double dt = 5e-3) {
  double a[4] = {0.3, 0.1, -0.2, 0.05}, b[4];
  for (int i = 0; i < N / 5; ++i) step(p, a, dt);
  for (int i = 0; i < 4; ++i) b[i] = a[i];
  b[0] += 1e-6;
  double sum = 0, mn = 1e9, mx = -1e9; long sat = 0; int n = 0;
  for (int i = 0; i < N; ++i) {
    step(p, a, dt); step(p, b, dt);
    mn = std::min(mn, a[0]); mx = std::max(mx, a[0]);
    for (int k = 0; k < 4; ++k) if (std::fabs(a[k]) > 10.99) ++sat;
    if (i % 50 == 49) {
      double dd = 0; for (int k = 0; k < 4; ++k) dd += (b[k] - a[k]) * (b[k] - a[k]);
      dd = std::sqrt(dd);
      if (dd > 0) { sum += std::log(dd / 1e-6); ++n; for (int k = 0; k < 4; ++k) b[k] = a[k] + (b[k] - a[k]) * 1e-6 / dd; }
    }
  }
  return {sum / (n * 50 * dt), mx - mn, sat / (4.0 * N)};
}

static char cell(const Res& r) {
  if (r.rng < 0.05) return '.';
  if (r.sat > 0.2) return '#';
  if (r.lyap > 0.02) return 'C';
  if (r.lyap > 0.004) return 'c';
  return 'o';
}

int main(int argc, char** argv) {
  P p;
  p.mode = argc > 1 ? std::atoi(argv[1]) : 0;
  const char* sg = argc > 2 ? argv[2] : "++++";
  for (int j = 0; j < 4; ++j) p.s[j] = sg[j] == '-' ? -1 : 1;
  p.shape = argc > 3 ? std::atof(argv[3]) : 0.5;
  p.symm = argc > 4 ? std::atoi(argv[4]) : 1;
  p.sh = argc > 5 ? std::atof(argv[5]) : 0.5;
  for (double K : {0.5, 1.0, 2.0, 4.0}) {
    p.K = K;
    std::printf("K=%.1f  rows G 0.25..3, cols B 0..2\n", K);
    for (double G = 0.25; G <= 3.01; G += 0.25) {
      p.G = G;
      std::printf(" %4.2f ", G);
      for (double B = 0; B <= 2.01; B += 0.25) { p.B = B; Res r = run(p, 120000); std::printf("%c", cell(r)); }
      std::printf("\n");
    }
  }
}
