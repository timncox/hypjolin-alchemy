/*
 * Hypjolin on the Hermetic Modular Alchemy Lab V2 -- Rob Hordijk's Benjolin
 * and Ian Fritz's Hypster hyperchaos generator, cross-patched, after
 * Triglav Modular's Hypjolin (anti-copyright 2023, github.com/triglav-
 * modular/Hypjolin). See src/hypjolin/DESIGN.md.
 *
 * The DSP is hypjolin::Engine (hypjolin_engine.cpp). This file folds the
 * Hypjolin's 19 knobs, two switches and 24-jack banana patchbay onto six
 * pots, three buttons and ten jacks:
 *
 *   JOLIN   OSC A . OSC B . RUN>A . RUN>B . RUN>F . LOOP offset
 *   FILTER  CUTOFF . RESO . INPUT . DRIVE . OUT L . OUT R
 *   HYP     RATE . FINE . GAIN . DAMP . SHAPE . SYMM
 *   PATCH   the source into OSC A . OSC B . FILTER . RATE . GAIN . DAMP
 *   DEPTH   (hold B3) the six "cv" attenuverters for those inputs
 *   B1      the LOOP switch; with B3 held: kick the Hypster
 *   B2      next page (JOLIN -> FILTER -> HYP -> PATCH)
 *   J1 filter EXT in . J2 rungler clock in (Settings Clock = J2)
 *   J3-J6 CV into OSC A / OSC B / FILTER / RATE by default (Settings page 2
 *   re-points each at any of the six inputs), 1 V/oct where it is a pitch
 *   J7 / J8 any source as 0-5 V (Settings; Rungler and X by default)
 *   J9 / J10 the two audio outs (OUT L / OUT R)
 */
#include <math.h>
#include <string.h>

#include "daisy_seed.h"
#include "util/CpuLoadMeter.h"

#include "alchemy/hw/alchemy_lab.h"
#include "alchemy/host_link/cdc_transport.h"
#include "alchemy/host_link/host.h"
#include "alchemy/storage/fs_extension.h"
#include "alchemy/storage/sd_card.h"
#include "alchemy/surface/control_loop.h"
#include "alchemy/surface/cv_matrix.h"
#include "alchemy/surface/jack.h"
#include "alchemy/surface/manual.h"
#include "alchemy/surface/page.h"
#include "alchemy/surface/pager.h"
#include "alchemy/surface/presets.h"
#include "alchemy/surface/settings.h"
#include "alchemy/surface/virtual_button.h"
#include "alchemy/surface/virtual_knob.h"

#include "mi_family.h"
#include "picker.h"
#include "hypjolin_engine.h"

#ifndef HYPJOLIN_VERSION
#define HYPJOLIN_VERSION "0.0.0"
#endif
#ifndef HYPJOLIN_GIT_HASH
#define HYPJOLIN_GIT_HASH "dev"
#endif

using namespace alchemy;
using daisy::System;
namespace hj = hypjolin;

/* ---- geometry ----------------------------------------------------------- */

static constexpr uint32_t kBlockSize = 24u;

enum : uint8_t {
    kPageJolin = 0, kPageFilter = 1, kPageHyp = 2, kPagePatch = 3, kPageDepth = 4,
    kNumAppPages = 5
};
enum : uint8_t { kSettingsMain = 0, kSettingsFirmware = 1, kSettingsJacks = 2 };

/* CV indices: 0 = J3 ... 5 = J8. */
static constexpr uint8_t kNumCvIn = 4u;
static constexpr uint8_t kCvOut[2] = {4u, 5u};

/* ---- hardware + engine -------------------------------------------------- */

static AlchemyLab          hw;
static daisy::CpuLoadMeter cpu;
static hj::Engine          engine;

/* ---- colours: Benjolin blue, Hypster purple, as the banana jacks ---------- */

static constexpr LedPanel::Rgb kBlue   = {0x20, 0x60, 0xFF};
static constexpr LedPanel::Rgb kCyan   = {0x20, 0xC0, 0xFF};
static constexpr LedPanel::Rgb kPurple = {0xB0, 0x30, 0xFF};
static constexpr LedPanel::Rgb kPink   = {0xFF, 0x40, 0xC0};
static constexpr LedPanel::Rgb kAmber  = {0xFF, 0xA0, 0x20};
static constexpr LedPanel::Rgb kGreen  = {0x30, 0xFF, 0x60};
static constexpr LedPanel::Rgb kNotch  = {0xFF, 0xFF, 0xFF};
static constexpr LedPanel::Rgb kDim    = {0x18, 0x18, 0x18};
static constexpr LedPanel::Rgb kIdle   = {0x00, 0x00, 0x30};
static constexpr LedPanel::Rgb kRed    = {0xFF, 0x00, 0x00};

