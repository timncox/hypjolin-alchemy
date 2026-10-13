# Hypjolin — Benjolin + Hypster for the Alchemy Lab

Firmware for the Hermetic Modular Alchemy Lab V2: Rob Hordijk's **Benjolin**
(two oscillators, the rungler, a resonant filter) and Ian Fritz's **Hypster**
(a fourth-order hyperchaos generator: four integrators in a ring), with an
internal patchbay between them. It follows Triglav Modular's **Hypjolin**,
which Triglav released anti-copyright (github.com/triglav-modular/Hypjolin).
It is not affiliated with Triglav Modular, Rob Hordijk's estate or Ian Fritz.
`src/hypjolin/DESIGN.md` marks which parts come from the circuits and which
were invented.

    make libdaisy     build lib/libDaisy once after cloning
    make              build/hypjolin_alchemy.bin (BOOT_SRAM)
    make test         native engine tests (no hardware)

MIT licence.
