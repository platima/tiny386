# Credits and third-party code on this branch

tiny386 itself is Chunhui He's (hchunhui, github.com/hchunhui/tiny386), under the BSD 3-clause
licence (`LICENSE` at the top of the tree), and the changes made to it on this branch are offered
under the same licence, as are the notes and scripts in `s31/`. This note thanks the projects and
people whose work the branch uses, then gives, for each piece of code here or in the images that
came from elsewhere, what it is, its licence, what was changed and the notice that goes with it.
Written for v5.2 (2026-11-10); the first version, for v5.0 (2026-10-27), covered tab5-pc's
interpreter only.

Below, "code" is text copied or adapted, which brings its licence's terms with it; "reference" is
figures, register values, a method or an idea, where nothing was copied.


## Credits

### Code the branch carries, or its images are built from

* **Chunhui He**, for tiny386: the emulator, its ESP-IDF front end (where `esp/` began), its
  SeaBIOS patch and config (`seabios/`), and the front ends `korvo/main.c` is made from. v5.2 is
  tiny386's master at `a080c5d` with this branch's work on top.
* **Fabrice Bellard**, whose device models from QEMU and TinyEMU tiny386 carries: the VGA, the
  keyboard controller, the timer, the interrupt controller, IDE, the NE2000 and PCI.
  **Vassili Karpov (malc)**, for the AdLib's link to its synthesiser, the DMA controller and the
  Sound Blaster 16. **Joachim Henke**, for the PC speaker.
* **Tatsuyuki Satoh** and MAME's developers, for `fmopl.c`, the OPL2 synthesiser that gives the
  AdLib its sound.
* **Ben Hoyt**, for inih, the ini file parser (`ini.c`, `ini.h`).
* **Yejun Yang** (yejun), for tab5-pc's fast interpreter (`i386_fast.h`, since v5.0) and for the
  write gathering of his tiny386 PR #70 (`ide.c`, v5.2).
* **Marco Müller** (annoyedmilk), for esp32-s31-linux: the Korvo-1's panel timings and pins and
  the card slot's power pin (`esp/main/lcd_korvo1.c`, `esp/main/board_korvo1.h`), and the
  bootloader configuration and partition table the dual boot is built from (`s31/dualboot/`,
  `esp/partitions-korvo1-dualboot.csv`).
* **Espressif**, for ESP-IDF, the USB host library and HID class driver (espressif/usb,
  espressif/usb_host_hid), and the examples `usb_input.c` and `esp_main.c` began from; and the
  authors of the code ESP-IDF links into the images: FreeRTOS (Amazon), the SD/MMC driver from
  OpenBSD (Uwe Stuehler), TLSF (Matthew Conte), FatFs (ChaN) and picolibc.
* **Kevin O'Connor and the SeaBIOS contributors**, for SeaBIOS and SeaVGABIOS, which the PC boots
  from (`bios.bin` and `vgabios.bin` on the card).
* The **fntcol16** collection (packaged by Joseph Gil, the fonts public domain), for the 8x16
  font in `vgafont.c`, as SeaBIOS carries it: the guest's text modes, the boot console and the
  crash screen.

### Upstream contributors whose work the branch has, through tiny386

These came in with tiny386's master and are under its licence; their git authorship is kept.

