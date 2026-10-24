# LAB-4T

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

**LAB-4T v0.9.6** is a fork of [Felucca](https://github.com/hugelton/Felucca) 1.6.1 (Leo Kuroshita, Hügelton Instruments), a multi-engine
synthesizer firmware for the M-VAVE FM-1. Lab = a place to try things out; 4T = the four audio tracks. It is GPL-3.0-only like Felucca, keeps its credits
([CREDITS.md](CREDITS.md)) and is **not** affiliated with or endorsed by Hügelton Instruments or M-VAVE.

What is different from Felucca 1.6.1 is in [CHANGES.md](CHANGES.md): **CLK kept** (INT / USB / TRS) across
power-offs and project loads. The sound is Felucca's, unchanged.

Felucca's author has stepped back from active development (see the [Maintenance Notice](https://github.com/hugelton/Felucca#maintenance-notice)); LAB-4T carries on from 1.6.1 with small, tested changes.

Installing is at your own risk: the installer's **Return to official V15** or M-VAVE's updater takes you back.

- Install: [web installer](https://omaunaproducer.github.io/LAB-4T/webapp/installer/) in Chrome or Edge (Web MIDI), or `tools/fm1_install.py` from a terminal
- Edit: [web editor](https://omaunaproducer.github.io/LAB-4T/webapp/editor/)
- Try it in your browser: [LAB-4T Try](https://omaunaproducer.github.io/LAB-4T/webapp/try/), no FM-1 needed
- Package: the `.fwsc` of each build is an artifact of the run in [Actions](https://github.com/oMauNaProducer/LAB-4T/actions)
- What changed: [CHANGES.md](CHANGES.md) · credits: [CREDITS.md](CREDITS.md)
- Build: [BUILDING.md](BUILDING.md); clone with the engines' submodule: `git clone --recurse-submodules https://github.com/oMauNaProducer/LAB-4T.git`
- Felucca's own manual (still valid for everything not in CHANGES.md): the [Felucca wiki](https://github.com/hugelton/Felucca/wiki/Home):
  [Installation](https://github.com/hugelton/Felucca/wiki/Installation) · [Controls](https://github.com/hugelton/Felucca/wiki/Controls) ·
  [Menu](https://github.com/hugelton/Felucca/wiki/Menu) · [Engines](https://github.com/hugelton/Felucca/wiki/Engines) ·
  [Troubleshooting](https://github.com/hugelton/Felucca/wiki/Troubleshooting)
- Tests: `sh tests/ci_quick.sh` (a compiler and `build/gen` from one `./build.sh`); `tests/run_tests.sh` is the full suite
- Upstream: [Felucca by Hügelton Instruments](https://github.com/hugelton/Felucca)

## What it is

Thirteen engines with factory presets, four tracks (8 voices shared), a 64-step sequencer (chords, ties, chance, conditions,
ratchets, nudge, locks, automation, live and step recording), SEQ TOOLS, songs, effects, a MOD matrix, MIDI (USB and TRS,
per-track channels, MIDI LEARN) and USB audio. More: [Features](https://github.com/hugelton/Felucca/wiki/Features).

## AI disclaimer

Felucca is developed with the assistance of AI coding agents, see
[On AI-Assisted Development and Responsibility](https://github.com/hugelton/Felucca/discussions/166). The LAB-4T changes were also written with the
assistance of an AI coding agent (Claude) and tested on the host with the suites in `tests/`; the direction and the testing on the device are the maintainer's.

## Credits and licence

Free software: [GPL-3.0-only](LICENSE), the Hügelton Sample Pack included; the bundled fonts and ported DSP keep their own licences,
and five engines ([Felucca-Engines](https://github.com/hugelton/Felucca-Engines), `firmware/engines/`) are MIT
([LICENSES/](LICENSES/), [LICENSING.md](LICENSING.md)). Thanks to everyone whose work and contributions are in it: [CREDITS.md](CREDITS.md).

M-VAVE and FM-1 are trademarks of their respective owners.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, and the LAB-4T contributors
