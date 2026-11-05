# CLAUDE.md

Notes for coding agents (and people) working on this branch, `esp32-s31` of platima/tiny386.

## What this is

- [tiny386](https://github.com/hchunhui/tiny386) at the top level, merged from upstream's master at the start of each
  version, with bare-metal firmware for the ESP32-S31 (the Korvo-1, and the DOS board) on top.
- `i386.c` is tiny386's interpreter. `i386_fast.h` is the firmware's fast interpreter in front of it (v5.0, from
  tab5-pc), switched by `[esp] fast_cpu`. `fast/` is upstream's own fast path; the firmware does not build it.
- `esp/` is the ESP-IDF app (`esp/main`, one board header each). `s31/` holds this branch's notes, reviews, test plans,
  card inis, flash scripts and tools, and exists only here. `tests/host` and `tests/lockstep` are the checks.
- `qw.h` and `qw-config.h` are v5.2's speed changes, each behind a switch (0 is the v5.1 code).

## Rules that keep it working

- The guest must see the same machine with `fast_cpu` on or off, and the same as the reference release unless a change
  says why. Every change to `i386.c`, `i386_fast.h` or a device goes through the lockstep tester against the reference
  (`tests/lockstep/README.md`) and `tests/host/run.sh`, which includes `test_fast_cpu` and its planted faults. When a
  change moves a line a planted fault's `sed` pattern looks for, update the pattern: a fault that is not put in fails.
- `i386_fast.h` keeps its own copies of some of `i386.c`'s rules (paging rights, the real-mode INT, segment loads,
  descriptor reads). An upstream fix to the old lines in `i386.c` merges without a conflict and does not reach the
  copy. At each merge, read upstream's `i386.c` hunks against `i386_fast.h`, `TLB_WALK()`, `LPGNO_RIGHTS()`,
  `lpgno_fault()`, `fast_translate()`, `read_desc()` and `op_int`.
- The lockstep builds are 32-bit x86, so a compiler flag for the board is checked only by RV32 runs and on the board.
- Internal RAM: keep at least 64 KiB free at the board's least-free point.
- Never `idf.py flash` the dual-boot build: write it with `s31/flash-dualboot.bat`.

## Building and testing

```sh
make prepare HOSTCC=cc
cd esp && idf.py -B build-korvo1 -DBOARD=korvo1 -DIDF_TARGET=esp32s31 build
bash tests/host/run.sh
```

ESP-IDF's version and the other boards' builds are in `s31/README.md`; the lockstep tester's use is in its README.

## Upstream

This branch never goes upstream as a whole. A fix for tiny386 itself goes to hchunhui/tiny386 as a small PR of its
own, cut fresh from upstream's master, never cherry-picked from this branch's history.

## Writing

Notes, comments and commit messages: Australian English, plain and understated, no em dashes or double hyphens.
