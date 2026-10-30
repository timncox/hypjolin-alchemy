/*
 * Benjolin on the Hermetic Modular Alchemy Lab V2 -- Rob Hordijk's Benjolin
 * alone, in three pages: the Hypjolin firmware (src/hypjolin) with the
 * Hypster taken out. Same engine (hypjolin::Engine, Params.hypster = false),
 * same knobs where they survive. See src/benjolin/DESIGN.md.
 *
 *   JOLIN   OSC A . OSC B . RUN>A . RUN>B . RUN>F . LOOP offset
 *   FILTER  CUTOFF . RESO . INPUT . DRIVE . OUT L . OUT R
 *   PATCH   one row per input, source on the left, amount on the right:
 *           >OSC A . amount / >OSC B . amount / >FILTER . amount
 *   B1      the LOOP switch
 *   B2      next page (JOLIN -> FILTER -> PATCH)
 *   B3      tap: a new random rungler pattern (LOOP then holds it)
 *   J1 filter EXT in . J2 rungler clock in (Settings Clock = J2)
 *   J3-J5 CV into OSC A / OSC B / FILTER at 1 V/oct; J6 a LOOP gate
 *   (Settings page 2 re-points each of J3-J6)
 *   J7 / J8 any output as 0-5 V (Settings; Rungler and Triangle B)
 *   J9 / J10 the two audio outs (OUT L / OUT R)
 *
 * The name (BENJOLIN_TARGET / BENJOLIN_PRODUCT) comes from
 * src/benjolin/name.mk, the one place it is set.
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
#include "home_state.h"
#include "picker.h"
#include "hypjolin_engine.h"
#include "benjolin_map.h"

#if !defined(BENJOLIN_TARGET) || !defined(BENJOLIN_PRODUCT)
#error "BENJOLIN_TARGET / BENJOLIN_PRODUCT come from src/benjolin/name.mk (make FW=benjolin)"
#endif
#ifndef BENJOLIN_VERSION
#define BENJOLIN_VERSION "0.0.0"
#endif
#ifndef BENJOLIN_GIT_HASH
#define BENJOLIN_GIT_HASH "dev"
#endif

using namespace alchemy;
using daisy::System;
namespace hj = hypjolin;
namespace bj = benjolin;

/* ---- geometry ----------------------------------------------------------- */

static constexpr uint32_t kBlockSize = 24u;

using bj::kPageJolin;
using bj::kPageFilter;
using bj::kPagePatch;
static constexpr uint8_t kNumAppPages = bj::kNumPages;
enum : uint8_t { kSettingsMain = 0, kSettingsFirmware = 1, kSettingsJacks = 2 };

/* CV indices: 0 = J3 ... 5 = J8. */
static constexpr uint8_t kNumCvIn = bj::kNumCvIn;
static constexpr uint8_t kCvOut[2] = {4u, 5u};

/* ---- hardware + engine -------------------------------------------------- */

static AlchemyLab          hw;
static daisy::CpuLoadMeter cpu;
static hj::Engine          engine;

/* ---- colours: Benjolin blue, as the original's banana jacks -------------- */

static constexpr LedPanel::Rgb kBlue   = {0x20, 0x60, 0xFF};
static constexpr LedPanel::Rgb kCyan   = {0x20, 0xC0, 0xFF};
static constexpr LedPanel::Rgb kPink   = {0xFF, 0x40, 0xC0};
static constexpr LedPanel::Rgb kAmber  = {0xFF, 0xA0, 0x20};
static constexpr LedPanel::Rgb kGreen  = {0x30, 0xFF, 0x60};
static constexpr LedPanel::Rgb kNotch  = {0xFF, 0xFF, 0xFF};
static constexpr LedPanel::Rgb kWhite  = {0xFF, 0xFF, 0xFF};
static constexpr LedPanel::Rgb kDim    = {0x18, 0x18, 0x18};
static constexpr LedPanel::Rgb kIdle   = {0x00, 0x00, 0x30};
static constexpr LedPanel::Rgb kRed    = {0xFF, 0x00, 0x00};

