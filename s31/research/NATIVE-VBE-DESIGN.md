# Windows 95 at the panel's own resolution: the design

Written 2026-10-23, for Plat's request: "Whatever video bios you think is best - ideally if we
could get the resolution of the LCD itself, so it's not boxed to 4:3, that would be amazing. 256
colour minimum, 16 bit preferred, 24 bit amazing." Work folder: WSL Debian `~/v4-work/vbe-research`.
Nothing in the v3 tree (`~/tiny386-idf-scratch/v3/tiny386`) was changed; nothing was flashed, no
serial port was opened, nothing was committed. Every claim is marked [verified] (seen in a source
line, a disassembly, a build or a run named here), [inferred] or [unknown].


## 0. The decision in one screen

* **Driver: VMDisp9x by Jaroslav Hensl (JHRobotics), release v1.2025.0.119, its 2D driver for
  QEMU's standard VGA (`qemumini.drv` and `qemumini.vxd`, the INF's `[Qemu]` section).** It
  programs tiny386's Bochs VBE (DISPI) registers directly, offers 8, 16, 24 and 32 bits a pixel
  at any size from 640x480 up that the registry lists and video memory holds, is MIT licensed and
  maintained. Second choice: Philip Kelley's BOXV9X for QEMU, v1.1.0, the small driver VMDisp9x
  grew from. Section 3.
* **It already works on v3.9's emulator core.** In the PC harness, from fresh copies of
  `win95.img`, Windows 95 OSR2 with `qemumini` reached the desktop at 800x480 in 8, 16, 24 and
  32 bits a pixel, and at 800x600 in 16 bits with the DOS board's build: the desktop fills the
  panel, nothing boxed [verified: section 7].
* **Modes:** the Korvo-1 at 800x480, the DOS board at 800x600, both 16 bits a pixel by default,
  8, 24 and 32 offered. Video memory: `vga_mem_size = 768K` gives 800x480 in 16 bits and should
  give the guest 14M again (13M today with 1M); 32 bits needs 1536K and keeps 13M; the DOS board
  needs 960K or 1M for 16 bits. Section 4.1.
* **What must change before it is good on the board: the drawing.** v3.9 draws a VBE mode by
  working out every pixel of the screen at every picture, 10 to 23 instructions and one division
  a pixel whether anything changed or not: 3.8 to 8.8 million instructions a picture at 800x480,
  and 1.5 MB of PSRAM pulled through the 64 KiB cache the two cores share, every picture
  [verified: disassembly of v3.9's `tiny386.elf`; the cost in time inferred, section 1.5]. In the
  harness the screen changes in 2.5 to 16 rows a picture on average, and not at all in 32 to 89
  per cent of pictures [verified: section 7.3]. So:
  1. Phase 1 (the shared core, `vga.c` and `pc.c`, testable on the PC): mark which 1 KiB blocks
     of video memory the guest writes, convert only the rows they touch, with a converter for
     each depth that has no division in it; fix the bank register for sizes that are not a power
     of two; give reads of the linear frame buffer the fast path writes already have.
  2. Phase 2 (the board): in 16 bits at exactly the panel's size the guest's frame buffer is
     already the panel's RGB565, so the panel scans it out directly (ESP-IDF's RGB driver takes
     user frame buffers and switches between them); the drawing task only writes the changed rows
     back from the cache. No copy, no conversion.
  3. Phase 3 (the board, optional): 24 and 32 bits converted by the S31's pixel processing
     accelerator (PPA), which takes ARGB8888 and RGB888 in and gives RGB565 out, and can scale,
     instead of the CPU.
* **VGA BIOS:** stay with SeaVGABIOS rel-1.17.0 as now, and add 800x480 at 8, 15, 16, 24 and 32
  bits (modes 1A0h to 1A4h). The Windows driver does not need them (it never asks the BIOS), but
  DOS programs, VBEMP and the tests do. Five table entries, no growth of `vgabios.bin` in a
  Debian build; the BIOS keeps or drops each by video memory as it does all others [verified:
  section 2.3].
* **Plat can try it today on v3.9**, with the cautions in 3.3: it will be correct and slower than
  the standard VGA driver until phase 1 lands [inferred].


## 1. tiny386's VBE today (v3.9, d370a7a)

The DISPI registers, the PCI function and the paths into video memory (1.1 to 1.3) are the same
in upstream master 4ce2e06 [verified: upstream `vga.c`, `pc.c`]; the drawing (1.4, 1.5) is v3.9's
own.

### 1.1 The DISPI registers [verified: `vga.c` v3.9 lines 93 to 118, 784 to 910, 2428 to 2529]

Ports 1CEh (index) and 1CFh (data), `vbe_write()` and `vbe_read()`:

