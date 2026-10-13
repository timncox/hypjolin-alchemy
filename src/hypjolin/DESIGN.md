# Hypjolin on the Alchemy Lab — design

Hypjolin is Triglav Modular's Benjolin + Hypster "cross-modulating ultra-chaos
generator" (Budapest, 2023, an edition of 10 for Goodiepal / Bananskolen),
released anti-copyright under the Unlicense with KiCad files and a manual at
github.com/triglav-modular/Hypjolin. Its two halves are Rob Hordijk's
**Benjolin** and Ian Fritz's **Hypster** (Fritz, "The HYPSTER Hyperchaos
Generator", Jan 2018, with schematics).

The original is analog and patched by hand on a banana patchbay: blue jacks
are Benjolin outputs, purple ones Hypster outputs, black ones inputs. This
firmware models both circuits and replaces the patchbay with two pages.

Tags: **[circuit]** from the Hypjolin manual or Fritz's schematic,
**[Benjolin]** standard Benjolin behaviour, **[invented]** a choice made here
to fill a gap or fit the Lab.

## The original panel (from the Hypjolin manual)

- Benjolin knobs: osc A frequency, osc B frequency, filter frequency,
  resonance, rungler to A, rungler to B, rungler to F, loop offset; a LOOP
  switch. Inputs (cv + attenuator each): osc A, osc B, filter; ext. in.
  Outputs: triangle A, triangle B, pulse A, pulse B, xor, pwm, rungler,
  lopass.
- Hypster knobs: rate, fine, gain, damp, shape; a SYMMETRY switch. Inputs
  (cv + attenuator each): rate, gain, damp. Outputs: X, Y, Z, U, −X, −Y, −Z,
  −U, plus X/Y/Z/U LEDs.

## The Lab panel

| Page | P1 | P2 | P3 | P4 | P5 | P6 |
|---|---|---|---|---|---|---|
| **JOLIN** (blue) | OSC A | OSC B | RUN>A | RUN>B | RUN>F | LOOP offset |
| **FILTER** (cyan) | CUTOFF | RESO | INPUT | DRIVE | OUT L | OUT R |
| **HYP** (purple) | RATE | FINE | GAIN | DAMP | SHAPE | SYMM |
| **PATCH** (amber) | → Osc A | → Osc B | → Filter | → Rate | → Gain | → Damp |
| **DEPTH** (green, hold B3) | Osc A cv | Osc B cv | Filter cv | Rate cv | Gain cv | Damp cv |

- **B1**: the LOOP switch (lit cyan when on). With B3 held: kick the Hypster
  (it can stop dead at low GAIN / high DAMP, as the circuit does).
- **B2**: next page. **B3** held: DEPTH. Its LEDs also show the Hypster: X in
  red, Y in blue.