static constexpr LedPanel::Rgb kPageCol[kNumAppPages] = {kBlue, kCyan, kAmber};

/* Labels index the tables in benjolin_map.h. */
static const char* const kSrcLabels[bj::kNumPatchSrc] = {
    "Off", "Triangle A", "Triangle B", "Pulse A", "Pulse B", "XOR", "PWM", "Rungler", "Lopass"};
static const char* const kOutLabels[bj::kNumOut] = {
    "Triangle A", "Triangle B", "Pulse A", "Pulse B", "XOR", "PWM", "Rungler", "Lopass"};
static const char* const kFiltInLabels[bj::kNumFiltIn] = {
    "PWM", "Ext (J1)", "XOR", "Triangle A", "Triangle B"};
static const char* const kClockLabels[2] = {"Osc B", "J2"};
static const char* const kJackLabels[bj::kNumJackDest] = {"Osc A", "Osc B", "Filter", "Loop gate"};

/* ---- pages --------------------------------------------------------------- */

/* Panel positions, front view:
 *      [P1] B1 [P2]
 *      [P3] B2 [P4]
 *      [P5] B3 [P6]                                                        */

/* JOLIN (as Hypjolin's) */
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

/* FILTER (as Hypjolin's, Benjolin sources only) */
static VirtualKnob cutoff = VirtualKnob(kPotTopLeft, "Cutoff")
    .Linear(0.f, 1.f).Ident("cutoff")
    .Help("Filter frequency, 20 Hz to 20 kHz.")
    .Ring(Level(kBlue));
static VirtualKnob reso = VirtualKnob(kPotTopRight, "Reso")
    .Linear(0.f, 1.f).Ident("reso")
    .Help("Resonance; self-oscillates at the top.")
    .Ring(Level(kBlue));
static VirtualKnob filt_in = VirtualKnob(kPotMiddleLeft, "Input")
    .Selector(bj::kNumFiltIn).Labels(kFiltInLabels).Ident("filt_in")
    .Help("What the filter hears. PWM (osc A against osc B) is the "
          "Benjolin's own normal; Ext is J1.")
    .Ring(SelectorRing(kBlue, kDim, bj::kNumFiltIn));
static VirtualKnob drive = VirtualKnob(kPotMiddleRight, "Drive")
    .Linear(0.f, 1.f).Ident("drive")
    .Help("Level into the filter, into saturation at the top.")
    .Ring(Level(kAmber));
static VirtualKnob out_l = VirtualKnob(kPotBottomLeft, "Out L")
    .Selector(bj::kNumOut).Labels(kOutLabels).Ident("out_l")
    .Help("J9: any of the eight outputs (Lopass is the Benjolin's main out).")
    .Ring(SelectorRing(kAmber, kDim, bj::kNumOut));
static VirtualKnob out_r = VirtualKnob(kPotBottomRight, "Out R")
    .Selector(bj::kNumOut).Labels(kOutLabels).Ident("out_r")
    .Help("J10: any of the eight outputs.")
    .Ring(SelectorRing(kAmber, kDim, bj::kNumOut));

/* PATCH: a row per input -- the source on the left pot, how much (and
 * which way; noon is off) on the right. Full scale is 5 V: 5 octaves. */
#define PATCH_SRC(name, pot, label, id, what)                                   \
    static VirtualKnob name = VirtualKnob(pot, label)                            \
        .Selector(bj::kNumPatchSrc).Labels(kSrcLabels).Ident(id)                 \
        .Help("What is patched into " what ": any of the Benjolin's eight "      \
              "outputs. The pot to its right sets how much.")                    \
        .Ring(SelectorRing(kAmber, kDim, bj::kNumPatchSrc));