static constexpr LedPanel::Rgb kPageCol[kNumAppPages] = {
    kBlue, kCyan, kPurple, kAmber, kGreen};

/* Labels index hj::Src (Off first). */
static const char* const kSrcLabels[hj::kNumSrc] = {
    "Off", "X", "Y", "Z", "U", "Triangle A", "Triangle B", "Pulse A",
    "Pulse B", "XOR", "PWM", "Rungler", "Lopass"};
/* The outputs have no Off: index i = source i + 1. */
static const char* const kOutLabels[hj::kNumSrc - 1] = {
    "X", "Y", "Z", "U", "Triangle A", "Triangle B", "Pulse A",
    "Pulse B", "XOR", "PWM", "Rungler", "Lopass"};
static const char* const kFiltInLabels[hj::kNumFiltIn] = {
    "PWM", "Ext (J1)", "XOR", "Triangle A", "Triangle B", "X", "Y"};
static const char* const kSymmLabels[2]  = {"Asymmetric", "Symmetric"};
static const char* const kClockLabels[2] = {"Osc B", "J2"};
static const char* const kDestLabels[hj::kNumDest] = {
    "Osc A", "Osc B", "Filter", "Rate", "Gain", "Damp"};

/* ---- pages --------------------------------------------------------------- */

/* Panel positions, front view:
 *      [P1] B1 [P2]
 *      [P3] B2 [P4]
 *      [P5] B3 [P6]                                                        */

/* JOLIN */
static VirtualKnob osc_a = VirtualKnob(kPotTopLeft, "Osc A")
    .Linear(0.f, 1.f).Ident("osc_a")
    .Help("Oscillator A, 18 octaves: 0.03 Hz to 7.9 kHz. Its pulse feeds "
          "the rungler's data input.")
    .Ring(Level(kBlue));
static VirtualKnob osc_b = VirtualKnob(kPotTopRight, "Osc B")
    .Linear(0.f, 1.f).Ident("osc_b")
    .Help("Oscillator B, 18 octaves. Its pulse clocks the rungler (unless "
          "Settings Clock is J2).")
    .Ring(Level(kBlue));
static VirtualKnob run_a = VirtualKnob(kPotMiddleLeft, "Run>A")
    .Linear(0.f, 1.f).Ident("run_a")
    .Help("Rungler to oscillator A, up to 5 octaves.")
    .Ring(Level(kCyan));
static VirtualKnob run_b = VirtualKnob(kPotMiddleRight, "Run>B")
    .Linear(0.f, 1.f).Ident("run_b")
    .Help("Rungler to oscillator B, up to 5 octaves. Feeding the clock "
          "back from its own register is where the Benjolin gets wild.")
    .Ring(Level(kCyan));
static VirtualKnob run_f = VirtualKnob(kPotBottomLeft, "Run>F")
    .Linear(0.f, 1.f).Ident("run_f")
    .Help("Rungler to the filter cutoff, up to 6 octaves.")
    .Ring(Level(kCyan));
static VirtualKnob loop_off = VirtualKnob(kPotBottomRight, "Loop")
    .Linear(0.f, 1.f).Ident("loop_offset")
    .Help("With the LOOP switch on (B1): offsets osc A's comparator. Near "
          "noon the register keeps taking new bits; towards either end it "
          "stops, and the rungler locks into a loop (inverted one way, so "
          "16 steps instead of 8). Does nothing with LOOP off.")
    .Ring(Bipolar(kCyan, kBlue, kNotch));

/* FILTER */
static VirtualKnob cutoff = VirtualKnob(kPotTopLeft, "Cutoff")
    .Linear(0.f, 1.f).Ident("cutoff")
    .Help("Filter frequency, 20 Hz to 20 kHz.")
    .Ring(Level(kBlue));
static VirtualKnob reso = VirtualKnob(kPotTopRight, "Reso")
    .Linear(0.f, 1.f).Ident("reso")
    .Help("Resonance; self-oscillates at the top.")
    .Ring(Level(kBlue));
static VirtualKnob filt_in = VirtualKnob(kPotMiddleLeft, "Input")
    .Selector(hj::kNumFiltIn).Labels(kFiltInLabels).Ident("filt_in")
    .Help("What the filter hears. PWM (osc A against osc B) is the "
          "Benjolin's own normal; Ext is J1.")
    .Ring(SelectorRing(kBlue, kDim, hj::kNumFiltIn));
