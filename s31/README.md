# tiny386 on the ESP32-S31 🖥️

This branch, `esp32-s31`, is my bare-metal firmware for [tiny386](https://github.com/hchunhui/tiny386), the little x86 PC emulator, on the ESP32-S31. No Linux under it: tiny386 runs straight on ESP-IDF, boots FreeDOS and Windows 95, and draws Windows at the panel's own 800x480.

It runs on the ESP32-S31-Korvo-1, either beside Linux (hold VOL+ through reset for tiny386, or the other way round) or on its own, and on the DOS board, an HDMI board I'm designing for it. One source tree builds all of them.

## What this branch is 🌿

- **Upstream tiny386 underneath.** The branch is hchunhui's master of 2026-10-22 (`4ce2e06`) merged with my work, so `git log` shows what's mine and every upstream commit keeps its own hash.
- **My firmware on top**, v3 to v5.0: the board support, the boot console and crash screen, the status LED, the dual boot, the card and ini handling, the Windows 95 fixes, the VBE drawing at 800x480, the information port that [T386Mon](https://github.com/platima/T386Mon) reads, (v4.4) Windows on a partition of the card of its own, (v4.6) double buffering made at power-on, (v5.0) a fast interpreter in front of tiny386's own, from Yejun Yang's [tab5-pc](https://github.com/yejun/tab5-pc), with the emulated processor storing into the frame buffer itself, and (v5.1) that interpreter made cheaper with tiny386's own back in internal RAM, and a profiler of the emulator's core on the board.
- **`s31/`** - the notes, test plans, reviews, flash scripts, card inis and the evidence behind each release. That folder only exists on this branch.

This branch never goes upstream as a whole. Fixes and improvements that make sense for tiny386 itself go to hchunhui as separate, small PRs, each cut from upstream's master on its own branch. That's what the other branches on this fork are.

## Releases 📦

Each release is a tag: `s31-v3`, `s31-v3.1` up to `s31-v5.0`, and `s31-v5.1` once it is out. v4.4 is the first made on this branch rather than in the scratch repository: its notes are `s31/BUILD-NOTES.md` sections 29 and 30. v4.5 is v4.4 with the fixes of its two reviews: section 31. v4.6 answers my run of v4.5 on the board: double buffering's two frame buffers made at power-on, and the guest's stores and DMA kept out of the devices' state: section 32. v5.0 is for speed: the fast interpreter from tab5-pc (`s31/FAST-CPU.md`) and the frame buffer's own path, checked against v4.6 instruction for instruction with a lockstep tester (`tests/lockstep`): section 33. v5.1 makes the fast interpreter cheaper in four work items, puts tiny386's own interpreter back in internal RAM and adds a profiler of core 1 (`s31/PROFILE.md`), checked against v4.6 the same way, Plat's own 800x480 setup with T386MON among the workloads: section 34. Releases with the images will go on the [releases page](https://github.com/platima/tiny386/releases): the four image sets (Korvo-1 dual boot, the same with tiny386 the default, Korvo-1 on its own, the DOS board) with their ELFs, the card files (`bios.bin`, `vgabios.bin`, the inis, `vbe-reg.img`), the VGA BIOS's source, the patch against upstream, and `SHA256SUMS`. None are up yet.

Binaries aren't committed here - build them, or take them from a release.

## Where to start 📖

| Read | For |
|-|-|
| [`s31/TEST-PLAN.md`](TEST-PLAN.md) | first: every risk to a Linux install, plainly, then the first boot step by step |
| [`s31/FLASH.txt`](FLASH.txt) | writing each image set, and every way back to Linux |
| [`s31/newcard/README.md`](newcard/README.md) | v4.4: a new card with Windows 95's own volume on a partition (`hda = sd:3`), step by step, the old card kept as the fallback |
| [`s31/FIRMWARE.md`](FIRMWARE.md) | the firmware: what each version changed, what's in each folder, what has been seen on a board |
| [`s31/BUILD-NOTES.md`](BUILD-NOTES.md) | how each version was built and checked, section by section |
| [`s31/REVIEW-RESOLUTION.md`](REVIEW-RESOLUTION.md) | every review finding and what became of it |

## Building 🛠️

In Linux or WSL, with ESP-IDF v6.1-dev-8142-g188e3e55bb (the ESP32-S31 needs ESP-IDF's master; that's the commit every release was built with):

```sh
git clone -b esp32-s31 https://github.com/platima/tiny386.git
cd tiny386
make prepare HOSTCC=cc      # the OPL synthesiser's tables, with the host's compiler
cd esp
idf.py -B build-korvo1 -DBOARD=korvo1 -DIDF_TARGET=esp32s31 build
idf.py -B build-korvo1-standalone -DBOARD=korvo1 -DIDF_TARGET=esp32s31 -DPARTITION_CSV=partitions.csv build
idf.py -B build-dosboard -DBOARD=dosboard -DIDF_TARGET=esp32s31 build
```

`build-korvo1` is the app for the Korvo-1 beside Linux (it goes at 0xE20000 with the dual-boot bootloader and table), `build-korvo1-standalone` the Korvo-1 on its own, `build-dosboard` the DOS board. Built this way from the v5.1 code, all three give the v5.1 release build's `tiny386.bin` and `tiny386.elf` byte for byte, and the standalone and DOS board builds their bootloader and table too [verified, 2026-10-28; the same was checked for `s31-v5.0`, `s31-v4.6`, `s31-v4.5`, `s31-v4.4` and `s31-v4.3`]; the dual-boot set's bootloader comes from `s31/dualboot/` instead. Since 2026-10-25 the committed `esp/dependencies.lock` is the S31 one, `s31/dependencies.lock` byte for byte, and the builds leave it as it is (before then it was upstream's, for the S3, and the first build rewrote it).

The dual-boot build refuses `idf.py flash` on purpose: write it with `s31/flash-dualboot.bat`, which backs up the board's flash first.

`s31/build.sh` is the release build: it starts from upstream's `4ce2e06`, applies the release's `tiny386-idf-v4.patch` (this branch's tree against `4ce2e06`, `s31/` left out: `git diff 4ce2e06 s31-v5.1 -- . ':(exclude)s31'`), builds all three, and lays out the four image sets with their `SHA256SUMS`. It needs the patch beside it and the dual-boot bootloader and tables in `s31/dualboot/`, all from the release, and builds the same bytes twice from nothing (`s31/FIRMWARE.md`, "Building again").

## Layout 📁

```
s31/
  README.md               this file
  FIRMWARE.md             the firmware, version by version, and what's in each folder
  TEST-PLAN.md            the steps on the board, and what to photograph
  FLASH.txt               flashing, and every way back to Linux
  BUILD-NOTES.md          how each release was built and checked
  STATUS-LED.md           the status LED's colours and blinks
  FAST-CPU.md             v5.0 and v5.1: the fast interpreter from tab5-pc, how it stays tiny386, its tests and counts
  PROFILE.md              v5.1: the profiler ([esp] profile = 1), PROFILE.TXT, and the report on the PC
  profile/                profile_report.py: PROFILE.TXT and tiny386.elf into a report
  THIRD_PARTY.md          code from other projects (tab5-pc's fast interpreter), with its MIT notice
  LINE-OUT.md             the DOS board's line out and speakers
  REVIEW-*.md             the independent reviews, as their authors left them
  REVIEW-RESOLUTION.md    every finding, one line each
  build.sh                the release build (with dependencies.lock)
  flash-*.bat, back-to-linux.bat, flash-paths.cmd
                          the Windows flash scripts: they check the board, back it up, then write
  card/, card-dosboard/   the inis for the card's tiny386 folder
  newcard/                v4.4: newcard.sh and export-root.sh, a new card with Windows on p3
  dualboot/               the dual boot: DUALBOOT.md, the tables as CSV, scripts, checks
  qemumini-pointer/       a script that changes one byte of your copy of VMDisp9x's driver
  research/               the investigations: Windows 95 on the board, the VBE design, the flicker
  checks/                 the evidence behind BUILD-NOTES.md, text only
  old-to-new.tsv          old commit hashes to new ones (see below)
```

## Old commit hashes 🔁

The firmware grew in a scratch repository on my PC, and its history moved here on 2026-10-24 with the author and committer set to my GitHub address. Nothing else changed, but every commit of mine has a new hash. The notes in `s31/` still name the old ones, so [`s31/old-to-new.tsv`](old-to-new.tsv) maps each old hash to its new one, and an old tag `vN` is `s31-vN` here.

On 2026-10-25 this branch's own history from `s31-v4.4` on was rewritten once more, to take out a few lines quoted from a Windows log in one check file, and force-pushed. The eight commits from `s31-v4.4` to `s31-v4.5` and those two tags have new hashes; their code is the same. The notes already name the new ones (`s31/BUILD-NOTES.md` 32.6).

## Sponsor This Project 💝

If this saved you some time, or you just like seeing Windows 95 on odd hardware, you can chip in:

**[💖 Sponsor on GitHub](https://github.com/sponsors/platima)**

## Acknowledgements 🙏

- [tiny386](https://github.com/hchunhui/tiny386) by hchunhui - the emulator itself, and the reason a 386 PC fits on an ESP32 at all.
- [Espressif's ESP-IDF](https://github.com/espressif/esp-idf) - the framework it all runs on.
- [SeaBIOS](https://www.seabios.org/) - the BIOS and VGA BIOS the PC boots from.
- [VMDisp9x](https://github.com/JHRobotics/vmdisp9x) by JHRobotics - the display driver that gets Windows 95 to 800x480.
- [tab5-pc](https://github.com/yejun/tab5-pc) by Yejun Yang - the fast interpreter in front of tiny386's own (v5.0, `i386_fast.h`, `s31/FAST-CPU.md`).
- [annoyedmilk's esp32-s31-linux](https://github.com/annoyedmilk/esp32-s31-linux) - the Linux the dual boot keeps.

## Licence 📄

- tiny386 is under the BSD 3-clause licence - see [LICENSE](../LICENSE) at the top of the tree. My changes to it are offered under the same licence, so they can go back upstream, and so are the notes and scripts in `s31/`.
- `i386_fast.h` (v5.0) is Yejun Yang's fast interpreter from tab5-pc, under the MIT licence (Copyright (c) 2026 Yejun Yang), with its parts built on tiny386 under tiny386's BSD 3-clause; both notices and the MIT text are in the file, and the changes made to it here are under the same terms. [`s31/THIRD_PARTY.md`](THIRD_PARTY.md) has the notice and what was changed.
- The VGA BIOS in the releases is SeaBIOS's, under the GNU LGPL version 3, and its source goes with it.
- Windows 95 and its display driver aren't part of this branch or its releases - bring your own. `TEST-PLAN.md` says where to get the driver.

## Author 👨‍💻

**Platima** - [GitHub Profile](https://github.com/platima)