#define PATCH_AMT(name, pot, label, id, what)                                   \
    static VirtualKnob name = VirtualKnob(pot, label)                            \
        .Linear(-1.f, 1.f).Ident(id)                                             \
        .Help("How much of the source on the left goes into " what ", and "     \
              "which way (noon off). Full scale is 5 octaves.")                  \
        .Ring(Bipolar(kGreen, kPink, kNotch));
PATCH_SRC(p_osc_a, kPotTopLeft,     ">Osc A",  "patch_osc_a",  "osc A's CV")
PATCH_AMT(d_osc_a, kPotTopRight,    "Osc A cv",  "depth_osc_a",  "osc A")
PATCH_SRC(p_osc_b, kPotMiddleLeft,  ">Osc B",  "patch_osc_b",  "osc B's CV")
PATCH_AMT(d_osc_b, kPotMiddleRight, "Osc B cv",  "depth_osc_b",  "osc B")
PATCH_SRC(p_filt,  kPotBottomLeft,  ">Filter", "patch_filter", "the filter's CV")
PATCH_AMT(d_filt,  kPotBottomRight, "Filter cv", "depth_filter", "the filter")
#undef PATCH_SRC
#undef PATCH_AMT

static Page jolin_page = Page(kPageJolin).Name("Jolin").Color("#2060ff")
    .Knobs(osc_a, osc_b, run_a, run_b, run_f, loop_off);
static Page filter_page = Page(kPageFilter).Name("Filter").Color("#20c0ff")
    .Knobs(cutoff, reso, filt_in, drive, out_l, out_r);
static Page patch_page = Page(kPagePatch).Name("Patch").Color("#ffa020")
    .Knobs(p_osc_a, d_osc_a, p_osc_b, d_osc_b, p_filt, d_filt);

/* In page order, pot order: what benjolin::ToParams reads. */
static VirtualKnob* const kKnobs[kNumAppPages][kNumPots] = {
    {&osc_a, &osc_b, &run_a, &run_b, &run_f, &loop_off},
    {&cutoff, &reso, &filt_in, &drive, &out_l, &out_r},
    {&p_osc_a, &d_osc_a, &p_osc_b, &d_osc_b, &p_filt, &d_filt},
};

/* ---- descriptor metadata -------------------------------------------------- */

static Jack jk_ext  ("J1",  "Filter ext in",  JackSig::AudioIn);
static Jack jk_clk  ("J2",  "Rungler clock",  JackSig::AudioIn);
static Jack jk_cv3  ("J3",  "CV (Osc A)",     JackSig::CvBi);
static Jack jk_cv4  ("J4",  "CV (Osc B)",     JackSig::CvBi);
static Jack jk_cv5  ("J5",  "CV (Filter)",    JackSig::CvBi);
static Jack jk_cv6  ("J6",  "Loop gate",      JackSig::Gate);
static Jack jk_out7 ("J7",  "CV out (Rungler)",    JackSig::CvUni);
static Jack jk_out8 ("J8",  "CV out (Triangle B)", JackSig::CvUni);
static Jack jk_l    ("J9",  "Out L",          JackSig::AudioOut);
static Jack jk_r    ("J10", "Out R",          JackSig::AudioOut);

static VirtualButton bt_b1 = VirtualButton("b1", "Loop")
    .Action("Tap", "The LOOP switch on / off");
static VirtualButton bt_b2 = VirtualButton("b2", "Page")
    .Action("Tap", "Jolin -> Filter -> Patch");
static VirtualButton bt_b3 = VirtualButton("b3", "New")
    .Action("Tap", "A new random rungler pattern (with LOOP on, it then holds)");

static Manual manual = Manual()
    .Tagline("Rob Hordijk's Benjolin in three pages")
    .Preamble(
        "**" BENJOLIN_PRODUCT "** is Rob Hordijk's Benjolin -- two "
        "oscillators, the rungler shift register and a resonant filter -- "
        "on three pages: JOLIN (the oscillators and the rungler), FILTER, "
        "and PATCH, a small patchbay with one source and one amount for "
        "each of osc A, osc B and the filter. It is the Hypjolin firmware "
        "with the Hypster taken out. By default triangle B wobbles osc A "
        "and, gently, the filter.");