* **Yejun Yang** (git authors "M Y", "Yejun Yang" and "yejun"), whose merged PRs include #40 to
  #42: task switches that load the new task's flags and push error codes, with the task gate's
  error code checked at the new task's CPL (`0642735`, `e2f9e9e`); the i8254's one-shot modes
  (`7989c8f`, `16a060a`, `4ce2e06`); near JMP and Jcc displacements by operand size (`d81e225`);
  FILD and FISTP m64 exact (`79ac877`); 64-bit writes to the linear frame buffer (`96fa0da`);
  `vga=` for linuxstart (`003c266`); AAM 0 raising a divide error (`81823e2`); a real-mode
  interrupt changing SP only (`6bd2794`); the ninth column for C0h to DFh only (`b9e4af5`); the
  Sound Blaster reading only what fits (`ba74ccb`); the DMA controller's decrement order
  (`4808f17`); and the optional fast path `fast/` (`2d2e124`, PR #56), which is in the tree but
  not built by this branch's firmware.
* **UKTailwind** (git author "peter"): the mouse reporting at its sample rate, one packet at a
  time (`606b4e7`, PR #63), and IRETD in real mode (`61bfd3e`, PR #66), merged here at `cb72b00`.
* **cmeyer** (BigFishDev22): the USB hub, keyboard and mouse (`d623bd9`, `85c1110`, PR #4).
  `esp/main/usb_hid.c` is his, unchanged, and 45 lines of `esp/main/usb_input.c` still are.
* **CopperBucket**: the per-board ESP build (`585865c`, PR #16). The `-DBOARD` block of
  `esp/CMakeLists.txt`, which copies `sdkconfig.${BOARD}` into place at each build, is his, kept
  and extended (`PARTITION_CSV`); both boards here build through it.

### Work measured, read or learnt from (references; nothing copied)

* **Yejun Yang**: tab5-pc's translator (`t386_jit.h`, `t386_fpu_jit.h`) was built and measured
  on 2026-10-24 and not taken. The page-crossing accesses of `a67bb47` (`CROSSES_PAGE` in
  `i386.c`) follow his tiny386 PR #49, which tab5-pc carries.
* **Jackson Hu** (huming2207), for esp_mem_benchmark (github.com/huming2207/esp_mem_benchmark):
  its ESP32-S31 figures are the quiet baseline that the v5.2 PSRAM study and
  `esp/main/membench.c` read their lines against, and membench follows its method (32-bit
  kernels run from internal RAM, lines made cold by a cache write-back and invalidate, the cycle
  counter, medians, async memcpy in 64 KiB pieces). membench's code is this branch's own.
  esp_mem_benchmark states no licence, so none of its code is copied.
* **Marco Müller**: the Linux route (tiny386 as a Linux program, `korvo/`) ran on his
  esp32-s31-linux, built with the cross compiler of its Buildroot tree, and his code was the
  reference for the board's memory, consoles and card layout (`s31/newcard/`). The flash scripts
  know his be5b725 images and put them back. None of his Wi-Fi or LCD code was taken: the network
  build uses ESP-IDF's Wi-Fi with tiny386's own `wifi.c` and `ne2000.c`.
* **Espressif's ESP-IDF sources**, read for this branch's own text: the hardware loop test
  (`membench_pie.S`), the cache registers and counters (`membench.c`), the SD host driver
  (`sdmmc_clk_wrap.c`), the panic handler (`crash.c`) and the bus monitor's PC record
  (`profile.c`).
* **The MS7210 sources** behind `esp/main/ms7210.c`, the DOS board's HDMI encoder: MacroSilicon's
  MS7210 SDK V1.0.0 as published in D-Robotics' RDK X5 kernel (github.com/D-Robotics/x5-kernel),
  Loongson's LoongGPU bridge driver, Meyesemi's table in OurEDA-Pango-Remote-Lab-FPGA, and
  avmwww/ms7210-linux-driver. The register sequence was compiled from the SDK and checked against
  the Loongson and Meyesemi tables: register values only, no code text.
* **The CH9350L references** behind `esp/main/ch9350.c`, the DOS board's USB input: WCH's
  CH9350L data sheet V2.6, Zephyr's `input_ch9350l.c`, sjmf/kvm-serial, and hoboNicolaLibrary
  and ESP32_CH9350_KEY (read only; the parser was written fresh).
* **QEMU**, whose answers (7.2, `-cpu 486`) the REP wrap and CMPXCHG host tests check against.
* **The JIT and recompiler projects** surveyed in the JIT assessment of 2026-10-22 (tiny386-idf's
  research notes, not on this branch, with each one's URL and commit): QEMU's TCG, DOSBox's
  core_dynrec, DOSBox-X, DOSBox Staging, 86Box, v86, Box64, Bochs (Mihocka and Shwartsman's
  paper), xtensa-68000-dynarec, retro-go-tab5's gpSP back end, and esp32-p4-jit with boulabiar's
  fork of it. Designs and published figures only.
* **Data sheets and specifications**: Texas Instruments (PCM5102A, TPA2012D2), USB-IF (HID 1.11),
  Intel (the 80386 Programmer's Reference), Espressif (the Korvo-1 schematic and user guide, the
  ESP32-S31 data sheet, esp-board-manager's board file), Worldsemi and Inolux (WS2812B,
  IN-PI554FCH), VESA (DMT) and Bochs (the VBE DISPI registers).

### Tools and guest software

* **QEMU**: qemu-riscv32 to count the board compiler's instructions, qemu-system-i386 for the
  upstream checks and to show the SeaBIOS mode 07h faults.
* **GCC and binutils**; **Espressif's** esptool, esp-rom-elfs and RISC-V toolchain; **mtools**
  and **dosfstools**; **Python**; the **GCC sanitizers** (AddressSanitizer, UBSan,
  ThreadSanitizer); **Valgrind**; **gcov**; **NASM**; **MinGW-w64**; **KiCad** and
  **Freerouting** for the DOS board; **Buildroot**, through esp32-s31-linux's tree, for the Linux
  route's cross compiler.
* **VMDisp9x** by Jaroslav Hensl (JHRobotics), built on Michal Necasek's VirtualBox driver with
  Philip Kelley's QEMU changes: the display driver that takes Windows 95 to 800x480.
* **FreeDOS 1.4**, the guest of the card's default `tiny386.ini` and of the lockstep bench.
* **SDL 1.2.15** (Sam Lantinga and the SDL project), which `korvo/` links when built for Linux.


## tiny386

**What.** The whole emulator, the base of the branch. v5.2 changes 29 of upstream's files and
adds the rest of the tree; the ESP images are built from upstream's ESP-IDF front end as changed
here.

**Licence.** BSD 3-clause, Copyright (c) 2024-2025, Chunhui He (`LICENSE`). Its second condition
asks that a binary package reproduce the notice, the conditions and the disclaimer in its
documentation, so `LICENSE` goes with every package (the last section).


## Device models from QEMU and TinyEMU (MIT)

**What.** Files tiny386 carries, each with its own MIT notice, all built into every image:

* Copyright (c) Fabrice Bellard: `vga.c` (2003-2017), `i8042.c` (2003), `i8254.c` (2003-2004),
  `i8259.c` (2003-2004), `ide.c` and `ide.h` (2003-2016), `ne2000.c` (2003-2004), `pci.c` (2017);
* Copyright (c) Vassili Karpov (malc): `adlib.c` (2004-2005), `i8257.c` (2003-2004), `sb16.c`
  (2003-2005);
* Copyright (c) 2006 Joachim Henke: `pcspk.c`.

**What was changed here.** `vga.c`, `i8042.c`, `i8254.c`, `i8257.c`, `ide.c`, `ide.h`, `sb16.c`,
`pcspk.c` and `adlib.c` (`BUILD-NOTES.md` has each change); `i8259.c`, `ne2000.c` and `pci.c` are
as upstream has them. Every header is kept.

**The notice**, as each file's header gives it after its copyright line:

```
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```


## fmopl.c, the OPL2 synthesiser (LGPL 2.1 or later)

**What.** `fmopl.c`, the FM synthesiser behind the AdLib: a fork of MAME's file, relicensed under
the LGPL, as QEMU and tiny386 carry it. Unchanged here, and compiled and statically linked into
every image (`USE_FMOPL` in `esp/main/CMakeLists.txt`). Upstream's other choice since `a080c5d`,
Nuked OPL3 (nukeykt, `USE_NUKED_OPL3`), is not built here; it is under the LGPL 2.1 as well.

**Licence.** GNU Lesser General Public License, version 2.1 or any later version. Its header:

```
Copyright (C) 1999,2000 Tatsuyuki Satoh , MultiArcadeMachineEmurator development

This version of fmopl.c is a fork of the MAME one, relicensed under the LGPL.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.
```

**What a package needs** (section 6 of the LGPL 2.1, for a library linked into a program): this
notice that the images use `fmopl.c` under the LGPL, a copy of the LGPL 2.1, and the complete
source with the means to build the images again. For a package published as a release of this
repository, that source is the release's tag, and `s31/README.md` says how to build it; a package
handed out any other way carries the source or a written offer of it.


## inih (BSD 3-clause)

**What.** `ini.c` and `ini.h`, Ben Hoyt's ini file parser (github.com/benhoyt/inih), which reads
the card's ini files. Unchanged from upstream, built into every image. The files' own headers say
"Copyright (C) 2009-2020, Ben Hoyt" and point at inih's `LICENSE.txt`, which reads:

```
The "inih" library is distributed under the New BSD license:

Copyright (c) 2009, Ben Hoyt
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Ben Hoyt nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY BEN HOYT ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL BEN HOYT BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```


## The 8x16 font (public domain)

**What.** `vgafont.c`, tiny386's 8x16 font of 256 characters (code page 437), drawn by `vga.c`
for the guest's text modes and by this branch's boot console and crash screen
(`esp/main/console.c`). The file has no header. Its 4,096 bytes are, byte for byte, the
`vgafont16` of SeaBIOS's `vgasrc/vgafonts.c`, whose header gives the fonts as coming from the
fntcol16 collection (the package by Joseph Gil) and says the individual fonts are public domain.

**Licence.** Public domain; nothing has to go with it.


## tab5-pc's fast interpreter (v5.0)

**What.** `i386_fast.h` is Yejun Yang's fast interpreter from tab5-pc, his port of tiny386 to the
M5Stack Tab5 (github.com/yejun/tab5-pc). It was taken from `firmware/tab5_386/src/t386_fast.h` at
commit `98a442e` (the commit `research\JIT-OPTIONS-2026-10-24.md` measured; release 1.0.0 is of
2026-10-22), with the `cpui386_step()` of that file folded into `i386.c`'s own and the hook of his
patch 0010 in `tlb_clear()`. tab5-pc's translator (`t386_jit.h`, `t386_fpu_jit.h`), its other
patches and its own paths into video memory were not taken.

**Licence.** tab5-pc's own code is under the MIT licence, Copyright (c) 2026 Yejun Yang. The parts
of `t386_fast.h` built on tiny386's internal definitions, and the `cpui386_step()` adapted from
tiny386's, are Copyright (c) 2024-2025 Chunhui He, under tiny386's BSD 3-clause licence (tab5-pc's
`LICENSE` and the file's own header say both). `i386_fast.h` keeps both notices and the MIT text at
its top. The changes made to it on this branch are offered under the same terms.

**What was changed here**, so that the guest sees exactly what tiny386 alone shows it
(`FAST-CPU.md` 3 has each, `BUILD-NOTES.md` 33 the v5.0 integration):

* the count of instructions kept exact at every port and MMIO access and at every hand-over to
  tiny386, and each step ended where tiny386's `cpu_exec1()` ends it;
* tiny386's CMP and JZ fusion done where tiny386 does it, and instruction bytes read only from the
  page in tiny386's own fetch cache;
* its TLB kept as a subset of tiny386's, with the dirty bit set on every store;
* video memory and other MMIO through tiny386's own `load8()` to `store32()`, so the dirty marks of
  this branch's drawing are kept, and REP STOS and REP MOVS into video memory through tiny386's
  v5.0 loops (`lfb_stos()`, `lfb_movs()`, the VGA window's own calls);
* wrapping 16-bit stack slots, INT's ESP, BT with an immediate beyond the operand, ARPL's write and
  failed segment loads done as tiny386 does them.

v5.1 and v5.2 changed it further for speed (`BUILD-NOTES.md` 34 and 35 have each change and its
checks).

**The MIT licence, as tab5-pc gives it:**

```
MIT License

Copyright (c) 2026 Yejun Yang

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```


## Write gathering from tiny386 PR #70 (v5.2)

**What.** The gathering of a write command's blocks in `ide.c` (`IDE_ENABLE_WRITE_GATHER`) is
Yejun Yang's, from his tiny386 pull request #70 (github.com/hchunhui/tiny386/pull/70, opened on
2026-11-07 and not merged at the time of writing). His commits `b4c0bd5` and `2b84ed6` were taken
as they stood, in `1363b51` (2026-11-08), so that a later merge of upstream has little to join.

**What was changed here** (`85d656c`, `18dfc7c`, `545aa1d`; `BUILD-NOTES.md` 35.8): the buffer
taken from PSRAM; the gathered blocks of a command the guest leaves part way written at a FLUSH
CACHE on any drive, at the 2 s quiet flush and on leaving the PC; and the gathering turned on for
both boards (`esp/main/board_korvo1.h`, `esp/main/board_dosboard.h`). The read-ahead beside it
(`IDE_ENABLE_READ_AHEAD`) and `[esp] ide_buffer` are this branch's own.

**Licence.** It was offered to tiny386, and is taken here on tiny386's terms (BSD 3-clause), as
the rest of tiny386's contributions are; `ide.c` itself is Fabrice Bellard's, under the MIT notice
above. No further notice is needed.


## Other contributors' code in tiny386

The upstream work named under Credits (Yejun Yang's merged PRs, UKTailwind's, cmeyer's and
CopperBucket's) came in with tiny386's master and is under tiny386's licence; no further notice
is needed. `esp/main/usb_input.c` began as Espressif's HID host example (Unlicense OR CC0-1.0),
through upstream and PR #4; `esp/main/usb_hid.c` is PR #4's, unchanged.


## esp32-s31-linux's panel values and dual-boot configuration

**What.** From Marco Müller's esp32-s31-linux (github.com/annoyedmilk/esp32-s31-linux) at
`be5b725`:

* the Korvo-1's RGB panel settings in `bootloader/main/display.c` (an 18 MHz clock, porches and
  pulse widths HSYNC 40/40/48 and VSYNC 23/32/13, PCLK on the falling edge, and the 21 pins), and
  the card slot's power switch on GPIO39 in `bootloader/main/sd.c`. Values only, typed again into
  this branch's own code on ESP-IDF's `esp_lcd_rgb_panel_config_t` (`panel_config()` in
  `esp/main/lcd_korvo1.c`; `esp/main/board_korvo1.h`), both of which name the source;
* for the dual boot, a copy of his `bootloader/` project: his `sdkconfig.defaults` kept whole with
  nine lines added (four of comment and five settings for the test-app pin, GPIO42), as
  `s31/dualboot/build/sdkconfig.defaults.dualboot`; the `sdkconfig` his build generated, kept as
  `sdkconfig.linux-be5b725` beside the changed `sdkconfig.dualboot`; and his partition table's
  seven entries, byte for byte, with tiny386's added (`esp/partitions-korvo1-dualboot.csv`,
  `s31/dualboot/`). The dual-boot `bootloader.bin` in the image sets is ESP-IDF's second-stage
  bootloader built with that configuration; his Linux loader (`bootloader/main`) is not in it.
  `s31/dualboot/DUALBOOT.md` 3.1 and `s31/dualboot/build/provenance.txt` record where each came
  from.

**Licence.** His README gives `bootloader/` as BSD 2-clause, and the build system, scripts and
rootfs files as MIT (Copyright (c) 2026 Marco Müller). Pin numbers and timings are facts about the
board. For the copied configuration text and table: his bootloader files carry an author line and
`SPDX-License-Identifier: BSD-2-Clause` but no copyright line, so the notice here names him as
their author.

```
esp32-s31-linux, bootloader/ (github.com/annoyedmilk/esp32-s31-linux), by Marco Müller
SPDX-License-Identifier: BSD-2-Clause

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```


## ESP-IDF, the code it links in, and the USB components

**What.** ESP-IDF 188e3e55bb (v6.1-dev-8142, github.com/espressif/esp-idf) is linked into every
image: its drivers, FreeRTOS, FatFs, the TLSF heap, the C library (picolibc) and, in every image
set, the second-stage bootloader. Up to v5.2 it was 188e3e55bb unchanged. Since v5.3 it has two of
ESP-IDF's own later fixes on top and is otherwise unchanged: e00861c3f497 (`sd_trans_sdmmc.c`)
and 007945125db7 (`sdmmc_cmd.c`), both on ESP-IDF's master and under its licence, whose patches
as GitHub gives them are in `s31/esp-idf/` (`README.md`, "Building"). The dual-boot set's
bootloader (`s31/dualboot/`) was built from 188e3e55bb unchanged; neither fix touches the
bootloader. The Korvo-1's images also link espressif/usb 1.6.0
and espressif/usb_host_hid 1.2.0 (`esp/dependencies.lock`), which the component manager fetches;
the S31 builds take no other managed component. `esp/main/usb_input.c` began as Espressif's HID
host example and `esp/main/esp_main.c` as the hello world example, whose CC0 header it keeps.

**Licences and notices.**

* ESP-IDF's own code, the two USB components and the bootloader: Apache License 2.0. A package
  carries a copy of the licence (ESP-IDF's `LICENSE`); ESP-IDF has no NOTICE file.
* FreeRTOS: MIT, "Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved."
  (its `tasks.c`), with the MIT text given for the device models.
* The SD/MMC driver, derived from OpenBSD's, as ESP-IDF's `components/sdmmc/sdmmc_sd.c` gives its
  notice (less the author's address):

  ```
  Copyright (c) 2006 Uwe Stuehler
  Adaptations to ESP-IDF Copyright (c) 2016-2024 Espressif Systems (Shanghai) PTE LTD

  Permission to use, copy, modify, and distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
  ```

* TLSF: "SPDX-FileCopyrightText: 2006-2016 Matthew Conte", BSD 3-clause; the conditions and the
  disclaimer are word for word those of tiny386's `LICENSE`, with his name in the copyright line.
* picolibc: BSD-style licences of many holders, collected in ESP-IDF's
  `components/esp_libc/COPYING.picolibc`, which a package carries.
* FatFs (ChaN): a BSD-style licence whose one condition is on source; nothing for binaries.
* The examples: Unlicense OR CC0-1.0, or CC0-1.0; nothing has to go with them.


## SeaBIOS and SeaVGABIOS (LGPL 3)

**What.** `bios.bin` and `vgabios.bin` on the card (`bios` and `vga_bios` in the inis) are
SeaBIOS and SeaVGABIOS (Kevin O'Connor and the SeaBIOS contributors, github.com/coreboot/seabios)
at `81ec9ec0` (rel-1.17.0 and twenty commits), built with tiny386's patch and config (`seabios/`
here), the Linux route's `seabios-korvo.patch` and, in the VGA BIOS only,
`seabios-korvo-800x480.patch` (five VBE modes at 800x480). SeaBIOS also boots the guests of the
host tests.

**Licence.** GNU LGPL version 3. The source as built, the patches, the build script and a
`README.txt` on building the two files again make up the release package's `bios\` folder
(`seabios-korvo-v4-src.tar.xz` holds SeaBIOS's `COPYING` and `COPYING.LESSER`); it goes with
every package that carries the two files.


## Code in the tree that the images do not build

Carried from upstream with their own headers, and in no image of this branch: `rawdraw/` (Charles
Lohr; MIT/x11 or NewBSD), `osd/` (rxi's microui; MIT), `linuxstart/` (Fabrice Bellard), `sdl/`,
`wasm/`, `fast/` (Yejun Yang's optional fast path, PR #56), and other boards' files in `esp/main/`:
`lcd_st7701.c` (Apache 2.0, based on csvke's esp32_p4_jc4880p433c_bsp), `lcd_axs15231b.c`,
`lcd_elecrow7s3.c` and `sdmmc_crc_wrap.c` (the last two CopperBucket's). The four ESP files are in
the build's source list, but each compiles to nothing unless `USE_LCD_ST7701`,
`USE_LCD_AXS15231B` or `USE_LCD_RGB_ELECROW7S3` is defined, and neither board here defines one.


## What goes with a package

A package of this branch's images and card files carries, beside them:

* `LICENSE` (tiny386) and this file, which between them give the notices above: tiny386, the
  device models, inih, tab5-pc, esp32-s31-linux's bootloader configuration, FreeRTOS, the SD/MMC
  driver and TLSF;
* the Apache License 2.0 (ESP-IDF's `LICENSE`);
* the GNU LGPL 2.1, for `fmopl.c`, with the line on where its source is;
* picolibc's `COPYING.picolibc`;
* the `bios\` folder, wherever `bios.bin` and `vgabios.bin` go.


## Elsewhere

* **The network build** (branch `v52-net`) also links lwIP (BSD 3-clause, Swedish Institute of
  Computer Science), wpa_supplicant (BSD, Jouni Malinen and contributors), FreeBSD's net80211
  (BSD, Sam Leffler and Errno Consulting), Mbed TLS (Apache 2.0, ARM) and Espressif's Wi-Fi and
  PHY libraries (Apache 2.0); a package of it needs their notices too. The two boards of this
  branch build no Wi-Fi.
* **`korvo/`**, the Linux route's front end, links SDL 1.2.15 (LGPL 2.1 or later) when built for
  Linux. It is not part of this branch's images; the Linux route's release (ESP32-Games
  v2026.10.24) carries SDL's source.
* **The Linux the dual boot keeps** is esp32-s31-linux's own (MIT, with `linux/` under the GPL 2
  and `bootloader/` under the BSD 2-clause licence), and not part of this branch.
* **Windows 95 and its display driver** (VMDisp9x, MIT) are not part of this branch or its
  releases; `s31/qemumini-pointer/` changes one byte of the copy of the driver you bring.
* **FreeDOS 1.4** (the kernel and shell under the GPL 2 or later, the rest under other free
  licences) is the guest of the card's default `tiny386.ini`, as `freedos-hd.img`, made by the
  Linux route's script; it is in no package of this branch.
