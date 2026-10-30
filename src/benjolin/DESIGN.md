# Benjolin on the Alchemy Lab — design

A Benjolin-only build of Hypjolin, in three pages. Asked for by a tester
(2026-10-30): Hypjolin "works great", but past three pages the module
"starts to feel tedious"; they wanted "a simplified Benjolin type firmware
with 3 pages or less". Tim chose: the Benjolin alone (no Hypster), exactly
three pages, one of them a patch page so the patchbay feel survives.

It is the same engine as Hypjolin (`src/hypjolin/hypjolin_engine.cpp`)
with `Params.hypster = false`, which skips the Hypster entirely (its four
outputs read 0). Everything Benjolin, including the knob ranges, is
Hypjolin's; see `src/hypjolin/DESIGN.md` for what is [circuit] and what is
[invented] there. This file covers only what differs.

**Name:** `benjolin_alchemy` / "Benjolin" is a working name, set once in
`src/benjolin/name.mk`. Renaming it also renames the autosave file.

## The panel

| Page (B2) | P1 | P2 | P3 | P4 | P5 | P6 |
|---|---|---|---|---|---|---|
| **JOLIN** (blue) | OSC A | OSC B | RUN>A | RUN>B | RUN>F | LOOP offset |
| **FILTER** (cyan) | CUTOFF | RESO | INPUT | DRIVE | OUT L | OUT R |
| **PATCH** (amber) | → Osc A source | Osc A amount | → Osc B source | Osc B amount | → Filter source | Filter amount |

- JOLIN is Hypjolin's page unchanged.
- FILTER is Hypjolin's page with the Hypster's options removed: INPUT is
  PWM / Ext (J1) / XOR / Triangle A / Triangle B (5); OUT L / OUT R pick
  from the Benjolin's eight outputs (Triangle A, Triangle B, Pulse A,
  Pulse B, XOR, PWM, Rungler, Lopass).
- **B1**: the LOOP switch (lit cyan when on), as Hypjolin's.
- **B2**: next page, Jolin → Filter → Patch → Jolin. Nothing is held for a
  page: a shift page would be a fourth page.
- **B3**: tap for a new random rungler pattern (the LED pair flashes
  white). With LOOP on and LOOP offset at either end, the rungler then
  holds that pattern, so B3 is "give me another loop". With LOOP off it
  nudges a pattern that keeps evolving anyway. Its LEDs otherwise show
  triangle A in red and triangle B in blue. [invented] B3 + B1 (Hypjolin's
  "kick the Hypster") is gone with the Hypster; B2+B3 held 2 s is still
  Settings.

### Why the PATCH page looks like this

Hypjolin's patchbay took two pages: PATCH (a source per input) and DEPTH
(held B3: how much). Here the Hypster's three inputs (RATE, GAIN, DAMP)
are gone, which leaves exactly the Benjolin's three cv inputs — osc A,
osc B, filter — and six pots is exactly three (source, amount) pairs. So
the patchbay fits on one page as three rows, one per input, read left to
right the way a patch cable runs: **what** (left pot, 9 zones: Off plus
the eight outputs) **into this input, how much** (right pot, bipolar,
noon off, full scale 5 V = 5 octaves). Source and amount for one input
are always side by side, the whole patch is visible at once, and nothing
needs a held button.

Rejected: putting the jack CV amounts on the page instead (it would trade
the internal patching, the part that makes it feel like a patchbay, for
something an external attenuator can do); one source per input with
fixed depth (loses "how much", which is most of the playing).

**Defaults** [invented]: triangle B → osc A at +0.15 (±0.75 octave; the
Benjolin's classic cross-patch, a wobble at osc B's ~8 Hz), osc B row
parked on Pulse A at noon (off), triangle B → filter at +0.10. JOLIN and
FILTER start at Hypjolin's defaults (OUT L Lopass, OUT R XOR).

## Jacks

| Jack | Use |
|---|---|
| J1 | Filter ext in (INPUT = Ext), as Hypjolin |
| J2 | Rungler clock in, when Settings Clock = J2, as Hypjolin |
| J3 / J4 / J5 | CV into Osc A / Osc B / Filter, 1 V/oct, summed after the patch, as Hypjolin |
| J6 | **LOOP gate** [invented]: above 1.5 V (released below 0.5 V) it flips the LOOP switch while held. Was Hypjolin's RATE CV, which has nothing to drive here |
| J7 / J8 | Any of the eight outputs as 0–5 V (Settings; Rungler and Triangle B by default — J8 was the Hypster's X) |
| J9 / J10 | OUT L / OUT R |

Settings (B2+B3 2 s): page 0 Clock (P2), J7 out (P5), J8 out (P6) beside
brightness and presets; page 1 the SD picker; page 2 "Jacks": each of
J3–J6 to Osc A / Osc B / Filter / Loop gate.

## Saving

The panel, Settings and the LOOP switch autosave 5 s after the last
change (hands off) to `0:/alchemy/state/benjolin_alchemy.st` through
`mi::HomeFile` (`src/common/home_state.h` + `state_file.h`, copied from
mi-alchemy), not to a QSPI preset slot: all 16 are taken (memory
`reference_alchemy_lab_slots`). `host.BootSlot` stays 0. Schema tag
`'BNJ'`, distinct from Hypjolin's `'HYP'`, and the pager's three pages
change the live schema too, so neither firmware reads the other's state.

## CPU

With the Hypster off, nothing in the per-sample path depends on a knob's
value: two oscillators, the rungler, the filter (one `tan`, two `tanh`),
four `exp2`. That is the 2026-10-21 lesson (Hypjolin froze when RATE
pushed the Hypster's RK2 substeps past the budget) satisfied by
construction. Host timing (`make -C test fast`, relative only): Benjolin
~0.93 µs per 24-sample block at default and at top RATE alike, Hypjolin
~1.6 µs at default and ~14 µs at top RATE. Never measured on the M7.

## Tests

- `make -C test` — `test_benjolin`: Hypster off (X..U zero, its state
  untouched at top RATE, Benjolin outputs bit-identical to the Hypster-on
  engine when nothing routes from it), every knob of the three pages to its
  Params field, selector zones, defaults, J3–J6, the B3 seed under LOOP,
  400 panels finite and bounded. `make -C test fast` adds the cost asserts.
- `make -C <alchemy-lab>/emu test-custom FW=custom FW_DIR=<repo>/src/benjolin`
  (plus `EMU_ALCHEMY_DIR` / `EMU_LIBDAISY_DIR` in a worktree) — boot,
  pages (exactly three, B3 is no shift page, B1 LOOP), jacks (J3 1 V/oct,
  the PATCH page moving osc A, the J6 gate).

## Not done / unknown

- Never on hardware: not flashed, not heard, CPU not measured on the M7.
- The autosave file is exercised only as far as the emulator boots with it;
  no test reads a saved file back.
- B3's re-seed and the J6 gate are inventions; they may not be what a
  Benjolin player wants from those controls.