- **PATCH** [invented]: each of the six black inputs takes one source: Off,
  X, Y, Z, U, Triangle A/B, Pulse A/B, XOR, PWM, Rungler, Lopass (13, under
  the SDK's 16-zone ring limit). −X..−U are a negative DEPTH.
- **DEPTH** [circuit]: the panel's six "cv" attenuators, made bipolar
  [invented]. A full-scale source at full DEPTH is 5 V at the input.
- **Defaults** [invented]: X → osc A at 0.3, Y → filter at 0.25, rungler →
  rate at 0.3, so it moves the moment it boots.
- **OUT L / OUT R** [invented]: J9/J10 play any of the twelve outputs
  (default Lopass and XOR), at 0.8 of full scale.

## Jacks

| Jack | Use |
|---|---|
| J1 | Filter ext in (INPUT = Ext) [circuit "ext. in"] |
| J2 | Rungler clock in, when Settings Clock = J2 [invented; the Benjolin V2's external clock] |
| J3–J6 | CV into Osc A / Osc B / Filter / Rate by default; Settings page 2 re-points each at any of the six inputs. 1 V/oct for the pitches and rate, Gain 0.3 / Damp 0.2 per volt |
| J7 / J8 | Any output as 0–5 V (Settings; Rungler and X by default). Bipolar sources centred on 2.5 V. STM32 DAC, written once per 0.5 ms block |
| J9 / J10 | OUT L / OUT R |

Settings (B2+B3 2 s): page 0 Clock (P2), J7 out (P5), J8 out (P6) beside
brightness and presets; page 1 the SD picker; page 2 "Jacks": J3–J6 targets.

## Benjolin

- Two triangle-core oscillators, 18 octaves each (0.03 Hz–7.9 kHz)
  [circuit: "eighteen-octave range"]. Pulse = the core's comparator.
- **Rungler** [Benjolin]: an 8-bit shift register clocked by pulse B's rising
  edge, its data pulse A XOR the last stage; a 3-bit DAC on the last three
  stages (eight levels). RUN>A / RUN>B up to 5 octaves, RUN>F up to 6
  [invented depths].
- **LOOP** [circuit, mechanism invented]: the manual says "when turned on,
  turning the loop offset knob clockwise or anti-clockwise locks the rungler
  in a loop". Here, with LOOP on, LOOP offset biases osc A's comparator by
  ±1.5 (past the triangle's ±1). Near noon new bits still arrive; towards
  either end the comparator stops changing and the register recirculates
  (period 8, or 16 when the constant inverts it).
- **PWM** [Benjolin]: osc A > osc B. **XOR**: pulse A XOR pulse B.
- **Filter** [invented model]: 2-pole TPT state-variable, low-pass out, a
  tanh on the band state so it overdrives itself at high resonance (the
  Benjolin's "slightly chaotic" filter); self-oscillates at the top. Cutoff
  20 Hz–20 kHz. DRIVE is the level into it [invented].

## Hypster

From Fritz's two schematic pages (part 1: the U stage with Gain, Damping,
Rate; part 2: Z, Y, X and the Shape options). Four voltage-controlled
integrators in a ring, x → u → z → y → x, closed through −X'.

    u' = K·G·NL1(−x) − B·u
    z' = K·NL2(u)    − 1.00·z
    y' = K·NL3(z/2)  − 0.21·y  + (1−shape)·c(−x)·0.5
    x' = K·NL4(y/2)  − 0.14·x  + shape·c(−x)·0.5
    (all × the rate; K = 2)

- **Nonlinearities** [circuit, simplified]: Fritz: "two … approximating an
  x−x³ behavior … the two other … (offset) breakpoint singularities".
  NL1, NL2: `a·v − 3.03·deadzone(v, 5.4 V)` (back-to-back 4.7 V zeners plus
  a diode drop; 3.03 = 100k/33k); NL3, NL4: a one-sided breakpoint (diode +
  zener). Slopes a from the resistor ratios.
- **Damping**: z, y, x fixed from R59 82k / R74 220k / R89 330k against the
  82k / 47k / 47k inputs [circuit]; u's from DAMP [circuit].
- **SHAPE** [circuit: Option 2, pot between A (fat) and B (slim)]: −X' fed
  back into the x stage (fat) or the y stage (slim). **SYMM** [circuit: S1
  switches D9 across D8]: full-wave feedback (symmetric) or half-wave.
- **Limiter** [circuit: optional D2–D4]: past ±9 V a stage is pulled back
  hard, so it stays off the rails instead of latching there (Fritz: without
  them "the controls must be returned to near zero once the signal becomes
  saturated").
- **K = 2 was found by simulation** [invented]: Fritz tuned his NL parameters
  in a spreadsheet; the exact loop gain here comes from a sweep
  (`tools/hyp_explore.cpp`) scoring each (GAIN, DAMP) cell for the largest
  Lyapunov exponent. At K = 2 with SYMM on, most of GAIN 0.5–3 × DAMP
  0.25–2 is chaotic with periodic islands; low GAIN with high DAMP stops it.
  Half-wave (SYMM off) is mostly periodic.
- **RATE** [circuit: exponential]: 0.05 Hz–6.5 kHz, calibrated so the X
  output's fundamental at the default GAIN/DAMP matches the readout (within
  25 % across the range, test_hypjolin). FINE ±half an octave. GAIN 0.25–3,
  DAMP 0–2, both linear [circuit: "Gain and Damping have linear CV
  response"].
- Integration: RK2 (midpoint), up to 12 substeps per sample at high rates.
  A 1e-4 noise floor lets it restart (as circuit noise would).

## Tests

- `make -C test` — 48 native checks (ASan + UBSan): mappings, rungler
  shift/DAC, LOOP locking vs LOOP off, Hypster bounded/alive/off-the-rails,
  the limiter at DAMP 0, RATE calibration, chaos (positive Lyapunov) at the
  default, stopping and restarting, SYMM changing the orbit, the patch (X
  moving osc A), 1 V/oct, the J2 clock, EXT through the filter, finite at
  every corner.
- `make -C <alchemy-lab>/emu test-custom FW=custom FW_DIR=<this repo>` — 5
  scripts, 38 checks: boot (sound, finite, unclipped, J7/J8 moving), pages
  and LEDs, the Hypster at audio rate / stopped / kicked back, J3 1 V/oct
  (113 → 226 Hz) and J1 through the filter. Each knob move in a script
  sweeps through 0 and 1 first, because a pot only takes over once it
  crosses the stored value.

## Not done / unknown

- Never on hardware; CPU load unmeasured (the RK2 substeps at high RATE are
  the heaviest part).
- No A/B against a real Hypster or Benjolin; the resemblance is by the
  schematic and the manual, not by ear.
- The Hypjolin's X/Y/Z/U LEDs are folded into B3 (X and Y only).
- The oscillators' pulses are naive (no band-limiting); at high pitches they
  alias, which suits the instrument but is a choice.