static VirtualKnob drive = VirtualKnob(kPotMiddleRight, "Drive")
    .Linear(0.f, 1.f).Ident("drive")
    .Help("Level into the filter, into saturation at the top.")
    .Ring(Level(kAmber));
static VirtualKnob out_l = VirtualKnob(kPotBottomLeft, "Out L")
    .Selector(hj::kNumSrc - 1).Labels(kOutLabels).Ident("out_l")
    .Help("J9: any of the twelve outputs (Lopass is the Benjolin's main out).")
    .Ring(SelectorRing(kAmber, kDim, hj::kNumSrc - 1));
static VirtualKnob out_r = VirtualKnob(kPotBottomRight, "Out R")
    .Selector(hj::kNumSrc - 1).Labels(kOutLabels).Ident("out_r")
    .Help("J10: any of the twelve outputs. The Hypster's X..U are audio "
          "once RATE is up past about two o'clock.")
    .Ring(SelectorRing(kAmber, kDim, hj::kNumSrc - 1));

/* HYP */
static VirtualKnob rate = VirtualKnob(kPotTopLeft, "Rate")
    .Linear(0.f, 1.f).Ident("rate")
    .Help("The Hypster's overall rate, exponential: 0.02 Hz to 1.3 kHz.")
    .Ring(Level(kPurple));
static VirtualKnob fine = VirtualKnob(kPotTopRight, "Fine")
    .Linear(0.f, 1.f).Ident("fine")
    .Help("Fine rate, half an octave either way.")
    .Ring(Bipolar(kPurple, kPink, kNotch));
static VirtualKnob gain = VirtualKnob(kPotMiddleLeft, "Gain")
    .Linear(0.f, 1.f).Ident("gain")
    .Help("Gain of the variable stage. With DAMP, the main chaos control: "
          "low is quiet or periodic, up and the orbit thickens.")
    .Ring(Level(kPurple));
static VirtualKnob damp = VirtualKnob(kPotMiddleRight, "Damp")
    .Linear(0.f, 1.f).Ident("damp")
    .Help("Damping (resonance) of the variable stage. Too much with too "
          "little GAIN and the Hypster stops.")
    .Ring(Level(kPurple));
static VirtualKnob shape = VirtualKnob(kPotBottomLeft, "Shape")
    .Linear(0.f, 1.f).Ident("shape")
    .Help("The cross-coupling: slim to the left, fat to the right.")
    .Ring(Bipolar(kPink, kPurple, kNotch));
static VirtualKnob symm = VirtualKnob(kPotBottomRight, "Symm")
    .Selector(2).Labels(kSymmLabels).Ident("symm")
    .Help("The SYMMETRY switch: half- or full-wave cross-coupling. "
          "Symmetric orbits are the denser, more chaotic ones.")
    .Ring(SelectorRing(kPink, kDim, 2));

/* PATCH + DEPTH: one source and one attenuverter per black jack */
#define PATCH_KNOB(name, pot, label, id, what)                                   \
    static VirtualKnob name = VirtualKnob(pot, label)                            \
        .Selector(hj::kNumSrc).Labels(kSrcLabels).Ident(id)                       \
        .Help("What is patched into " what ": any Benjolin or Hypster output.") \
        .Ring(SelectorRing(kAmber, kDim, hj::kNumSrc));
PATCH_KNOB(p_osc_a, kPotTopLeft,     ">Osc A",  "patch_osc_a",  "osc A's CV")
PATCH_KNOB(p_osc_b, kPotTopRight,    ">Osc B",  "patch_osc_b",  "osc B's CV")
PATCH_KNOB(p_filt,  kPotMiddleLeft,  ">Filter", "patch_filter", "the filter's CV")
PATCH_KNOB(p_rate,  kPotMiddleRight, ">Rate",   "patch_rate",   "the Hypster's RATE")
PATCH_KNOB(p_gain,  kPotBottomLeft,  ">Gain",   "patch_gain",   "the Hypster's GAIN")
PATCH_KNOB(p_damp,  kPotBottomRight, ">Damp",   "patch_damp",   "the Hypster's DAMP")
#undef PATCH_KNOB

#define DEPTH_KNOB(name, pot, label, id, scale)                                  \
    static VirtualKnob name = VirtualKnob(pot, label)                            \
        .Linear(-1.f, 1.f).Ident(id)                                             \
        .Help("How much of the PATCH source, and which way (noon off). Full " \
              "scale is 5 V: " scale ".")                                        \
        .Ring(Bipolar(kGreen, kPink, kNotch));