/* ---- persisted extras: CPU readout and the LOOP switch -------------------- */

struct BenExtras : public Serializable
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
    /* 'BNJ' << 8 | layout version (Hypjolin's is 'HYP'). */
    uint32_t SchemaHash() const override { return (0x424E4Au << 8) | 0x01u; }

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
/* Three pages on B2, nothing held: B2+B3 stays free for the Settings chord. */
static Pager       pager = Pager(kNumAppPages, kNumPots)
                               .Cycle(hw.buttons[kButtonB2], kPageJolin, kPageFilter, kPagePatch);

/* No QSPI slot (the card's 16 are all taken, memory
 * reference_alchemy_lab_slots): the panel autosaves to
 * 0:/alchemy/state/<BENJOLIN_TARGET>.st. host.BootSlot stays 0. */
static mi::HomeFile home(BENJOLIN_TARGET);
static Presets      presets(hw.seed.qspi);
static Settings     settings(hw, &pager);
static CvMatrix     cv_matrix(kNumCvInputs);
static SdCard       sd;
static hostlink::FsExtension fs_ext(sd);
static hostlink::Host host(presets, BENJOLIN_TARGET, BENJOLIN_PRODUCT,
                           BENJOLIN_VERSION, BENJOLIN_GIT_HASH);
static BenExtras    extras;
static SelectorHandle s_clock, s_cvout[2], s_jack[kNumCvIn];

#ifdef BENJOLIN_BENCH_USB
static daisy::UsbHandle          bench_usb;
static hostlink::CdcUsbTransport bench_cdc;
#endif

/* ---- control frame -> audio block ---------------------------------------- */

/* Written by the control thread, copied by the callback once per block;
 * fields are single words, so a torn copy is a one-block mix, never a torn
 * float. */
static hj::Params       P;
static volatile uint8_t g_out_l = hj::kSrcLopass, g_out_r = hj::kSrcXor;
static volatile uint8_t g_cv_out[2] = {hj::kSrcRungler, hj::kSrcTriB};
static volatile uint8_t g_jack_dest[kNumCvIn] = {bj::kJackDefault[0], bj::kJackDefault[1],
                                                 bj::kJackDefault[2], bj::kJackDefault[3]};
static volatile bool    g_seed = false;
static volatile uint8_t g_seed_bits = 0x5Au;

/* Read back by the LEDs. */
static volatile float G_RUNG = 0.f, G_TRI_A = 0.f, G_TRI_B = 0.f;
static volatile bool  G_LOOP = false;   /* the switch after the J6 gate */

/* ---- buttons (1 ms poll) -------------------------------------------------- */

static constexpr uint32_t kTapMs = 300u;

static mi::Swallow   swallow;
static bool          b1_down = false, b3_down = false, b3_spoilt = false;
static uint32_t      b3_t0 = 0;
static uint32_t      rng = 0x9E3779B9u;
static volatile bool g_loop_tap = false;
static volatile uint32_t g_seed_flash_until = 0;

static uint8_t NewPattern(uint32_t now)
{
    rng ^= now * 2654435761u;
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    uint8_t b = (uint8_t)(rng >> 24);
    /* All-zero or all-one only ever loops as a flat line. */
    if (b == 0x00u || b == 0xFFu) b ^= 0x5Au;
    return b;
}

static void OnPoll(uint32_t now)
{
    if (settings.IsActive())
    {
        b1_down = b3_down = false;
        swallow.All();
        return;
    }
    const bool b1 = swallow.Live(hw, kButtonB1);
    const bool b2 = swallow.Live(hw, kButtonB2);
    const bool b3 = swallow.Live(hw, kButtonB3);

    if (b1 && !b1_down) g_loop_tap = true;
    b1_down = b1;

    /* B3: a short tap on its own. Held with B2 it is the Settings chord. */
    if (b3 && !b3_down) { b3_t0 = now; b3_spoilt = b2; }
    if (b3 && b2) b3_spoilt = true;
    if (!b3 && b3_down && !b3_spoilt && now - b3_t0 < kTapMs)
    {
        g_seed_bits        = NewPattern(now);
        g_seed             = true;
        g_seed_flash_until = now + 80u;
    }
    b3_down = b3;
}