| Index | Register | What tiny386 does |
|-|-|-|
| 0 | ID | accepts B0C0h to B0C5h; `vga_init()` sets B0C5h |
| 1, 2 | XRES, YRES | stored; XRES rounded down to a multiple of 8, 0 made 8 (YRES 0 made 1); no upper limit, none against video memory (QEMU's limits are commented out) |
| 3 | BPP | 4, 8, 15, 16, 24, 32; anything else becomes 8; 15 takes 2 bytes a pixel |
| 4 | ENABLE | bit 0 on: VIRT_WIDTH/HEIGHT set from XRES/YRES, offsets 0, video memory cleared unless NOCLEARMEM (80h), only on the change from off to on; bit 5 the 8-bit DAC; GETCAPS (2h) makes XRES, YRES and BPP read back the maximums; LFB_ENABLED (40h) is ignored, the linear frame buffer is always there |
| 5 | BANK | `val &= (vga_ram_size >> 16) - 1`, then the 64 KiB window at A0000h starts at `val << 16` |
| 6, 7 | VIRT_WIDTH, VIRT_HEIGHT | VIRT_WIDTH sets the line length; VIRT_HEIGHT stored, unused |
| 8, 9 | X_OFFSET, Y_OFFSET | the display start, `vbe_start_addr = (x * bpp / 8 + y * line) / 4` |
| 0Ah | VIDEO_MEMORY_64K | `vga_ram_size >> 16` |

GETCAPS answers XRES = the screen's width and YRES = its height (800 and 480 on the Korvo-1,
800 and 600 on the DOS board) and BPP = 32 [verified: lines 2495 to 2513]. The PCI function is
1234:1111, class 0300h, with one memory BAR the size of video memory rounded up to a power of two
[verified: lines 3005 to 3017]; SeaBIOS put it at FEB00000h for 768K and 1M, FEA00000h for 1152K
and 1536K, FEBC0000h for 256K [verified: the runs' logs].

Two faults found here, both upstream's too [verified: upstream `vga.c` lines 1694 and 2192]:

* **BANK with a size that is not a power of two.** With `vga_mem_size = 768K` the mask is 11
  (1011b): banks 4 to 7 become 0 to 3, so a DOS program drawing 800x480 in 16 bits through the
  window puts rows 164 to 327 over rows 0 to 163. 1152K (mask 10001b) and 1536K (10111b) are hit
  too; 256K, 512K, 1M and 2M are not. The Windows drivers use the linear frame buffer and are not
  affected [inferred from the source].
* **VIDEO_MEMORY_64K truncates**: a `vga_mem_size` that is not a multiple of 64 KiB is reported
  smaller than it is. The ini should take multiples of 64K only.

### 1.2 How the guest reaches video memory [verified: `pc.c` lines 634 to 748, `i386.c` lines 801 to 913, 2812 to 2862]

* Every guest physical address in A0000h to BFFFFh or at E0000000h and above goes to the iomem
  callbacks (`in_iomem()`); `cpu_register_mem()`, which maps the frame buffer like RAM, is a no-op
  in the i386 core.
* The linear frame buffer: `iomem_write8/16/32()` store straight into `vga_mem` when the address
  is in the BAR; `rep movs` from RAM into it is one `memcpy()` a page (`iomem_write_string()`).
  Reads are slow: `iomem_read32()` is two `iomem_read16()`, each two `iomem_read8()`, each a call
  with its own checks.
* The 64 KiB window: `vga_mem_write/16/32/string()` with chain 4 (which `vbe_update_vgaregs()`
  turns on for every depth but 4), `addr += bank_offset`.
* So every write to video memory passes through one of eight functions (four for the LFB, four
  for the window) or the clear in `vbe_write()`: the places to note what changed.

### 1.3 Video memory and its limits

`pc_new()` takes `vga_mem_size + VGA_MEM_SLACK` (16 KiB) from the emulator's PSRAM pool
(`bigmalloc()`, 4 KiB aligned) [verified: `pc.c` 844 to 849, `esp_main.c` 361 to 374]. Nothing in
tiny386 stops a mode bigger than video memory: SeaVGABIOS leaves such modes out of its list (2.2)
and the Windows drivers refuse them (3.1). A line that starts past the end is not drawn, and one
that runs past it reads the slack; with the picture at most the screen's width that is at most
3,200 bytes, inside the 16 KiB [verified: `vga.c` 1681 to 1690].

### 1.4 What is drawn, and where [verified: `vga.c` 1410 to 1978, `board_korvo1.h`, `board_dosboard.h`]

* A VBE mode is drawn by `vga_graphic_refresh()` like every graphics mode: 8 bits through the
  256-entry palette (6 or 8-bit DAC), 15 and 16 bits as RGB555 and RGB565, 24 and 32 bits as
  B, G, R bytes, all into the panel's RGB565 frame buffer, which the VGA code draws into directly
  (`LCD_ZERO_COPY`), with a 16-bit store only where the pixel differs (`VGA_STORE_CHANGED`) and
  the changed rows written back from the cache (`VGA_REDRAW_DIRTY_ROWS`, `lcd_flush()`).
* A picture no larger than the screen is centred; at exactly the screen's size it is drawn 1:1
  from the top left. 800x480 on the Korvo-1 and 800x600 on the DOS board are drawn 1:1 today.
* A picture larger than the screen (`VGA_SHRINK_LARGE`: 800x600 on the Korvo-1) is scaled down to
  fit by `vga_graphic_refresh_shrunk()`, which works out each pixel with `vga_pixel()`.
* `vga_task` on core 0 calls `pc_vga_step()` every `LCD_FRAME_MS` (30 ms) on both boards while the
  emulator runs on core 1 [verified: `display.c`].

### 1.5 What a picture costs today

The inner loops of `vga_graphic_refresh()` in v3.9's Korvo-1 image (`images\korvo1-dualboot\
tiny386.elf`, SHA-256 a4b2e58c...), disassembled with ESP-IDF's `riscv32-esp-elf-objdump`
[verified: `~/v4-work/vbe-research/vga_graphic_refresh.dis`, loops at 4001bd08, 4001bdda,
4001be2a and 4001bae8]. The compiler moved the depth's `switch` out of the loop, but each pixel
still has a `div` for `x / xdiv` (which is 1 in every VBE mode):

| Depth | Instructions a pixel, unchanged (stored: 2 more) | One 800x480 picture | One 800x600 picture |
|-|-|-|-|
| 8 | 10, one a `div` | 3.84 M | 4.80 M |
| 15 | 18, one a `div` | 6.91 M | 8.64 M |
| 16 | 13, one a `div` | 4.99 M | 6.24 M |
| 24, 32 | 23, one a `div` | 8.83 M | 11.04 M |

Every picture costs that whether anything changed or not. At the 16 ns an instruction the Linux
route fitted to this chip (`tiny386\BUILD.txt`, "Speed"), 16 bits at 800x480 is 80 ms a
picture; at one instruction a cycle (3.1 ns at 320 MHz) 16 ms, plus whatever the 384,000
divisions cost [inferred; how long a division takes on this core is unknown]. Either way `vga_task` cannot keep to its 30 ms and runs all the time, and each picture
reads the 768,000 bytes of video memory and the 768,000 of the panel's frame buffer (for the
comparison) through the data cache the emulator's core depends on [inferred]. The picture larger
than the screen is far worse: `vga_graphic_refresh_shrunk()` calls libgcc's 64-bit `__divdi3`
for every pixel [verified: the calls at 4001b2e6, 4001b39e, 4001b576 and 4001b5c4], tens of
millions of instructions a picture [inferred].


## 2. SeaVGABIOS's VBE

### 2.1 Which BIOS