DEPTH_KNOB(d_osc_a, kPotTopLeft,     "Osc A cv",  "depth_osc_a",  "5 octaves")
DEPTH_KNOB(d_osc_b, kPotTopRight,    "Osc B cv",  "depth_osc_b",  "5 octaves")
DEPTH_KNOB(d_filt,  kPotMiddleLeft,  "Filter cv", "depth_filter", "5 octaves")
DEPTH_KNOB(d_rate,  kPotMiddleRight, "Rate cv",   "depth_rate",   "5 octaves")
DEPTH_KNOB(d_gain,  kPotBottomLeft,  "Gain cv",   "depth_gain",   "1.5 of GAIN's 2.75 range")
DEPTH_KNOB(d_damp,  kPotBottomRight, "Damp cv",   "depth_damp",   "1.0 of DAMP's 2.0 range")
#undef DEPTH_KNOB

static Page jolin_page = Page(kPageJolin).Name("Jolin").Color("#2060ff")
    .Knobs(osc_a, osc_b, run_a, run_b, run_f, loop_off);
static Page filter_page = Page(kPageFilter).Name("Filter").Color("#20c0ff")
    .Knobs(cutoff, reso, filt_in, drive, out_l, out_r);
static Page hyp_page = Page(kPageHyp).Name("Hyp").Color("#b030ff")
    .Knobs(rate, fine, gain, damp, shape, symm);
static Page patch_page = Page(kPagePatch).Name("Patch").Color("#ffa020")
    .Knobs(p_osc_a, p_osc_b, p_filt, p_rate, p_gain, p_damp);
static Page depth_page = Page(kPageDepth).Name("Depth").Color("#30ff60")
    .Knobs(d_osc_a, d_osc_b, d_filt, d_rate, d_gain, d_damp);

/* ---- descriptor metadata -------------------------------------------------- */

static Jack jk_ext  ("J1",  "Filter ext in",  JackSig::AudioIn);
static Jack jk_clk  ("J2",  "Rungler clock",  JackSig::AudioIn);
static Jack jk_cv3  ("J3",  "CV (Osc A)",     JackSig::CvBi);
static Jack jk_cv4  ("J4",  "CV (Osc B)",     JackSig::CvBi);
static Jack jk_cv5  ("J5",  "CV (Filter)",    JackSig::CvBi);
static Jack jk_cv6  ("J6",  "CV (Rate)",      JackSig::CvBi);
static Jack jk_out7 ("J7",  "CV out (Rungler)", JackSig::CvUni);
static Jack jk_out8 ("J8",  "CV out (X)",     JackSig::CvUni);
static Jack jk_l    ("J9",  "Out L",          JackSig::AudioOut);
static Jack jk_r    ("J10", "Out R",          JackSig::AudioOut);

static VirtualButton bt_b1 = VirtualButton("b1", "Loop")
    .Action("Tap", "The LOOP switch on / off")
    .Action("With B3 held", "Kick the Hypster (restart it after it has stopped)");
static VirtualButton bt_b2 = VirtualButton("b2", "Page")
    .Action("Tap", "Jolin -> Filter -> Hyp -> Patch");
static VirtualButton bt_b3 = VirtualButton("b3", "Depth")
    .Action("Hold", "Show the Depth page (the patch attenuverters)");

static Manual manual = Manual()
    .Tagline("Benjolin + Hypster: stepped chaos meets smooth hyperchaos")
    .Preamble(
        "**Hypjolin** pairs Rob Hordijk's Benjolin (two oscillators, the "
        "rungler shift register and a resonant filter) with Ian Fritz's "
        "Hypster (four integrators in a ring, a fourth-order hyperchaos "
        "generator), after Triglav Modular's anti-copyright Hypjolin. The "
        "original's banana patchbay is the PATCH page (a source per input) "
        "and the DEPTH page (hold B3: how much). By default the Hypster's X "
        "wobbles osc A, its Y sweeps the filter, and the rungler pushes the "
        "Hypster's rate about.");

/* ---- persisted extras: CPU readout and the LOOP switch -------------------- */

struct HypExtras : public Serializable
{
    float cpu_peak = 0.f, cpu_avg = 0.f;
    bool  loop = false;