/* ---- control frame (~60 Hz) ------------------------------------------------ */

static mi::StoredWatch<kNumPots> watch[kNumAppPages];
static float g_saved_peak  = 0.0f;
static bool  prev_settings = false;
static int   prev_sel[1 + 2 + kNumCvIn];

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

    /* Settings: the clock, the two CV outs, where J3-J6 go. */
    int v[1 + 2 + kNumCvIn];
    v[0] = selh(s_clock, 2);
    for (int i = 0; i < 2; i++) v[1 + i] = selh(s_cvout[i], bj::kNumOut);
    for (int i = 0; i < kNumCvIn; i++) v[3 + i] = selh(s_jack[i], bj::kNumJackDest);
    static bool primed = false;
    for (int i = 0; i < 1 + 2 + kNumCvIn; i++)
    {
        if (primed && v[i] != prev_sel[i]) home.Touch(now);
        prev_sel[i] = v[i];
    }
    primed = true;

    bj::Panel pn;
    for (uint8_t pg = 0; pg < kNumAppPages; pg++)
        for (uint8_t p = 0; p < kNumPots; p++) pn.v[pg][p] = kKnobs[pg][p]->Value();
    uint8_t ol, orr;
    P       = bj::ToParams(pn, extras.loop, v[0] == 1, ol, orr);
    g_out_l = ol;
    g_out_r = orr;
    for (int i = 0; i < 2; i++) g_cv_out[i] = bj::kOutSrc[v[1 + i]];
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
 *   B1 pair  the LOOP switch (after the J6 gate): off = the rungler's level
 *            in blue, on = cyan with the level riding on it
 *   B2 pair  the page colour; red when the callback averages over 80 %
 *   B3 pair  triangle A as red, triangle B as blue; white for a moment on
 *            a new pattern
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
    LedPanel::Rgb b1 = G_LOOP ? scaled(kCyan, 0.35f + 0.65f * G_RUNG)
                              : scaled(kBlue, 0.15f + 0.5f * G_RUNG);
    if (b1.r < kIdle.r && b1.g < kIdle.g && b1.b < kIdle.b) b1 = kIdle;
    L.SetButtonPair(kButtonB1, L.ScaleGlobal(b1));

    const LedPanel::Rgb b2 = cpu.GetAvgCpuLoad() > 0.80f
                                 ? kRed
                                 : kPageCol[page < kNumAppPages ? page : 0];
    L.SetButtonPair(kButtonB2, L.ScaleGlobal(b2));

    LedPanel::Rgb b3;
    if ((int32_t)(t_ms - g_seed_flash_until) < 0)
        b3 = kWhite;
    else
    {
        const float a = mi::clampf(0.5f + 0.5f * G_TRI_A, 0.f, 1.f);
        const float b = mi::clampf(0.5f + 0.5f * G_TRI_B, 0.f, 1.f);
        b3 = {(uint8_t)(0xFF * a), 0x00, (uint8_t)(0xFF * b)};
    }
    L.SetButtonPair(kButtonB3, L.ScaleGlobal(b3));
}

/* ---- audio ----------------------------------------------------------------- */

