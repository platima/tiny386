s31/bios: how the cards' bios.bin is built
==========================================

s31/card/tiny386/bios.bin is SeaBIOS built here, for the S31 boards'
tiny386.  Until v5.3 the cards had the Linux route's bios.bin
(esp32-s31-linux-release/tiny386/bios.bin, sha256 be36f0d3...); v5.3's is
the same source with one change more, in this tree's seabios/patch:
mathcp_setup() puts the maths coprocessor in the BIOS's equipment word
(INT 11h, 0040:0010) when bit 1 of CMOS 14h is set, which the emulator
does with [cpu] fpu = 1 (pc.c).  With fpu = 0 the guest sees what it saw
before.

    bash s31/bios/build-bios.sh WORKDIR

builds it into WORKDIR/out: bios.bin, vgabios.bin (the same bytes as the
Linux route's) and seabios-korvo-src.tar.xz, the source as it was built.
It fetches SeaBIOS from GitHub and Ubuntu 26.04's gcc 15.2 and binutils
2.46 from archive.ubuntu.com (22 packages, 47 MB, each checked against the
checksum in the script), and needs no root.  The comment at its top says
what it needs and how it differs from the Linux route's
tools/build-bios.sh.

The source of bios.bin:

  SeaBIOS        https://github.com/coreboot/seabios, commit
                 81ec9ec0bcf45df11fb7f98339ec9036b546fca0 (twenty commits
                 after release 1.17.0)
  seabios/patch  tiny386's patch for it, with v5.3's change to
                 mathcp_setup()
  seabios/config tiny386's configuration
  seabios-korvo.patch
                 the Linux route's fixes, beside this file, unchanged
                 (esp32-s31-linux-release/tiny386/patches)

Its version string is rel-1.17.0-20-g81ec9ec0-dirty-20261111_000000-korvo
(the time and the host's name are fixed by the script); SeaBIOS prints it
in its first line.  The Linux route's says 20261019_000000.

What was checked (2026-11-11, v5.3):
* The Linux route's script and patches, unchanged, gave the Linux route's
  bios.bin and vgabios.bin byte for byte; so did this script with v5.2's
  seabios/patch and SB_STAMP_BIOS=20261019_000000.
* With v5.3's seabios/patch and that time, the only code that differs is
  in maininit(), where mathcp_setup() is inlined: the CMOS read and the
  OR into 0x410, and around them some of GCC's register choices elsewhere
  in the function (33 instructions out and 39 in, of 3,539).  Every other
  function of code16.o, code32seg.o and code32flat.o is the same but for
  its addresses.  With the time of
  20261111_000000 the file differs from that one only in the version
  string.
* The headless build with FreeDOS: INT 11h and 0040:0010 give 0026 with
  fpu = 1 and 0024 with fpu = 0, and FreeDOS boots both ways.

equ.S is EQU.COM, a DOS program that prints INT 11h, the word at
0040:0010, CMOS 14h and whether an x87 answers FNSTSW (its first lines say
how to build it).  On the board with fpu = 1 and this bios.bin it should
say int11=0026; with fpu = 0, or with the Linux route's bios.bin, 0024.
"EQU OFF" powers the PC off afterwards, for the headless build.

SeaBIOS is under the GNU LGPL version 3, and so are both patches.
Whoever passes bios.bin on passes its source with it, or offers it: the
source tarball the script makes, or this folder with seabios/patch and
seabios/config.