    size_t SerializedSize() const override { return 9u; }
    void Serialize(uint8_t* out) const override
    {
        memcpy(out, &cpu_peak, 4u);
        memcpy(out + 4u, &cpu_avg, 4u);
        out[8] = loop ? 1u : 0u;
    }
    bool Deserialize(const uint8_t* in) override
    {
        float p;
        memcpy(&p, in, 4u);
        cpu_peak = (p >= 0.f && p <= 1.f) ? p : 0.f;
        memcpy(&p, in + 4u, 4u);
        cpu_avg = (p >= 0.f && p <= 1.f) ? p : 0.f;
        loop = in[8] == 1u;
        return true;
    }
    /* 'HYP' << 8 | layout version. */
    uint32_t SchemaHash() const override { return (0x485950u << 8) | 0x01u; }

    bool Track(daisy::CpuLoadMeter& meter, float saved_peak)
    {
        float mx = meter.GetMaxCpuLoad();
        if (mx > 1.f) mx = 1.f;
        if (mx > cpu_peak) cpu_peak = mx;
        float av = meter.GetAvgCpuLoad();
        if (av > 1.f) av = 1.f;
        if (av > cpu_avg) cpu_avg = av;
        return cpu_peak > saved_peak + 0.05f;
    }
};

/* ---- surfaces ------------------------------------------------------------ */

static ControlLoop loop(hw);
static Pager       pager = Pager(kNumAppPages, kNumPots)
                               .Cycle(hw.buttons[kButtonB2], kPageJolin, kPageFilter, kPageHyp, kPagePatch)
                               .Shift(hw.buttons[kButtonB3], kPageDepth);

/* Home slot 1: the last free one (memory reference_alchemy_lab_slots). */
static mi::HomeSlot home(1u);
static Presets      presets(hw.seed.qspi);
static Settings     settings(hw, &pager);
static CvMatrix     cv_matrix(kNumCvInputs);
static SdCard       sd;
static hostlink::FsExtension fs_ext(sd);
static hostlink::Host host(presets, "hypjolin_alchemy", "Hypjolin",
                           HYPJOLIN_VERSION, HYPJOLIN_GIT_HASH);
static HypExtras    extras;
static SelectorHandle s_clock, s_cvout[2], s_jack[kNumCvIn];

#ifdef HYPJOLIN_BENCH_USB
static daisy::UsbHandle          bench_usb;
static hostlink::CdcUsbTransport bench_cdc;
#endif

/* ---- control frame -> audio block ---------------------------------------- */

/* Written by the control thread, copied by the callback once per block;
 * fields are single words, so a torn copy is a one-block mix, never a torn
 * float. */
static hj::Params       P;
static volatile uint8_t g_out_l = hj::kSrcLopass, g_out_r = hj::kSrcXor;
static volatile uint8_t g_cv_out[2] = {hj::kSrcRungler, hj::kSrcX};
static volatile uint8_t g_jack_dest[kNumCvIn] = {hj::kDestOscA, hj::kDestOscB, hj::kDestFilter, hj::kDestRate};
static volatile bool    g_kick = false;

/* Read back by the LEDs. */
static volatile float G_RUNG = 0.f, G_X = 0.f, G_Y = 0.f;

/* ---- buttons (1 ms poll) -------------------------------------------------- */

static mi::Swallow swallow;
static bool        b1_down = false;
static volatile bool g_loop_tap = false;

static void OnPoll(uint32_t now)
{
    (void)now;
    if (settings.IsActive())
    {
        b1_down = false;
        swallow.All();
        return;
    }
    const bool b1 = swallow.Live(hw, kButtonB1);
    const bool b3 = swallow.Live(hw, kButtonB3);
    if (b1 && !b1_down)
    {
        if (b3) g_kick = true;
        else    g_loop_tap = true;
    }
    b1_down = b1;
}

/* ---- control frame (~60 Hz) ------------------------------------------------ */

static mi::StoredWatch<kNumPots> watch[kNumAppPages];
static float g_saved_peak  = 0.0f;
static bool  prev_settings = false;
static int   prev_sel[1 + 2 + kNumCvIn];

static int sel(VirtualKnob& k, int n)
{
    int v = (int)k.Value();
    return v < 0 ? 0 : (v >= n ? n - 1 : v);
}
static int selh(SelectorHandle& h, int n)
{
    int v = (int)h.Value();
    return v < 0 ? 0 : (v >= n ? n - 1 : v);
}