static hj::Params P_audio;
static bool       g_gate[kNumCvIn];

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
    if (g_seed) { g_seed = false; engine.SeedRungler(g_seed_bits); }

    P_audio = P;
    uint8_t dest[kNumCvIn];
    float   volts[kNumCvIn];
    for (uint8_t j = 0; j < kNumCvIn; j++)
    {
        dest[j]  = g_jack_dest[j];
        volts[j] = hw.cv_jacks[j].Volts();
    }
    bj::ApplyJacks(P_audio, dest, volts, g_gate);
    G_LOOP = P_audio.loop;

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

    G_RUNG  = engine.Source(hj::kSrcRungler);
    G_TRI_A = engine.Source(hj::kSrcTriA);
    G_TRI_B = engine.Source(hj::kSrcTriB);

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
        .Selector(kOutLabels).Default(bj::IndexOf(bj::kOutSrc, bj::kNumOut, hj::kSrcRungler))
        .Ident("cv_out_j7").Name("J7 out")
        .Help("What J7 puts out, 0-5 V (bipolar sources centred on 2.5 V). "
              "Written once per 0.5 ms block.");
    s_cvout[1] = settings.Page(kSettingsMain).Pot(5)
        .Selector(kOutLabels).Default(bj::IndexOf(bj::kOutSrc, bj::kNumOut, hj::kSrcTriB))
        .Ident("cv_out_j8").Name("J8 out")
        .Help("What J8 puts out, 0-5 V (bipolar sources centred on 2.5 V).");
    {
        static const char* const kIds[kNumCvIn]   = {"jack_j3", "jack_j4", "jack_j5", "jack_j6"};
        static const char* const kNames[kNumCvIn] = {"J3 to", "J4 to", "J5 to", "J6 to"};
        settings.Page(kSettingsJacks).Name("Jacks");
        for (uint8_t j = 0; j < kNumCvIn; j++)
            s_jack[j] = settings.Page(kSettingsJacks).Pot(j)
                .Selector(kJackLabels).Default(bj::kJackDefault[j])
                .Ident(kIds[j]).Name(kNames[j])
                .Help("Where this jack goes: Osc A / Osc B / Filter add to "
                      "the patch at 1 V/oct; Loop gate flips the LOOP switch "
                      "while it is above 1.5 V.");
    }

    /* Every jack is read raw in the callback. */
    for (uint8_t j = 0; j < 6; j++) cv_matrix.Jack(j).Off();

    for (uint8_t pg = 0; pg < kNumAppPages; pg++) pager.SetPageColor(pg, kPageCol[pg]);

    host.Product(BENJOLIN_PRODUCT)
        .Pages(jolin_page, filter_page, patch_page)
        .Jacks(jk_ext, jk_clk, jk_cv3, jk_cv4, jk_cv5, jk_cv6,
               jk_out7, jk_out8, jk_l, jk_r)
        .Buttons(bt_b1, bt_b2, bt_b3)
        .Attach(manual)
        .Extend(fs_ext);
#ifdef BENJOLIN_BENCH_USB
    bench_cdc.Init(bench_usb, daisy::UsbHandle::FS_INTERNAL, BENJOLIN_PRODUCT " (bench)");
    host.Transport(bench_cdc);
#endif

    presets.Manage(pager);
    presets.Manage(settings);
    presets.Manage(extras);
    presets.Init();
    const bool had_boot = home.Boot(presets, sd, System::GetNow());

    float phys[kNumPots];
    for (int i = 0; i < 8; i++)
    {
        hw.ProcessAllControls();
        System::Delay(1);
    }
    for (uint8_t p = 0; p < kNumPots; p++) phys[p] = hw.pots[p].Value();

    if (!had_boot)
    {
        /* First boot: benjolin::Defaults(), so it makes a sound at once. */
        const bj::Panel d = bj::Defaults();
        for (uint8_t pg = 0; pg < kNumAppPages; pg++)
            for (uint8_t p = 0; p < kNumPots; p++)
                pager.SetStored(pg, p, bj::StoredOf(bj::kSpec[pg][p], d.v[pg][p]), phys);
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
        .Use(patch_page)
        .Use(host)
        .OnFrame(OnFrame)
        .OnPoll(OnPoll)
        .OnRender(OnRender);

    for (;;) loop.Tick();
}