The board runs SeaBIOS and SeaVGABIOS rel-1.17.0 (81ec9ec0) built from source with tiny386's
patch and `patches\seabios-korvo.patch` (`tiny386\BUILD.txt`, "The BIOS"); `vgabios.bin` is
40,448 bytes [verified: `card\tiny386\vgabios.bin`]. Its VBE back end for this hardware is
`vgasrc/bochsvga.c` (`CONFIG_VGA_BOCHS`), VBE 3.0 with a linear frame buffer, 8-bit DAC, display
start, logical line length, save and restore; no protected mode interface (4F0Ah) [verified:
`seabios-korvo-src.tar.xz`, `.config`, `vbe.c`]. There is nothing better to move to: the other
VGA BIOSes for this interface (the old LGPL VGABios, Bochs's) have fewer modes and none of the
fixes in `seabios-korvo.patch` [inferred], and the Windows drivers chosen here do not call the
BIOS to set their modes at all (3.1). So the decision is to keep SeaVGABIOS and extend its mode
table.

### 2.2 The mode list and the "Removing mode" lines [verified: `bochsvga.c` 301 to 384, `svgamodes.c`]

`svga_modes[]` holds 82 modes, 100h to 19Eh. `bochsvga_setup()` reads VIDEO_MEMORY_64K, then with
GETCAPS the maximum XRES and BPP, and for each mode prints "Removing mode N" and drops it if it is
wider than the maximum XRES, deeper than the maximum BPP, or needs more than video memory
(`height * width * bytes a pixel`, times 4 for the planar modes). YRES is not checked. So on both
boards every mode wider than 800 goes (the GETCAPS width is the screen's), and with 1M of video
memory 800x600 in 8, 15 and 16 bits stays while 800x600 in 24 and 32 bits goes [verified: the
runs' logs]. On the Korvo-1 an 800x600 mode is then drawn shrunk (1.4).

800x600 is already there for the DOS board: 102h (16 colours), 103h (8), 113h (15), 114h (16),
115h (24), 143h (32).

### 2.3 Adding 800x480 [verified: `~/v4-work/vbe-research/svgamodes-800x480.diff`, the builds and the FreeDOS runs in `~/v4-work/vbe-research/h2`]

Five entries after 19Eh, in the same form as the others (8x16 characters, so 30 text rows):

    { 0x1a0, { MM_PACKED, 800,  480,  8,  8, 16, SEG_GRAPH } },
    { 0x1a1, { MM_DIRECT, 800,  480,  15, 8, 16, SEG_GRAPH } },
    { 0x1a2, { MM_DIRECT, 800,  480,  16, 8, 16, SEG_GRAPH } },
    { 0x1a3, { MM_DIRECT, 800,  480,  24, 8, 16, SEG_GRAPH } },
    { 0x1a4, { MM_DIRECT, 800,  480,  32, 8, 16, SEG_GRAPH } },

* Mode numbers above 100h other than the VESA 1.2 ones are the BIOS's own; programs are meant
  to look in the list (4F00h) and at 4F01h, and 1A0h to 1A4h are used by nothing else here.
* Built with Debian's gcc 12 from `seabios-korvo-src.tar.xz`, `vgabios.bin` is 40,960 bytes with
  and without them (the ROM is padded to 512 bytes; the 60 bytes fit in the padding). The release
  build uses tiny386's CI compiler (`tools/build-bios.sh`), whose output is 512 bytes shorter;
  it may grow by 512 bytes at most [inferred]. Limits: tiny386 loads the file at C0000h with no
  limit; upstream's own partition tables give `vgabios.bin` a 60 KiB flash partition; the board
  reads it from the card. 40 KiB is far inside all of them.
* With that BIOS, 768K and 1M keep 1A0h to 1A2h and drop 1A3h and 1A4h; 1536K keeps all five
  [verified: FreeDOS booted three times in the harness, `h2/runs/fd-*/log.txt`]. 1152K would keep
  1A3h too [arithmetic].
* The mode list buffer in 4F00h's info block has room for 110 modes; 82 plus 5 plus the standard
  VGA modes stays under it even before any are removed [inferred from `vbe.c` 56 to 73].
* For the DOS board nothing needs adding. Whether to also stop offering 800x600 on the Korvo-1
  (it can only be shown shrunk) is a choice; this design keeps it, and phase 1 makes the shrunk
  picture cheap (5.1).
* SeaBIOS is LGPL 3: the patch, the rebuilt source tarball and `tools/build-bios.sh` go with the
  two files, as now.


## 3. Windows 9x display drivers for this hardware

### 3.1 The candidates [verified unless marked: their source, INFs and READMEs as fetched 2026-10-23 into `~/v4-work/vbe-research/drivers` and `downloads`]

| | VMDisp9x `qemumini` | BOXV9X for QEMU (Philip Kelley) | BOXV9X (Michal Necasek, original) | VBEMP 9x (bearwindows) |
|-|-|-|-|-|
| Where | github.com/JHRobotics/vmdisp9x, release v1.2025.0.119 (2025-07-29, repacked 2025-08-03), `vmdisp9x-1.2025.0.119b-driver-2d.zip`; source commits to 2026-10-05 | github.com/phkelley/boxv9x, release v1.1.0 (2023-01-25), `boxv9x_qemu.zip` | os2museum.com (2022-06), source mirrored at github.com/volkertb/boxv9x | bearwindows.zcm.com.au/vbe9x.htm, `191201.zip` (driver 2019-12-01, archive repacked 2026-03-31) |
| Interface | DISPI registers directly, from its 32-bit mini-VDD; LFB from PCI BAR 0 | DISPI registers directly; LFB from the Configuration Manager's resources | DISPI, plus VirtualBox's own extensions | the VBE BIOS (int 10h 4F02h and friends), through V86 mode |
| Accepts tiny386 | yes: ID B0C0h to B0C6h, memory from VIDEO_MEMORY_64K, INF matches PCI\VEN_1234&DEV_1111 | yes: the same three changes were Kelley's QEMU port | **no**: accepts only IDs up to B0C4h (tiny386 says B0C5h), reads video memory with a 32-bit IN from 1CFh and the LFB address from VirtualBox's register 0Bh (tiny386 answers 0, so it would use E0000000h), INF matches only 80EE:BEEF | yes, as any VBE 2.0 card with an LFB (INF matches PCI\CC_0300) |
| Resolutions | any from 640x480 up (`FixModeInfo()`), up to 8192x5120, if one screen fits video memory (`pitch * height < vram`); the list is the registry's `MODES` keys from the INF | any from 640x480 to 5120x3840 that fits; the INF's list | as Kelley's | only what the VBE BIOS lists; its INF already names 800x480 at every depth |
| Depths | 8, 16, 24, 32 (15 only in its VESA variant) | 8, 16, 24, 32 | 8, 16, 24, 32 | 8, 15, 16, 24, 32 |
| Windows 95 | "Windows 95/98/Me"; users run it on 95 OSR 2.5 (SoftGPU issues 120, 179); needs DIBENG.DLL, which OSR2 has | 95, 98, ME | 95, 98, ME | "ALL of MS Windows 9x Family"; the author recommends the latest service pack |
| CPU | built for the Pentium Pro by default (`-6 -fp6`); floating point only in files the QEMU driver does not link, and debug printing [inferred: source]; tiny386 runs CMOV, CPUID, BSWAP, CMPXCHG and the rest whatever `gen` says (`i386ins.def`); it ran with `gen = 3`, `fpu = 0` (7.1) | Open Watcom | as Kelley's | [unknown] |
| Licence | MIT | MIT (the OS/2 Museum's) | MIT | freeware, closed; "Reverse engineering and modification are prohibited"; redistribution only unmodified and free |
| Size | 15,534 + 21,123 bytes | 7,596 + 4,803 bytes | | 18,432 + 5,317 bytes |
| Known problems on QEMU std VGA | SoftGPU issue 147 (reboot hang on QEMU std after 0.8.2025.51), "fixed" in SoftGPU 0.8.2025.53, released the same day as this driver [inferred: the same fix]; vmdisp9x issue 8 (Windows ME's Add New Hardware scan garbles the screen, open); DOS boxes handled by trapping ports 1CEh/1CFh in its mini-VDD | the DOS box crash of v1.0.0 fixed by v1.1.0's mini-VDD; unmaintained since | | the author lists slow window moves and scrolling, lockups and garbage when entering a DOS session, garbage text with some fonts |

Also in the VMDisp9x package and not for this board: `vesamini.drv`, its generic VBE BIOS driver,
draws into a copy of the screen in system memory and copies it to video memory on a timer
(`async_blit_init()` in `vxd_vesa.c`) [verified], a whole extra screen copy in the guest at two
million instructions a second; and `vmwsmini` (VMware SVGA), hardware tiny386 does not have.

### 3.2 The recommendation and why

**VMDisp9x v1.2025.0.119, `qemumini`.** It talks to the very interface tiny386 emulates, with no
BIOS call on the way; it takes any resolution the registry names, so 800x480 is four registry
lines; it covers 8 to 32 bits; it is MIT licensed, so its INF can be changed and the change
handed on; it is maintained, and its QEMU std VGA path has had its bugs found by users of a
better-tested emulator than this one. And it ran: Windows 95 OSR2 on tiny386 v3.9's core, at
every depth, at both boards' sizes (section 7) [verified on the PC].

**Second: BOXV9X for QEMU v1.1.0.** The same lineage and the same way of working, a third of the
size, no DirectDraw parts, no 32-bit mode setting; but unmaintained since January 2023. It is
what to try if VMDisp9x misbehaves on the board in a way the PC does not show [inferred].

Not chosen: Michal Necasek's original (it would not find tiny386's adapter, above); VBEMP 9x
(closed source with a licence that forbids changing it, every mode change through the emulated
BIOS in V86 mode, and the problems its author lists); VMDisp9x's `vesamini` (the timer copy).

### 3.3 Installing it

The release's files are third-party binaries: they are not put in this package. Plat gets them
from github.com/JHRobotics/vmdisp9x/releases/tag/v1.2025.0.119, file
`vmdisp9x-1.2025.0.119b-driver-2d.zip` (355,886 bytes, SHA-256 e0d698a6089347a6a619ed439c092f24
7392fbafac6b342f4651c1380911fb23 [verified: downloaded 2026-10-23]), or the same release's
1.44 MB floppy image `vmdisp9x-1.2025.0.119b-driver-2d.img`.

On the board (Windows 95 already installed):

1. Floppy image on the card beside the ini, and in the Windows ini under `[pc]`:
   `fda = vmdisp9x-1.2025.0.119b-driver-2d.img` (a name without a / is looked for in the ini's
   folder). `vga_mem_size` at least 768K for 800x480 in 16 bits (1M as now is fine).
2. In Windows: Control Panel, Display, Settings, Change Display Type, Adapter Type: Change, Have
   Disk, `A:\`, "QEMU STD VGA PCI Adapter", OK; restart when asked. The INF also copies the
   DirectDraw helpers `vmhal9x.dll`, `vmdisp9x.dll` and `vmhal486.dll`; nothing loads them unless
   a program uses DirectDraw [inferred from the INF].
3. The INF's mode list has no 800x480. Either edit `vmdisp9x.inf` before step 2 (copy A: to a
   folder on C:, add the four lines below to `[VM.AddReg]`, and point Have Disk there), or after
   step 2 add the keys with REGEDIT (`HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\Class\
   Display\0000\MODES\16\800,480` and so on: keys with no values):

       HKR,"MODES\8\800,480"
       HKR,"MODES\16\800,480"
       HKR,"MODES\24\800,480"
       HKR,"MODES\32\800,480"

   and, so that Windows never offers what can only be shown shrunk, take out the lines for every
   size above 800x600 (the ones for 1024x768 and up). On the DOS board the list already has
   800x600 and nothing needs adding.
4. The monitor: this image's monitor is "(Unknown Monitor)", whose key lists only 640,480
   [verified: the registry exported in run step1]. Windows 95 offers no desktop area larger than
   the monitor allows [inferred: Windows 95's documented behaviour], so: Change Display Type,
   Monitor Type: Change, "(Standard monitor types)", "Super VGA 800x600".
5. Settings: "Desktop area" 800 by 480 (Korvo-1) or 800 by 600 (DOS board), "Color palette"
   "High Color (16 bit)" (Windows' own words); restart.

On v3.9 this will look right (the PC runs drew it 1:1, 7.1) but every picture costs 4 to 9
million instructions (1.5), so Windows will very likely run slower than with the standard VGA
driver until phase 1 is in [inferred]; Ctrl+Alt+F12's speed line and the UART's `display:` line
(with `uart_log = 1`) will say by how much. To go back: Change Display Type, "Standard Display
Adapter (VGA)", or safe mode (F8), which always uses VGA.

For the PC harness the install is done offline, without the dialogs (section 6.2), which is how
the runs in section 7 were made.


## 4. The board side

### 4.1 Video memory, and what is left for the guest

Bytes one screen needs, the smallest `vga_mem_size` in whole 64 KiB that holds it with something
to spare (VMDisp9x wants `pitch * height < vram`, strictly; SeaVGABIOS `<=`), and what
`mem_size = max` gives. The pool is about 15,300 KiB on the Korvo-1 (NOTES-core2.md 3.4), and
Plat's 13M with 1M bounds it between 14,352 and 15,375 KiB; on the DOS board its frame buffer is
192,000 bytes bigger, so about 15,110 KiB [inferred]. The guest gets the whole megabytes of the
pool less `vga_mem_size` and 16 KiB of slack (`settle_memory()`) [verified: source]; a `mem_size`
in K (13824K, say) is kept as given when it fits, which can give more than "max" [inferred:
source; not tried with Windows].

| Board | Depth | One screen, bytes | `vga_mem_size` | `mem_size = max` | Mode kept by the BIOS |
|-|-|-|-|-|-|
| Korvo-1 800x480 | 8 | 384,000 | 384K | 14M | 1A0h |
| | 16 | 768,000 | 768K | 14M if the pool is 15,120 KiB or more, else 13M | 1A2h |
| | 24 | 1,152,000 | 1152K | 13M | 1A3h |
| | 32 | 1,536,000 | 1536K | 13M | 1A4h |
| DOS board 800x600 | 8 | 480,000 | 512K | 14M | 103h |
| | 16 | 960,000 | 960K or 1M | 13M | 114h |
| | 24 | 1,440,000 | 1408K | 13M | 115h |
| | 32 | 1,920,000 | 1920K | 12M | 143h |

[arithmetic; the guest's megabytes inferred from the pool sizes]. 768K, 1152K, 1408K, 1536K and
960K need the bank fix of 5.1 for DOS programs that draw through the window; until then 512K, 1M
and 2M are the safe sizes for them. 256K stays enough for Windows' standard VGA driver.

### 4.2 Drawing cost by depth, after phase 1 [inferred unless marked]

Instructions a pixel for a row that changed, for RV32 with the Zba and Zbb instructions the
compiler already uses (`sh1add`, `zext.h`, `bext`): estimates from writing the loops out, to be
counted in the built code (6.3):

| Depth | How | A pixel | A full 800x480 picture | Today, for comparison |
|-|-|-|-|-|
| 8 | four pixels from one 32-bit load, each through a 256-entry RGB565 table in internal RAM, two pixels a store | about 5 | 1.9 M | 3.84 M |
| 15 | two pixels a word: `(v & 0x001f001f) \| ((v & 0x7fe07fe0) << 1)` | about 3 | 1.2 M | 6.91 M |
| 16 | the row copied a word at a time | about 1 | 0.4 M | 4.99 M |
| 24 | four pixels from three words, each `(p >> 8 & 0xf800) \| (p >> 5 & 0x07e0) \| (p >> 3 & 0x001f)` | about 12 | 4.6 M | 8.83 M |
| 32 | one word a pixel, the same three masks | about 10 | 3.8 M | 8.83 M |

What matters more is how many rows change. In the harness, with the mouse moving every 50 ms,
Windows changed 15.4 to 15.8 rows a picture on average in 8, 16 and 32 bits, and with the mouse
still 7.9; standard VGA 11.5 and 2.5 [verified: 7.3]. At 16 rows, 12,800 pixels: about 13,000
instructions a picture in 16 bits and 130,000 in 32 bits, against today's 5 and 9 million. With
nothing changed, nothing but a look at the bitmap. Full-screen redraws (a window maximised,
scrolling) still cost the "full picture" column, once.

### 4.3 16 bits at exactly the panel's size: no conversion at all [verified: ESP-IDF v6.1-dev `components/esp_lcd/rgb/esp_lcd_panel_rgb.c`, `esp_lcd_panel_rgb.h`]

The guest's 16-bit frame buffer is little-endian RGB565, which is the panel's format byte for
byte [verified: `vga.c` draws 16 bits by copying the two bytes]. Three ways to use that:

1. **Copy the changed rows** into the panel's buffer (phase 1, both boards and the PC): 1,600
   bytes a row, about 400 loads and 400 stores, then the existing write-back.
2. **Let the panel scan the guest's buffer** (phase 2): `esp_lcd_rgb_panel_config_t` has
   `user_fbs[]` and `num_fbs` up to 3; the driver only checks the buffer is DMA-reachable (PSRAM
   is, `SOC_PSRAM_DMA_CAPABLE`), and `esp_lcd_panel_draw_bitmap()` given one of its own frame
   buffers makes it the current one rather than copying, and `rgb_panel_update_dma_link()` joins
   every buffer's DMA chain to it, so the DMA runs on into it after the frame it is in [the last
   step inferred from the link handling]. So: frame buffer 0 is today's
   (the console, every other mode, the crash screen), frame buffer 1 is the guest's video memory.
   While the guest shows 16 bits at exactly the panel's size, with VIRT_WIDTH equal to the width
   and the display start at 0, the panel shows buffer 1 and `vga_task` only writes back the rows
   the guest changed (`esp_cache_msync()` from cache to memory, as `lcd_flush()` does now). Any
   other mode, the boot console, Ctrl+Alt+F12: buffer 0 and a full redraw.
   * The guest's memory is taken after the panel starts, so the panel is started once more after
     `pc_new()` with both buffers user-provided (buffer 0 allocated by `lcd_init()` itself from
     the start, so that it survives); a blank frame at the switch to the PC [inferred].
   * The crash screen draws into whichever buffer is on show: both are RGB565 at the panel's
     size, so the crash path needs no DMA change from the panic handler [inferred].
   * Tearing as today: the panel reads while the guest writes.
   * Cache: the guest's writes from core 1 sit in the shared write-back cache until written back;
     writing back only the dirty rows each 30 ms is a small, bounded cost [inferred], and unlike
     a copy it evicts nothing.
   * The DOS board's bounce buffers, if used, copy from whichever buffer is current; nothing
     changes there [inferred from the driver].
3. Not chosen: the panel's own colour converter (below) or bounce buffers filled from the guest's
   memory by the CPU (a copy of the whole screen 35 to 60 times a second).

### 4.4 24 and 32 bits

The guest's pixels are B, G, R (24) and B, G, R, X (32) in memory [verified: `vga.c` and SeaVGABIOS
`vbe.c` 168 to 182]. They have to become RGB565, keeping the top 5, 6 and 5 bits; the CPU loops
of 4.2 do it, only on changed rows. The S31 also has hardware that does it [verified: ESP-IDF
headers; the speed on this chip unknown]:

* **The PPA** (`SOC_PPA_SUPPORTED`; `esp_driver_ppa`): a scale-rotate-mirror operation takes
  ARGB8888 (`ESP_COLOR_FOURCC_BGRA32`, the guest's 32-bit layout), RGB888 (`BGR24`, the 24-bit
  layout) or RGB565 in, gives RGB565 out, works on a block of rows, scales by sixteenths, and
  writes back and invalidates the cache around the operation itself (`ppa_srm.c` 314 to 329).
  One call per changed band of rows would take 24 and 32-bit conversion off the CPU entirely,
  and the same call with a scale of 0.8 would draw 800x600 on the Korvo-1's 800x480 (phase 3).
* **DMA2D** has an RGB888 to RGB565 conversion on its transmit channels.
* **The LCD controller itself** converts RGB888 input to RGB565 output on the S31
  (`LCD_LL_SUPPORT_RGB2RGB_CONV`), so the panel could scan a 24-bit guest buffer directly. Not
  chosen: the format belongs to the whole panel, not to one buffer, so the console, the other
  modes and the crash screen would all have to be 24-bit too, and the panel's DMA would read 1.5
  times the bytes from PSRAM.

24 and 32 bits also cost the guest: its DIB engine moves 1.5 or 2 times the bytes of 16 bits. In
the harness the boot to the desktop and 120 s after it took the same guest instructions at 8, 16
and 32 bits (within 2 per cent) and 6 per cent more at 24 bits, so this is small next to the
drawing [verified: 7.1, 7.3; `runs-summary.txt`]. 16 bits stays the default.

### 4.5 The DOS board

The same at 800x600: 960,000 bytes a screen, 1.25 times the Korvo-1's pixels. Its panel already
runs at 800x600, so Windows at 800x600 is native, 16 bits zero-copy in phase 2, and nothing is
ever shrunk (every mode wider than 800 is left out by the BIOS; the INF should list nothing above
800x600) [inferred]. Windows at 800x600 in 16 bits reached the desktop in the harness built with
`board_dosboard.h` [verified: 7.1].


## 5. The design

### 5.1 Phase 1: the shared core (`vga.c`, `pc.c`), upstreamable, testable on the PC

1. **Changed blocks.** A byte for each 1 KiB of video memory (1,024 bytes for 1M, from the pool
   with the video memory), set to 1 by every write path of 1.2: `iomem_write8/16/32()` and
   `iomem_write_string()` for the LFB, the chain-4 paths of `vga_mem_write*()` for the window,
   and the clear in `vbe_write()`. One shift and one store a write, on paths that already cost
   tens of instructions. The drawing (core 0) clears a block's byte before it reads the block,
   so a write that lands after the clear is drawn now or at the next picture, and none is lost;
   no atomic operation is needed, only that order and a compiler barrier [inferred; the
   one-writer, one-clearer pattern `VGA_TWO_CORES` already uses].
2. **Rows from blocks.** For the visible range, each block marked gives the rows it overlaps
   (`(block * 1024 - start) / line_offset`, divisions per block, not per pixel); those rows are
   converted. Everything is redrawn on a mode or depth change, a display start or line length
   change, a palette change in 8 bits, `full_update`, or a clear.
3. **Converters** for 8, 15, 16, 24 and 32 bits into RGB565 (4.2), one function each, no
   division, used for VBE pictures up to the screen's size; the old loop stays for the planar,
   CGA and panned cases.
4. **Shrunk pictures** (larger than the screen) through a table of source columns made once per
   size, and only rows whose source rows changed: no 64-bit division per pixel.
5. **BANK**: `bank_offset = (val % nbanks) << 16` (or ignore a bank past the end), so any
   multiple of 64 KiB works; the ini warns about a `vga_mem_size` that is not one.
6. **LFB reads**: `iomem_read16/32()` load straight from `vga_mem` within the BAR, as the writes
   do. Windows' DIB engine reads the screen to save what is under the cursor and for most raster
   operations [inferred].
7. **A hook for the front end** to say "I show the guest's buffer myself": while the condition of
   4.3 holds `vga.c` calls it with the base, size and changed rows instead of converting. The PC
   harness leaves it unset and so tests the copying path.

### 5.2 Phase 2: the board (`esp/main`)

* `lcd_korvo1.c`, `lcd_dosboard.c`: buffer 0 allocated by `lcd_init()` and passed as
  `user_fbs[0]`; after `pc_new()`, the panel started again with `user_fbs[1] = vga_mem`;
  `lcd_show(n)`; `lcd_flush_buf(buf, y, h)`.
* `display.c`: buffer 0 whenever the console owns the display; the hook of 5.1 item 7.
* `crash.c`, `console.c`: draw into the buffer on show.
* Fall back to the copy of phase 1 if the second start fails, with a line on the console.

### 5.3 Phase 3 (optional): the PPA for 24 and 32 bits and for shrinking

One SRM call per band of changed rows, blocking, from `vga_task`; the output rows of the panel's
buffer must meet the PPA driver's alignment (the cache line) [unknown: its exact rule on the S31;
whole rows of 1,600 bytes are 25 lines of 64 bytes]. Measure first (6.4); keep the CPU loops as
the fallback.

### 5.4 The VGA BIOS and the inis

* The five 800x480 modes (2.3), as a separate patch beside `patches\seabios-korvo.patch`,
  rebuilt with `tools/build-bios.sh`, with the source tarball.
* `win95-14m.ini` (to become a Windows ini for the native mode): `vga_mem_size = 768K` on the
  Korvo-1 with a comment for 1536K (32 bits), `1M` on the DOS board; `mem_size = max`.


## 6. Test plan for the next agent

All in WSL, from fresh copies of `win95.img` and `freedos-hd.img`, never the originals. The
harness used here is a copy of `~/win95-debug/fix/harness` in `~/v4-work/vbe-research/h/harness`
with three changes (`~/v4-work/vbe-research/harness-headless.diff`): `PCR_IGNORE_MODE=1` makes the
dialog and taskbar checks look at the picture whatever the BIOS mode byte says (a DISPI driver
never changes it, so the original harness took Windows at 800x480 for a text screen and never
pressed Enter); the screen is the board header's `LCD_WIDTH` by `LCD_HEIGHT` (so `BOARD=dosboard`
gives 800x600); and the rows changed by each periodic picture are counted (snapshots left out).

### 6.1 A DOS VBE test, pixel for pixel

A new `BVBE480.COM` in the style of `tiny386\tests\guest\bvbe.S` (which already draws through the
window, through the LFB with int 15h AH=87h, and says `@shot` on the debug port), built with
`as` and `ld` as that file's header shows, copied onto a copy of `freedos-hd.img` with `mcopy`,
started from its FDAUTO.BAT. For each of 1A0h to 1A4h (and 103h, 113h, 114h, 115h, 143h with the
DOS board's build):

1. 4F00h lists the mode exactly when it fits `vga_mem_size` (runs at 768K, 1152K, 1536K, 2M).
2. 4F01h: 800 by 480, the depth, bytes a line 800, 1600, 1600, 2400, 3200, the LFB bit and
   address (the BAR), the colour masks.
3. 4F02h with the LFB (BX = mode + 4000h): pattern A written through int 15h AH=87h (the
   `memcpy` path); `@shot`.
4. Then small writes in big real mode (FS with a 4 GB limit): single bytes, words, dwords and a
   `rep stosd` at known places (the 8, 16, 32-bit and string write paths); `@shot`. This is the
   test of the changed-blocks marking.
5. 4F02h without the LFB: pattern B through A000h with 4F05h, every bank including those past 3
   (catches the bank mask at 768K, 1152K, 1536K); `@shot`.
6. 4F07h, display start 16 lines down; `@shot`.
7. 8 bits: 4F08h BH=8, a palette by 4F09h, `@shot`; then only the palette changed, `@shot`.
8. Mode 3 back; the text screen read with the harness's `save_text()`.

Patterns that catch byte order, rows, columns and line length: in 24 and 32 bits R = x mod 256,
G = y mod 256, B = (x xor y) mod 256; in 16 bits (33x + 1031y) mod 65536; in 15 bits the same
mod 32768; in 8 bits (x + 2y) mod 256 through a known palette; first and last rows and columns
white. A Python script works out the expected RGB565 for each `@shot` (the top 5, 6, 5 bits; 15
bits' green doubled) and compares the harness's picture pixel for pixel (its PPM holds RGB565
widened exactly, so it can be narrowed back without loss).

The harness needs: on `@shot NAME` in the debug port output, the screen as the periodic drawing
leaves it (one more `vga_refresh()` with `full_update` 0, **not** `snapshot()`, which redraws
everything with `full_update` 1 and would hide a missed block), saved, then a key pressed; `@done`
to stop; and `-vgathread`, so that the drawing runs beside the emulator as on the board.

### 6.2 Windows 95 in the harness [done once in this investigation, section 7]

1. A fresh copy of the pristine image; `mcopy` `qemumini.drv` and `qemumini.vxd` from the release
   zip into `C:\WINDOWS\SYSTEM` and a `VBE.REG` into `C:\`; an `AUTOEXEC.BAT` that imports it with
   real-mode REGEDIT before Windows starts (`~/v4-work/vbe-research/s36.sh`, `mkreg.py`):

       @ECHO OFF
       C:
       CD \WINDOWS
       REGEDIT /L:C:\WINDOWS\SYSTEM.DAT /R:C:\WINDOWS\USER.DAT C:\VBE.REG
       CD \

   `VBE.REG` sets `Display\0000` as the INF's `[Qemu]` section would (`drv`, `minivdd`, `vdd`,
   `Mode`, `MODES\8|16|24|32\640,480|800,480|800,600`), the monitor's `MODES\800,600`, and
   `Config\0001\Display\Settings` `Resolution` and `BitsPerPixel`. Keep each REGEDIT line short:
   in run step1 a 154-character export line exported the whole `Class` key instead of
   `Class\Display` [verified], most likely cut at DOS's 127 characters [inferred].
2. Run with the DEBUG_VBE build (`SED_VGA` turning on `DEBUG_VBE` in `vga.c`): the log shows the
   driver's DISPI writes (XRES, YRES, BPP) as proof of the mode.
3. Outcome, pictures, rows changed a picture, guest instructions to the desktop, against the
   standard VGA driver on the same build, mouse still and moving (`-mouse 50`).
4. Still to do (needs a key script): the install through the dialogs as Plat will do it (Have
   Disk), and a DOS box round trip (Ctrl+Esc, R, "command", Enter, Alt+Enter, "exit") with a
   picture after the return to the desktop. Keys can be injected now with `-key T:SC` (set 1
   scan codes at virtual second T, 64 events at most); a file of steps, each waiting for a title
   bar or a text, would make the dialogs scriptable. Mouse clicks: `ps2_mouse_event()` with
   buttons.

### 6.3 Host tests

A test in `tests/host` like `tiny386\tests\vgatest.c`: random video memory, random writes
through each write path, the new drawing against the old per-pixel loop after every picture, at
every depth and size (smaller than, equal to and larger than the screen), with a second thread
drawing. And the instruction counts of the new loops from the built ELF (as in 1.5).

### 6.4 On the board (for Plat, after phase 1 or 2)

The `display:` UART line (pictures in 10 s, write-backs and their time) and BENCH or a Windows
timing with the standard VGA driver against 16 bits at 800x480; a picture of the desktop; a DOS
box; the crash test (`crash_test`) while in 800x480 16 bits, to see the crash screen on the
buffer on show; for phase 3, the PPA's time for a full 800x480 picture.


## 7. What the PC runs showed (this investigation)

Harness built from tag v3.9 (d370a7a), flavour esp, the board's compile flags, 64-bit; images
from `tiny386\win95\win95.img` (SHA-256 2871f472..., only copied); BIOS files from
`card\tiny386\`; `gen = 3`, `fpu = 0`, 13M; virtual clock at 2 million instructions a second;
Enter pressed on dialogs (`-dialogs 8`); stopped 120 virtual seconds after the taskbar was seen.
Runs in `~/v4-work/vbe-research/h/runs`, summary in `~/v4-work/vbe-research/runs-summary.txt`.

### 7.1 The driver at every depth [verified]

| Run | Screen, depth | `vga_mem_size` | DISPI writes in the log | Outcome |
|-|-|-|-|-|
| step2b-16-1M | 800x480, 16 | 1M | XRES 320h, YRES 1E0h, BPP 10h, ENABLE E1h | desktop at 223 s |
| d8-1M | 800x480, 8 | 1M | BPP 8 | desktop at 223 s |
| d16-768K | 800x480, 16 | 768K | BPP 10h | desktop at 221 s |
| d24-1152K | 800x480, 24 | 1152K | BPP 18h | desktop at 229 s |
| d32-1536K | 800x480, 32 | 1536K | BPP 20h | desktop at 223 s |
| dos16-1M (`BOARD=dosboard`) | 800x600, 16 | 1M | XRES 320h, YRES 258h, BPP 10h | desktop at 222 s |

ENABLE E1h is enabled, 8-bit DAC, LFB, no clear. The pictures (`final.png` in each run) show the
desktop and Internet Explorer 4's first-run setup filling the screen, the taskbar 800 pixels
wide, colours right at every depth.

### 7.2 Before the fix of the harness

The first 16-bit run (step2-16-1M) sat at the network logon box for 700 virtual seconds: the
harness's dialog check skipped the screen because the BIOS mode byte still said 03h. The picture
was right. Hence `PCR_IGNORE_MODE` (6).

### 7.3 What changes on the screen, and what it costs the guest [verified]

| Run | Driver | Mouse | Desktop at | Guest instructions, start to 120 s after the desktop | Pictures | Rows changed a picture | Pictures with no change |
|-|-|-|-|-|-|-|-|
| rows-vga-still | standard VGA, 640x480x4 | still | 211 s | 585.3 M | 10,981 | 2.5 | 88 % |
| rows-d16-still | qemumini 800x480x16, 768K | still | 222 s | 634.0 M | 11,343 | 7.9 | 89 % |
| rows-vga | standard VGA | every 50 ms | 228 s | 658.9 M | 11,551 | 11.5 | 51 % |
| rows-d8 | qemumini 800x480x8, 1M | every 50 ms | 303 s | 840.2 M | 14,038 | 15.4 | 32 % |
| rows-d16 | qemumini 800x480x16, 768K | every 50 ms | 297 s | 828.2 M | 13,839 | 15.8 | 32 % |
| rows-d32 | qemumini 800x480x32, 1536K | every 50 ms | 304 s | 842.2 M | 14,071 | 15.4 | 32 % |

"Rows changed" is the span of rows the drawing found different, as `VGA_REDRAW_DIRTY_ROWS`
reports it. Readings:

* Still, the new driver costs Windows about 8 per cent more guest instructions than the standard
  VGA driver over this run. The depth makes little difference: within 2 per cent between 8, 16
  and 32 bits here and in 7.1.
* With the mouse moving all the time, 26 to 28 per cent more, at every depth: the DIB engine's
  software cursor costs more guest work than the VGA driver's cursor [inferred: the cursor is
  the work that grows with the mouse]. The 2-pixel jiggle every 50 ms is harsher than real use.
* On the drawing side, an average picture touches 8 to 16 of 480 rows. Drawing only those makes
  the board's cost per picture tens of times smaller than today's full scan [inferred from the
  counts and 4.2].

### 7.4 The BIOS [verified]

The SeaVGABIOS with the five modes added (Debian gcc build, `h2/img/vgabios.bin`) booted FreeDOS
and removed 1A3h and 1A4h at 768K and 1M, none at 1536K (2.3).


## 8. What is not known

* The board: everything. In particular whether VMDisp9x behaves on the S31 as on the PC (it
  should, as the guest sees the same emulated hardware [inferred]), and how slow 16 bits at
  800x480 is on v3.9 before phase 1 [unknown].
* The latency of `div` on the S31's cores, the cache line size and how long `esp_cache_msync()`
  takes for a row; the PPA's and DMA2D's throughput on the S31 [unknown].
* Whether starting the RGB panel a second time with user buffers gives a clean picture on both
  boards; whether `esp_lcd_panel_del()` keeps a user buffer (the driver keeps a `user_fb` flag
  for that) [inferred, untested].
* Whether the Have Disk route copies anything the offline route did not need, and whether the
  DirectDraw helper DLLs matter on Windows 95 without DirectX [unknown].
* DOS boxes and full-screen DOS programs under VMDisp9x on tiny386, and Windows 95's shut-down
  screen [untested].
* The exact pool size on each board: the boot console's "Emulator memory" line says.


## 9. For Plat to decide

* 16 bits by default, with `vga_mem_size = 768K` on the Korvo-1 (14M for the guest if the pool
  allows) or 1M (13M, as now). 24 and 32 bits on offer, slower to draw until phase 3.
* Whether to try VMDisp9x on v3.9 now (3.3), knowing it is slower there, or wait for phase 1.
* Whether the Korvo-1 should still offer 800x600 (shrunk) at all, in the BIOS and the INF.
* Whether phase 2 (the panel showing the guest's buffer) is worth its changes to the panel and
  crash code, or phase 1's copy is enough. Phase 1 alone already removes nearly all of today's
  cost on a still or slowly changing screen.
* The bank and LFB-read fixes and the 800x480 BIOS modes are of use upstream too; that is for the
  other session.


## 10. Files and sources

In WSL `~/v4-work/vbe-research` (scripts `s*.sh` in the order they were run):

| Path | What |
|-|-|
| `v39/`, `upstream/` | v3.9's files read here (`git archive`), and a clone of github.com/hchunhui/tiny386 at 4ce2e06 |
| `vga_graphic_refresh.dis` | the disassembly of 1.5 |
| `seabios-src/` | `seabios-korvo-src.tar.xz` unpacked; `base/` and `mod/` the two builds of 2.3 |
| `svgamodes-800x480.diff` | the five modes |
| `drivers/vmdisp9x`, `drivers/boxv9x-phkelley` | the two drivers' sources (git clones) |
| `downloads/` | `vmdisp9x-1.2025.0.119b-driver-2d.zip` (e0d698a6...), `boxv9x_qemu.zip` v1.1.0 (b2c55026...), VBEMP `191201.zip` (93d9bd34...), each unpacked in its own folder, nothing run |
| `h/` | the harness copy, `h/harness`; `h/img/win95.pristine.img` (a read-only copy); `h/bin/v39dbg`, `v39rows`, `v39dos`; `h/runs/*` |
| `h2/` | the FreeDOS runs with the modified VGA BIOS |
| `mkreg.py`, `s36.sh` | the offline install of 6.2 |
| `patch_rows.py`, `harness-headless.diff` | the harness changes |
| `runs-summary.txt` | every run's outcome |

Sources fetched: github.com/JHRobotics/vmdisp9x (README, INF, source, release notes and issues),
github.com/JHRobotics/softgpu issues, github.com/phkelley/boxv9x, github.com/volkertb/boxv9x
(the mirror of Michal Necasek's repository), os2museum.com/wp/windows-9x-video-minidriver-hd/,
bearwindows.zcm.com.au/vbe9x.htm. ESP-IDF v6.1-dev at `~/esp/esp-idf`, only read.