static void OnFrame(void)
{
    const uint32_t now = System::GetNow();

    for (uint8_t pg = 0; pg < kNumAppPages; pg++)
        if (watch[pg].Changed(pager, pg)) home.Touch(now);

    if (g_loop_tap)
    {
        g_loop_tap  = false;
        extras.loop = !extras.loop;
        home.Touch(now);
    }

    P.osc_a       = mi::clampf(osc_a.Value(), 0.f, 1.f);
    P.osc_b       = mi::clampf(osc_b.Value(), 0.f, 1.f);
    P.run_a       = mi::clampf(run_a.Value(), 0.f, 1.f);
    P.run_b       = mi::clampf(run_b.Value(), 0.f, 1.f);
    P.run_f       = mi::clampf(run_f.Value(), 0.f, 1.f);
    P.loop_offset = mi::clampf(loop_off.Value(), 0.f, 1.f);
    P.loop        = extras.loop;

    P.cutoff  = mi::clampf(cutoff.Value(), 0.f, 1.f);
    P.reso    = mi::clampf(reso.Value(), 0.f, 1.f);
    P.filt_in = (uint8_t)sel(filt_in, hj::kNumFiltIn);
    P.drive   = mi::clampf(drive.Value(), 0.f, 1.f);
    g_out_l   = (uint8_t)(sel(out_l, hj::kNumSrc - 1) + 1);
    g_out_r   = (uint8_t)(sel(out_r, hj::kNumSrc - 1) + 1);

    P.rate  = mi::clampf(rate.Value(), 0.f, 1.f);
    P.fine  = mi::clampf(fine.Value(), 0.f, 1.f);
    P.gain  = mi::clampf(gain.Value(), 0.f, 1.f);
    P.damp  = mi::clampf(damp.Value(), 0.f, 1.f);
    P.shape = mi::clampf(shape.Value(), 0.f, 1.f);
    P.symm  = sel(symm, 2) == 1;

    VirtualKnob* const src[hj::kNumDest] = {&p_osc_a, &p_osc_b, &p_filt, &p_rate, &p_gain, &p_damp};
    VirtualKnob* const amt[hj::kNumDest] = {&d_osc_a, &d_osc_b, &d_filt, &d_rate, &d_gain, &d_damp};
    for (uint8_t d = 0; d < hj::kNumDest; d++)
    {
        P.src[d] = (uint8_t)sel(*src[d], hj::kNumSrc);
        P.amt[d] = mi::clampf(amt[d]->Value(), -1.f, 1.f);
    }

    /* Settings: the clock, the two CV outs, where J3-J6 go. */
    int v[1 + 2 + kNumCvIn];
    v[0] = selh(s_clock, 2);
    for (int i = 0; i < 2; i++) v[1 + i] = selh(s_cvout[i], hj::kNumSrc - 1);
    for (int i = 0; i < kNumCvIn; i++) v[3 + i] = selh(s_jack[i], hj::kNumDest);
    static bool primed = false;
    for (int i = 0; i < 1 + 2 + kNumCvIn; i++)
    {
        if (primed && v[i] != prev_sel[i]) home.Touch(now);
        prev_sel[i] = v[i];
    }
    primed = true;
    P.ext_clock = v[0] == 1;
    for (int i = 0; i < 2; i++) g_cv_out[i] = (uint8_t)(v[1 + i] + 1);
    for (int i = 0; i < kNumCvIn; i++) g_jack_dest[i] = (uint8_t)v[3 + i];

    if (extras.Track(cpu, g_saved_peak)) home.Touch(now);

    const bool sact = settings.IsActive();
    if (prev_settings && !sact) home.Touch(now);
    prev_settings = sact;

    if (home.Service(presets, hw, now, sact || picker::Busy()))
        g_saved_peak = extras.cpu_peak;
}

/* ---- LEDs ------------------------------------------------------------------ */

/*
 * The SDK draws the rings from the knobs. Added:
 *   B1 pair  the LOOP switch: off = the rungler's level in blue, on = cyan
 *            with the level riding on it
 *   B2 pair  the page colour; red when the callback averages over 80 %
 *   B3 pair  the Hypster: X as red, Y as blue (a two-colour Lissajous)
 *   P1 ring  the previous session's worst CPU load, 2.5 s at boot
 */
static uint32_t g_readout_until = 0;
static float    g_boot_peak     = 0.0f;

static LedPanel::Rgb scaled(LedPanel::Rgb c, float k)
{
    k = mi::clampf(k, 0.f, 1.f);
    return {(uint8_t)(c.r * k), (uint8_t)(c.g * k), (uint8_t)(c.b * k)};
}

static void OnRender(uint32_t t_ms)
{
    LedPanel& L = hw.leds;
    if (t_ms < g_readout_until) mi::paint_cpu_readout(hw, g_boot_peak);
    if (settings.IsActive()) return;   /* Settings owns the buttons */

    const uint8_t page = pager.Page();
    LedPanel::Rgb b1 = extras.loop ? scaled(kCyan, 0.35f + 0.65f * G_RUNG)
                                   : scaled(kBlue, 0.15f + 0.5f * G_RUNG);
    if (b1.r < kIdle.r && b1.g < kIdle.g && b1.b < kIdle.b) b1 = kIdle;
    L.SetButtonPair(kButtonB1, L.ScaleGlobal(b1));

    const LedPanel::Rgb b2 = cpu.GetAvgCpuLoad() > 0.80f
                                 ? kRed
                                 : kPageCol[page < kNumAppPages ? page : 0];
    L.SetButtonPair(kButtonB2, L.ScaleGlobal(b2));

    const float x = mi::clampf(0.5f + 0.5f * G_X, 0.f, 1.f);
    const float y = mi::clampf(0.5f + 0.5f * G_Y, 0.f, 1.f);
    const LedPanel::Rgb b3 = {(uint8_t)(0xFF * x), 0x00, (uint8_t)(0xFF * y)};
    L.SetButtonPair(kButtonB3, L.ScaleGlobal(b3));
}

/* ---- audio ----------------------------------------------------------------- */

static hj::Params P_audio;

static void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                          daisy::AudioHandle::OutputBuffer out,
                          size_t                           size)
{
    cpu.OnBlockStart();

    if (size != kBlockSize)
    {
        for (size_t i = 0; i < size; i++) { out[0][i] = 0.f; out[1][i] = 0.f; }
        cpu.OnBlockEnd();
        return;
    }
    if (g_kick) { g_kick = false; engine.KickHypster(); }

    P_audio = P;
    for (uint8_t d = 0; d < hj::kNumDest; d++) P_audio.ext_cv[d] = 0.f;
    for (uint8_t j = 0; j < kNumCvIn; j++)
    {
        const uint8_t d = g_jack_dest[j];
        if (d < hj::kNumDest) P_audio.ext_cv[d] += hw.cv_jacks[j].Volts();
    }

    engine.Process(P_audio, in[0], in[1], out[0], out[1], g_out_l, g_out_r, (int)size);

    /* J7/J8: a source as 0-5 V. The rungler is 0..1 already; the rest are
     * bipolar, centred on 2.5 V. */
    for (int i = 0; i < 2; i++)
    {
        const uint8_t s = g_cv_out[i];
        const float   v = engine.Source(s);
        const float   volts = s == hj::kSrcRungler ? 5.f * v : 2.5f + 2.5f * v;
        hw.cv_jacks[kCvOut[i]].SetVolts(mi::clampf(volts, 0.f, 5.f));
    }

    G_RUNG = engine.Source(hj::kSrcRungler);
    G_X    = engine.Source(hj::kSrcX);
    G_Y    = engine.Source(hj::kSrcY);

    cpu.OnBlockEnd();
}

/* ---- boot ------------------------------------------------------------------ */

int main(void)
{
    hw.Init(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ, kBlockSize);
    cpu.Init(hw.SampleRate(), (int)hw.BlockSize());
    if (hw.BlockSize() != kBlockSize) mi::fault_forever(hw);

    engine.Init();

    for (int i = 0; i < 2; i++)
    {
        hw.cv_jacks[kCvOut[i]].SetVolts(0.f);
        hw.cv_jacks[kCvOut[i]].EnableCvOutput();
    }

    sd.Init();
    picker::Install(settings, kSettingsFirmware, sd, hw);
    settings.UseBrightness();
    settings.UsePresets(presets);
    s_clock = settings.Page(kSettingsMain).Pot(1)
        .Selector(kClockLabels).Default(0)
        .Ident("clock").Name("Clock")
        .Help("What clocks the rungler: osc B's pulse (the Benjolin) or "
              "rising edges at J2.");
    s_cvout[0] = settings.Page(kSettingsMain).Pot(4)
        .Selector(kOutLabels).Default(hj::kSrcRungler - 1)
        .Ident("cv_out_j7").Name("J7 out")
        .Help("What J7 puts out, 0-5 V (bipolar sources centred on 2.5 V). "
              "Written once per 0.5 ms block.");
    s_cvout[1] = settings.Page(kSettingsMain).Pot(5)
        .Selector(kOutLabels).Default(hj::kSrcX - 1)
        .Ident("cv_out_j8").Name("J8 out")
        .Help("What J8 puts out, 0-5 V (bipolar sources centred on 2.5 V).");
    {
        static const char* const kIds[kNumCvIn]   = {"jack_j3", "jack_j4", "jack_j5", "jack_j6"};
        static const char* const kNames[kNumCvIn] = {"J3 to", "J4 to", "J5 to", "J6 to"};
        settings.Page(kSettingsJacks).Name("Jacks");
        for (uint8_t j = 0; j < kNumCvIn; j++)
            s_jack[j] = settings.Page(kSettingsJacks).Pot(j)
                .Selector(kDestLabels).Default(j)
                .Ident(kIds[j]).Name(kNames[j])
                .Help("Which of the six inputs this jack adds to, after the "
                      "patch: Osc A / Osc B / Filter / Rate at 1 V/oct, Gain "
                      "0.3 and Damp 0.2 per volt.");
    }

    /* Every jack is read raw in the callback. */
    for (uint8_t j = 0; j < 6; j++) cv_matrix.Jack(j).Off();

    for (uint8_t pg = 0; pg < kNumAppPages; pg++) pager.SetPageColor(pg, kPageCol[pg]);

    host.Product("Hypjolin")
        .BootSlot(home.slot)
        .Pages(jolin_page, filter_page, hyp_page, patch_page, depth_page)
        .Jacks(jk_ext, jk_clk, jk_cv3, jk_cv4, jk_cv5, jk_cv6,
               jk_out7, jk_out8, jk_l, jk_r)
        .Buttons(bt_b1, bt_b2, bt_b3)
        .Attach(manual)
        .Extend(fs_ext);
#ifdef HYPJOLIN_BENCH_USB
    bench_cdc.Init(bench_usb, daisy::UsbHandle::FS_INTERNAL, "Hypjolin (bench)");
    host.Transport(bench_cdc);
#endif

    presets.Manage(pager);
    presets.Manage(settings);
    presets.Manage(extras);
    presets.Init();
    const bool had_boot = home.Boot(presets);

    float phys[kNumPots];
    for (int i = 0; i < 8; i++)
    {
        hw.ProcessAllControls();
        System::Delay(1);
    }
    for (uint8_t p = 0; p < kNumPots; p++) phys[p] = hw.pots[p].Value();

    if (!had_boot)
    {
        /* First boot after a flash: the engine's defaults (hj::Params), so
         * it makes a sound straight away. Selectors store (index + 0.5) / n. */
        const hj::Params d;
        auto s = [](int i, int n) { return ((float)i + 0.5f) / (float)n; };
        const float jol[kNumPots] = {d.osc_a, d.osc_b, d.run_a, d.run_b, d.run_f, d.loop_offset};
        const float fil[kNumPots] = {d.cutoff, d.reso, s(d.filt_in, hj::kNumFiltIn), d.drive,
                                     s(hj::kSrcLopass - 1, hj::kNumSrc - 1), s(hj::kSrcXor - 1, hj::kNumSrc - 1)};
        const float hyp[kNumPots] = {d.rate, d.fine, d.gain, d.damp, d.shape, s(1, 2)};
        float pat[kNumPots], dep[kNumPots];
        for (uint8_t i = 0; i < kNumPots; i++)
        {
            pat[i] = s(d.src[i], hj::kNumSrc);
            dep[i] = 0.5f + 0.5f * d.amt[i];
        }
        for (uint8_t p = 0; p < kNumPots; p++)
        {
            pager.SetStored(kPageJolin, p, jol[p], phys);
            pager.SetStored(kPageFilter, p, fil[p], phys);
            pager.SetStored(kPageHyp, p, hyp[p], phys);
            pager.SetStored(kPagePatch, p, pat[p], phys);
            pager.SetStored(kPageDepth, p, dep[p], phys);
        }
    }

    g_boot_peak     = extras.cpu_peak;
    extras.cpu_peak = 0.0f;
    extras.cpu_avg  = 0.0f;
    g_readout_until = System::GetNow() + 2500u;

    hw.StartAudio(AudioCallback);
    cpu.Reset();

    loop.Use(pager)
        .Use(settings)
        .Use(cv_matrix)
        .Use(jolin_page)
        .Use(filter_page)
        .Use(hyp_page)
        .Use(patch_page)
        .Use(depth_page)
        .Use(host)
        .OnFrame(OnFrame)
        .OnPoll(OnPoll)
        .OnRender(OnRender);

    for (;;) loop.Tick();
}
