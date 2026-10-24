# Windows 95 on the bare-metal build: hazards a PC run would not show

Audit of tiny386 v3.7 (`~/tiny386-idf-scratch/v3/tiny386`, HEAD `012158e` = tag `v3.7`, read only)
as built for the ESP32-S31-Korvo-1 (`esp/build-v37k`), against ESP-IDF v6.1-dev-8142-g188e3e55bb
(`~/esp/esp-idf`, clean tree, none of tiny386's `esp-idf*.patch` applied) and the toolchain's
picolibc 1.8.11. Investigation 3 of 3, 2026-10-22. Nothing in the tree, the IDF or on the card
was changed. Work files are in WSL `~/win95-debug/audit` (section 10).

Tags: [verified] seen in a run, a build, the ELF or a named source line; [inferred] reasoned from
those; [unknown] not known from here.

## 0. Short answers

1. **One card error ends all disk I/O for the session.** FatFs makes any read, write or seek
   error on an open file sticky (`fp->err`), and tiny386 never reopens the image. After the first
   SD CRC error, timeout or failed bounce-buffer allocation, every later read and write of
   `win95.img` fails until RESET. Windows would then fail to load drivers (Windows Protection
   Error) and nothing more it writes reaches the card (no `BOOTLOG.TXT`). The only sign is a line
   on the UART and in the log ring, "The card refused N reads or writes", which the panel shows
   only under Ctrl+Alt+F12. Board only; v2 has the same code. Section 3.4. [verified: code path;
   whether it happened on the board is unknown]
2. **No fast seek: every backward seek walks the FAT chain from the image's first cluster.** In a
   logged PC run of the v3.7 core to the Windows desktop (4M/256K), the guest made 79,101
   single-sector requests and about 2,300 backward seeks. Modelled for the card, that is about
   120,000 (32 KiB clusters), 247,000 (16 KiB) or 1,010,000 (4 KiB) extra single-sector FAT reads
   before the desktop, roughly 25 to 400 s of card time; the worst single request stalls the
   emulator for 18 to 280 ms, depending on the cluster size. Board only, same in v2. Section 3.5. [verified: run and model;
   the card's time per read is inferred]
3. **The SD DMA and cache handling is sound.** The guest's IDE buffer is never DMA'd into: it sits
   at an address that is 28 or 60 modulo 64, so the driver always bounces it through internal
   RAM; FatFs's own buffers are cache-line aligned PSRAM with a write-back before and an
   invalidate after each transfer. No path was found where a DMA invalidates neighbouring dirty
   data or sends stale data. One ESP-IDF flaw: a transfer that times out on a PSRAM buffer is
   reported as a success (section 2.4). [verified]
4. **v2 could not have used `/sdcard3/win95.img`.** v2 mounts only the card's first FAT partition
   at `/sdcard` and reads only `/sdcard/tiny386.ini`. The good v2 run on the 21st therefore had
   the image on a first FAT partition (the be5b725 layout's p1 is 64 MiB, too small for it), while
   every v3.6 run used the WIN9X partition through a second FatFs logical drive. Card, partition,
   cluster size and fragmentation all differ between the good and bad runs: a confound for the
   bisect. Section 7. [verified: v2 source; the v2 card's layout is unknown]
5. **Memory:** no overlap of guest RAM with anything else, `mem_size = max` included; the
   i386 task's 4 KiB stack has about 1.3 KiB of headroom by static frame sizes and a hardware
   guard; cross-core access is read-only or atomic. The one board-only memory hazard found is
   `tlb_refill()` writing accessed and dirty bits to guest page-table addresses it never bounds:
   with a corrupt page directory entry this stores into host memory past the guest's RAM (the
   32-bit address space wraps), where a 64-bit PC would fault. Section 4. [verified: code; the
   stack figure is inferred from frame sizes]
6. **Logging:** a sustained stream costs 86.8 us a byte at 115200 baud, 1.8 ms for an
   "inw 0xa20" line and 3.9 ms for a "read_desc" line, over 95 per cent of the cost of a line; a
   read_desc loop is capped at about 256 lines a second. A normal boot to the desktop logs only
   4.6 KB (0.4 s). The Ctrl+Alt+F12 overlay costs the emulator nothing unless lines are logged
   while it is up; then each scrolled line redraws and writes back the whole console on the
   emulator's core (estimated 5 to 10 ms). Section 5. [verified: code, run; costs inferred]

## 1. Timestamps (Plat's question about UTC and local time)

No mix-up found. The PC is on W. Australia Standard Time and every time below is local (+08:00)
[verified: `Get-TimeZone`, file times]:

* v2 `images\tiny386.bin` and its bootloader: 2026-10-21 13:19:18 (05:19 UTC).
* dual-boot `bootloader.bin` and `partition-table.bin`: 16:02:33 (08:02 UTC).
* scratch tree commits carry +0800: `c2f415f` v2 baseline 15:05, `a589726` 16:33, `797b048`
  17:22; tag `v3` 18:20.
* Plat's photo of the blue screen: 18:29 on the 21st.

A v3 image tagged at 18:20 could not have been flashed, booted Windows, finished first-run setup
and blue-screened by 18:29 while Plat was out. The app in the dual-boot slot that afternoon was
the v2 baseline, as the orchestrator concluded. [inferred from verified times]

## 2. How a guest sector reaches the card (item 1)

### 2.1 The path

`ide_sector_read/write` (one call per sector: every one of the 79,101 requests in the PC log was
1 sector [verified: `runs/sect-4m.txt`]) to `bf_read_async/bf_write_async` (`ide.c` 2005 to
2129): `fseeko` then `fread`/`fwrite` (plus `fflush` after a write) on a `FILE` opened `r+b`, to
picolibc's bufio, to the VFS, to `vfs_fat_read/write` (`vfs_fat.c` 457 to 519), to `f_read/f_write`
(`ff.c` 4034 to 4247), to `ff_sdmmc_read/write` (`diskio_sdmmc.c`), to
`sdmmc_read/write_sectors` (`sdmmc_cmd.c` 493 to 713), to `sd_host_slot_sdmmc_do_transaction`
(`sd_trans_sdmmc.c` 484 to 597). [verified]

### 2.2 Where the buffers are

* **The guest's sector buffer** `s->io_buffer` is inside `IDEState`, which `pcmalloc()` takes
  from the guest's own 0xA0000 window in PSRAM (`esp_main.c` 96 to 118, `pc.c` 763), rounded to
  32 bytes. `sizeof(IDEState)` is 2144 and `io_buffer` is at offset 0x5C [verified: gdb on
  `build-v37k/tiny386.elf`], so its address is 28 or 60 modulo 64: never 64-byte aligned.
* **picolibc's FILE buffer**: `st_blksize` is 0 from `vfs_fat_fstat` (`vfs_fat.c` 708), so the
  buffer is `BUFSIZ`, which is 128 in this toolchain (`picolibc/include/stdio.h` 351) [verified].
  picolibc 1.8.11 with `__FAST_BUFIO` sends reads and writes of at least the buffer's size
  straight to `read()`/`write()` with the caller's pointer, after flushing or discarding what is
  buffered (`fread.c`, `fwrite.c` of 1.8.11) [verified: upstream source; the toolchain build was
  not disassembled]. A sector is 512 bytes, so the IDE data never goes through the 128-byte
  buffer.
* **FatFs**: `f_read`/`f_write` hand a whole sector at a sector boundary straight to
  `disk_read/disk_write` with the caller's pointer (`ff.c` 4081 to 4100, 4196 to 4217).
  Its own buffers (`fs->win`, the per-file `fp->buf`, the `FATFS` and `FIL` objects) come from
  `ff_memalloc()`, which with `CONFIG_FATFS_ALLOC_PREFER_EXTRAM` and
  `..._PREFER_ALIGNED_WORK_BUFFERS` gives cache-line aligned PSRAM (`port/freertos/ffsystem.c`).
  PSRAM on the S31 has no `MALLOC_CAP_DMA` in the heap (`heap/port/esp32s31/memory_layout.c`
  52), so the first choice (DMA and PSRAM) always fails and the second (aligned PSRAM) is used.
  [verified]
* **SDMMC DMA descriptors**: `MALLOC_CAP_DEFAULT | MALLOC_CAP_DMA`, so internal RAM
  (`sd_host_private.h` 41 to 45). [verified]

### 2.3 What the driver does with them

`sdmmc_read/write_sectors` asks `sd_host_check_buffer_alignment()` (`sd_host_sdmmc.c` 552 to
594): a PSRAM buffer must be DMA-reachable (`SOC_SDMMC_PSRAM_DMA_CAPABLE` is 1 on the S31) and
both its address and its length must be multiples of the PSRAM data cache line (64 bytes); an
internal buffer needs 4 bytes, because internal RAM is not behind the cache on the S31
(`SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE` is not defined, so `cache_hal_get_cache_line_size()` gives
0 for it) [verified]. Otherwise it bounces: `heap_caps_malloc(n * 512, MALLOC_CAP_DMA)`, which
can only be internal RAM, a `memcpy`, and a `free`, per transfer (`sdmmc_cmd.c` 40 to 59, 504 to
554, 661 to 711) [verified]. The transaction checks the alignment again and refuses a buffer that
fails it (`sd_trans_sdmmc.c` 535 to 540), then for a cacheable buffer writes the cache back
(C2M) before the transfer, for reads and writes alike, and invalidates it (M2C) after (542 to
549, 578 to 586) [verified].

So: the IDE buffer always takes the bounce path (internal RAM, no cache operation at all); FAT,
directory and partial sectors go by DMA straight into aligned PSRAM with a correct C2M/M2C pair.
Both cores share the one PSRAM data cache, so there is no second cache to keep coherent. No case
was found where a DMA read invalidates a neighbour's dirty line (the alignment check makes that
impossible) or a DMA write sends stale data (C2M comes first). [verified]

### 2.4 Two weaknesses on this path

* **A timed-out transfer into PSRAM is reported as success.** In
  `sd_host_slot_sdmmc_do_transaction`, when no interrupt arrives within the command's timeout
  (1 s for reads, 5 s for writes), `handle_event` returns `ESP_ERR_TIMEOUT` and stops the DMA, but
  the M2C sync that follows assigns `ret = esp_cache_msync(...)` and overwrites the error with
  `ESP_OK` (`sd_trans_sdmmc.c` 565 to 586); `cmd->error` was set to `ESP_OK` before the loop, so
  `sdmmc_send_cmd` returns success too (`sdmmc_cmd.c` 85) [verified]. Only PSRAM buffers are
  synced, so this hits FatFs's own window and file buffers, not the bounced IDE data. A FAT
  sector read that times out would leave `fs->win` partly old and FatFs would follow wrong cluster
  numbers: guest data read from, or written into, clusters that are not the image's. Needs a
  1 s stall of the card or the host, so unlikely [inferred]. Upstream ESP-IDF bug; worth a
  report.
* **A bounce allocation per sector.** Each guest sector costs a `heap_caps_malloc` and `free` of
  512 bytes of internal DMA RAM. If none is free the call fails with `ESP_ERR_NO_MEM`, which
  FatFs turns into a sticky error (3.4). Internal RAM free while the PC runs is printed on the
  "PC built" line; the figure from the board is [unknown].

## 3. The card's FAT32 partition (item 2)

### 3.1 Configuration in this build

From `build-v37k/config/sdkconfig.h` and `fatfs/src/ffconf.h` [verified]:

| Setting | Value | Effect here |
|-|-|-|
| `FF_FS_TINY` | 0 (`CONFIG_FATFS_PER_FILE_CACHE`) | each open file has its own 512-byte sector buffer (PSRAM) |
| `FF_USE_DYN_BUFFER` | 1 | `fs->win` and `fp->buf` allocated at mount and open, sector-sized |
| `FF_MAX_SS` / `FF_MIN_SS` | 4096 / 512 | 4096 only because of the wear-levelling sector size; the card uses 512 |
| `FF_USE_FASTSEEK` | 0 (`CONFIG_FATFS_USE_FASTSEEK` unset) | no cluster link map: seeks follow the FAT (3.5) |
| `FF_FS_NORTC` | 0 | `get_fattime()` from `time()`: no clock here, so 1980-01-01 |
| `FF_FS_NOFSINFO` | 0 | FSINFO trusted and rewritten when allocation changes |
| `FF_USE_TRIM` | 1 | TRIM only when clusters are freed (never by tiny386) |
| `FF_FS_LOCK` | 0 | no file locks (`check_config()` refuses one image named twice) |
| `FF_FS_REENTRANT` | 1, 10 s timeout | volume mutex; no contention while the PC runs |
| `CONFIG_FATFS_IMMEDIATE_FSYNC` | unset | the directory entry only on `f_sync` |
| `CONFIG_FATFS_VOLUME_COUNT` | 4 | `/sdcard` is drive 0, `/sdcard3` drive 1, bound to MBR entry 3 by `VolToPart` (`storage.c` 252 to 287) |

### 3.2 What FatFs writes when the guest writes inside `win95.img`

* **Data**: straight to the image's clusters, at once: `bf_write_async` does `fwrite` then
  `fflush`, and a whole sector at a sector boundary goes from `f_write` straight to
  `disk_write` (`ff.c` 4196 to 4216). Nothing waits in a buffer: `fp->buf` is used only for
  partial sectors, which IDE never writes. [verified]
* **FAT**: read only (`create_chain()` on an existing chain returns the next cluster after a
  `get_fat`, `ff.c` 1555 to 1559). The FATs and FSINFO are written only if a cluster is
  allocated, which needs a write or seek past the end of the chain; `ide.c` refuses requests past
  the image's end (2022 to 2029, 2081 to 2086). [verified]
* **Directory entry**: on `f_sync` only: one read and one write of the directory sector holding
  the entry (ARC bit, start cluster, size, modification time, last-access date cleared;
  `ff.c` 4307 to 4320, then `sync_fs`). [verified]
* **`f_sync` frequency**: `run_pc()` calls `ide_flush()` (`fflush` and `fsync`) after 2 s with no
  guest write (`esp_main.c` 54 to 56, 403 to 433); in the PC log there are at most 18 such
  quiet points before the desktop [verified: `runs/sect-4m.txt`].
* Elsewhere on the card: the boot menu's last-choice file on the first partition, written before
  the PC starts and only when the choice changes (`setup.c` 143 to 155). [verified]

### 3.3 A reset in the middle

* Data already handed to the card is on it; the FAT is never touched; only the directory entry's
  time is left behind. A RESET with the card still powered lets the card finish its program
  cycle. [inferred]
* `SD_PWR_EN_GPIO` 39 switches the slot's power, active low (`board_korvo1.h`, `storage.c` 60 to
  67). If that pin floats or is pulled to "off" during a chip reset, a RESET or brownout during a
  card write cuts the card's power mid-program, which on many cards can damage more than the
  sector being written (flash translation layer). Whether the switch drops on reset is
  [unknown]: check the schematic's pull on the switch's gate. [inferred]
* A Windows blue screen stops the guest writing, and an emulator crash halts the board (no
  reboot: `CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT`), both well before Plat's RESET; the 2 s quiet
  flush has run by then. So an image or partition damaged after "blue screen, then RESET" is more
  likely the guest's own file system (Windows stopped mid-update) or damage done earlier, than a
  write cut short by the RESET. [inferred]
* Ways tiny386's writes could land outside the image's clusters: (a) a FAT chain that is wrong
  already (cross-linked: writes go into another file; shorter than the file: `f_lseek` in write
  mode silently allocates new, unzeroed clusters with `create_chain()`, `ff.c` 4714, and FSINFO
  and both FATs are rewritten); (b) the masked timeout of 2.4 on a FAT sector read. Neither is
  likely on a healthy card; (a) matters after a partition has been damaged and repaired. [verified
  code; inferred likelihood]

### 3.4 Errors are sticky for the rest of the session

`ABORT()` stores the error in the file object (`ff.c` 236) and `f_read`, `f_write` and `f_lseek`
refuse to do anything while it is set (`ff.c` 4051, 4152, 4612). Every disk error inside them
(`FR_DISK_ERR` from one failed `disk_read/disk_write`, `FR_INT_ERR` from a bad chain) is an
`ABORT`. Nothing clears it but closing the file; `clearerr()` in `ide.c` clears only picolibc's
flags, and tiny386 never reopens the image. The SD layers do not retry: one data CRC error,
one timeout or one failed bounce allocation is enough. [verified]

Consequences: from the first error to RESET, every guest read and write of `win95.img` fails;
the guest's IDE driver is told each time, so Windows sees a disk that has stopped working.
That fits "Windows Protection Error during start-up" and "no `BOOTLOG.TXT` at all" (the file's
creation is itself a write), and it would not happen on a PC. [inferred]

How it shows: `disk_errors()` prints "The card refused N reads or writes of the disk images" to
the UART and the ring every 10 s while it goes on (`esp_main.c` 385 to 401), and ESP-IDF logs
"sdmmc_read_blocks failed" or "sdmmc_write_blocks failed". The panel shows them only under
Ctrl+Alt+F12. The status LED has no code for it. After a RESET by the button the last run's
lines are not shown (only after a crash or watchdog, `banner()`). [verified]

### 3.5 Seeks: the FAT chain walked again and again

`f_lseek` without fast seek (`ff.c` 4677 to 4751): a seek forward continues from the current
cluster; a seek back, even by one cluster, starts from the file's first cluster and calls
`get_fat` (`create_chain` in write mode) for every cluster up to the target. Each step that
leaves the 512-byte FAT window costs a single-sector card read (`move_window`, `ff.c` 1081 to
1102). ESP-IDF builds a link map only when `CONFIG_FATFS_USE_FASTSEEK` is set, and then only for
files opened read only (`vfs_fat.c` 413 to 444). picolibc does a real `lseek` only when the
target is not the current position (`__bufio_seek` in 1.8.11), so sequential access is free.
[verified]

Measured on the PC [verified: run `runs/r4m`, log `runs/sect-4m.txt`]: the v3.7 core built as for
the Korvo-1 (pc-repro's headless harness, esp flavour, 64-bit, with a logger added to
`bf_read_async/bf_write_async`), Windows 95 OSR2 from a fresh copy of the pristine image,
4M/256K, gen 3, no FPU, virtual clock at 2 million instructions a second. The desktop was
reached at t = 240 s. Up to then: 60,162 sector reads (27,251 distinct sectors) and 18,939
sector writes (587 runs of consecutive sectors), all one sector per request; 4,985 real seeks,
about 2,300 of them backwards.

Modelled card traffic for that request stream (`seekmodel.py`, `seek2.py`: the image contiguous,
FAT32, 128 entries a FAT sector, FatFs's one-sector window):

| Cluster size | FAT sectors spanned by the image | Extra FAT reads before the desktop | At 0.2 ms each | At 0.4 ms each | Worst single request |
|-|-|-|-|-|-|
| 4 KiB | 800 | 1,011,258 | 202 s | 405 s | 701 reads (140 to 280 ms) |
| 16 KiB | 200 | 247,135 | 49 s | 99 s | 176 reads (35 to 70 ms) |
| 32 KiB | 100 | 120,435 | 24 s | 48 s | 89 reads (18 to 36 ms) |

On top of that each of the 79,101 sectors is its own SD command (CMD17 or CMD24, then CMD13), and
a single-block write waits for the card's program cycle: perhaps 15 to 20 s of reads and 20 to 100
s of writes on a typical card. The time per card operation is [inferred]; measure it on the board
before relying on the totals. Fragmentation of the copy adds little to this (a FAT read per
far-apart fragment); the cluster size of the WIN9X partition decides it, and is [unknown] (a FAT32
volume of about 28 GiB made with default settings has 16 KiB clusters).

What this does to the guest: nothing to correctness by itself (the IDE completes synchronously),
but the PIT, the RTC and the VGA retrace follow real time (`i8254.c` 63 to 85, 268 to 291), so
after each stall the guest is handed the timer interrupts it missed, one per 512-instruction step
(`pc.c` 139, 604 to 620). Start-up takes much longer in real time than on a PC, which fits "still
loading device drivers after several minutes". The same code is in v2. [inferred]

## 4. Memory (item 3)

* **Layout.** `lcd_init()` takes the panel's frame buffer (768,000 bytes) from the PSRAM heap
  first; then `app_main` takes one aligned heap block of the largest free block less 256 KiB
  (`PSRAM_KEEP_FREE`, `esp_main.c` 1053 to 1058), and `psmalloc()` carves guest RAM and video
  memory from it in 4 KiB steps with a bounds check that stops with a message (`esp_main.c` 347
  to 363). `settle_memory()` sizes `mem_size = max` with the same rounding (`esp_main.c` 663 to
  683). Nothing else is allocated from that block, and the FatFs buffers, the console grids and
  the 16-colour cache come from the 256 KiB left in the heap, so guest RAM cannot overlap the
  frame buffer or anything else, `max` included. [verified]
* **The pcmalloc window.** Device state (IDE, VGA, PIC, PIT and so on) lives inside the guest's
  physical memory at 0xA0000 to 0xBFFFF on the board only; on a PC build `pcmalloc` is `malloc`.
  87,616 of 131,072 bytes are used [verified: harness, esp flavour]. The CPU's loads, stores,
  string moves and instruction fetches send that range to the VGA (`in_iomem`, `i386.c` 753 to
  865, 2782 to 2957) [verified]. The ISA DMA controller checks only the guest's memory size
  (`i8257.c` 418 to 468), so a DMA aimed at 0xA0000 to 0xBFFFF would read or overwrite device
  state on the board; Windows has no reason to do that [inferred].
* **Page-table walks are unbounded.** `tlb_refill()` loads the page directory and page table
  entries and sets their accessed bits through `cpu->phys_mem + address` with no check against the
  guest's memory size or the 0xA0000 window, and later sets the dirty bit through the stored
  pointer (`i386.c` 572 to 642) [verified]. With a sane guest this never matters. With a corrupt
  directory entry (a consequence of some other fault) the board stores bits into PSRAM past the
  guest's RAM, or, because 32-bit pointer arithmetic wraps, anywhere: FatFs buffers, the frame
  buffer, internal RAM, peripheral registers; a load there may also be a load access fault. A
  64-bit PC build would crash at once instead. A cheap guard (`addr < phys_mem_size - 4` and not
  in the window, else a page fault) would turn this into a clean guest fault. [inferred]
* **Stacks.** `i386_main` has 4096 bytes (`esp_main.c` 1146; v2 the same). Frames from the ELF's
  prologues [verified]: `i386_task` 48, `pc_step` 16, `cpui386_step` 32, `cpu_exec1` 336,
  `pmcall` 320, `call_isr` 240, `pc_io_write` 32, `bf_write_async` 48, `fwrite` 48,
  `vfs_fat_write` 64, `f_write` 64, `f_lseek` 64, `create_chain` 48, `move_window` 32,
  `ff_sdmmc_write` 32, `sdmmc_write_sectors` 80, `sdmmc_write_sectors_dma` 112, `sdmmc_send_cmd`
  32, the transaction 80, `xQueueReceive` 64, `esp_cache_msync` 64, `heap_caps_malloc` 32; an
  error log on the way adds about 0.5 KiB (`esp_log` 64, `console_printf` 80, `__d_vfprintf` 128,
  the UART write), and an interrupt saves its frame on the task stack. Worst case about 2.7 KiB of
  4 KiB [inferred]. `CONFIG_ESP_SYSTEM_HW_STACK_GUARD` and the FreeRTOS canary are on, so an
  overflow is a "Stack protection fault" panic, not silent corruption [verified: config]. The
  10 s UART line "stacks, least free since start" (`display.c` 38 to 58) gives the real figure;
  not yet seen from the board [unknown].
* **Cross-core.** `vga_task` (core 0) reads VGA state and video memory that core 1 writes,
  without locks: torn reads give a wrong picture for a frame, not wrong guest state; core 0's own
  writes are to drawing state and to one-writer clear counters with atomics (`vga.c` 217 to 284,
  1348 to 1353, 1576 to 1581, 2093 to 2100) [verified]. The keyboard and mouse reach the i8042
  through a locked queue and atomics (`i8042.c` 521 to 1030) [verified: present; not re-reviewed
  here]. The LED heartbeat is a relaxed atomic increment (`status_led.h` 40 to 43); the speed
  code reads one guest byte (0x4F0) and publishes 32-bit counters [verified]. The SDMMC interrupt
  is taken on core 0, where `app_main` created the host (`sd_host_sdmmc.c` 109), so every guest
  sector needs core 0 to wake core 1; core 0 holds interrupts off for each 32 KiB piece of a frame
  buffer write-back and for each LED frame, which adds latency, not errors [inferred].

## 5. Logging after the PC starts (item 4)

Path of one emulator line (`fprintf(stderr, ...)` in `pc.c` 316 to 573, `dolog` in `i386.c` 131,
`vga.c`): `stderr` is the console's tee stream (`console.c` 775 to 785): `tee_put()` takes and
gives the console's recursive mutex for every character, and at the newline `emit()` writes the
line to the UART (picolibc `stdout`, then `uart_write`, which busy-waits for FIFO space per byte
and adds CR: `uart_vfs.c` 186 to 196, 226 to 248), copies it into the 8 KiB ring in LP SRAM under
a spinlock, and puts it in the console's text grid in PSRAM (when the cursor is on the last row,
a 5.4 KB `memmove` to scroll). While the PC has the panel, `render()` returns at once. [verified]

Cost per line in a sustained stream [inferred from the code; UART arithmetic exact]:

| Line | Bytes with CR LF | UART at 115200 | Tee, ring, grid |
|-|-|-|-|
| `inw 0xa20 <= 0xffff` | 21 | 1.8 ms | about 0.1 ms |
| `set VGA mode 12` | 17 | 1.5 ms | about 0.1 ms |
| `read_desc: sel 77bc base 0 limit 0 off 77b8` | 45 | 3.9 ms | about 0.1 ms |

The UART's 128-byte FIFO absorbs an occasional line, so sporadic lines cost little; a stream runs
at the UART's pace. The read_desc loop of run c) therefore ran at most about 256 lines a second:
if each pass is around 1,000 guest instructions, the emulator was held to about 0.26 million
instructions a second, an eighth of its speed, with the timer interrupts piling up against real
time. In the PC run to the desktop the emulator logged 4,644 bytes in all, 0.4 s of UART time, so
normal running is not slowed by logging [verified: harness line].

Turning runtime UART output off by default (crash report kept, an ini key to turn it on) removes
over 95 per cent of the per-line cost; the ring keeps the lines for Ctrl+Alt+F12.

The overlay (Ctrl+Alt+F12, `speed.c` 290 to 347): while it is up `vga_task` skips the guest's
picture, and the title row is redrawn once a second on core 0, so the emulator's core pays
nothing. But every line the emulator logs while it is up is drawn by the logging core itself
(`display_trylock_draw()`, up to 200 ms wait for the panel); once the text scrolls, every cell
changes, so each line redraws about 2,800 cells and writes back up to 717 KB of frame buffer:
estimated 5 to 10 ms a line. Under a log stream the overlay roughly triples the cost of each
line. [inferred]

## 6. Timers, PIT, i8042, IDE timing (item 5)

* The PIT counts real microseconds (`i8254.c` 63 to 85); channel 0's interrupt is raised from
  `pc_step()` every 512 instructions, at most one per step, until it has caught up
  (`i8254.c` 268 to 291). Every stall (card I/O, FAT walks, UART lines) is seen by the guest as
  time passing with no instructions run, then a burst of timer interrupts. The CMOS periodic
  interrupt and, with `VGA_RETRACE_REALTIME`, the retrace bit behave the same way. At 2.2 million
  instructions a second (about a slow 386SX), each stall is followed by a run of timer handlers
  that a PC run at the same virtual rate never has. [verified: code; effect inferred]
* `i8254_update_irq()` calls `abort()` if channel 0 is in a mode other than 2 or 3 (`i8254.c`
  288 to 289). Shared with the PC, so a PC run would show it; noted because it is an `abort()`
  path in the step loop. [verified]
* IDE completes inside the guest's port access, so the guest never waits on BSY; real time
  passes during it (section 3.5). [verified]
* i8042: the queue and the mouse pacing are shared code with cross-core locking; under heavy
  interrupt load the guest reads the controller less often, so the queue fills and drops sooner
  than on a faster host. Not re-audited here. [inferred]
* `CONFIG_FREERTOS_HZ` went from 1000 (v2) to 100 (v3, commit `a589726`). In ESP-IDF's SD busy
  poll (`sdmmc_common.c` 445 to 491) the wait doubles from 100 us up to one tick: at 100 Hz a write
  whose program cycle takes 3.2 to 6.3 ms is seen done at 6.3 ms, 6.4 to 12.7 ms at 12.7 ms, and
  after that only every 10 ms, where at 1000 Hz it was every 1 ms. Slower writes, no correctness
  effect found. [verified: config and code; effect inferred]

## 7. v2 against v3.7: bare-metal differences that matter for the bisect

Same ESP-IDF (v6.1-dev-8142-g188e3e55bb, v2 `BUILD-NOTES.md` 156), same toolchain, same FatFs,
SDMMC driver and picolibc, same 4 KiB i386 stack, same PSRAM speed (250 MHz) [verified]. So
sections 2.4, 3.4 and 3.5 apply to v2 as much as to v3.7. What differs on the bare-metal side
[verified: v2 tree `~/tiny386-idf-scratch/v2/tiny386` and v3 commits]:

* **Where the image was.** v2: `esp_vfs_fat_sdmmc_mount("/sdcard")` only (v2 `storage.c` 113),
  ini from `/sdcard/tiny386.ini` or `/spiflash/tiny386.ini` (v2 `esp_main.c` 296 to 306). FatFs's
  automatic choice is the first partition with a FAT boot sector. v3.6/v3.7: every FAT partition,
  the image at `/sdcard3/win95.img` through a second logical drive (`storage.c` 196 to 292).
  Ask Plat which card and which partition held `win95.img` on the afternoon of the 21st: on the
  be5b725 layout p1 is 64 MiB and cannot hold a 419 MB image. A fair A/B test runs v2 and v3.6
  against the same file on the same partition of the same card.
* `CONFIG_FREERTOS_HZ` 1000 to 100; `CONFIG_SPIRAM_MEMTEST` on to off (v2 tested all of PSRAM at
  every boot, v3 never does); `CONFIG_ESP_MM_CACHE_MSYNC_C2M_CHUNKED_OPS` on; panic halts instead
  of rebooting (all `a589726`).
* stdout and stderr through the console's tee, ring and grid (v2 wrote straight to the UART);
  `ESP_LOG` through the console.
* The 2 s quiet `f_sync`, the speed counter, the status LED task, the boot menu's file.

## 8. Root-cause candidates from this audit, most likely first

1. **A sticky FatFs error after one card error** (3.4). Explains no `BOOTLOG.TXT`, a Windows
   Protection Error at start-up and failures that a PC never shows. Confirm: after a failure,
   press Ctrl+Alt+F12 and photograph the console; look for "The card refused" and
   "sdmmc_read_blocks failed" or "sdmmc_write_blocks failed" lines, or capture the UART once.
   Rule out: no such line in a failing run. Either way, add a counter of card errors to the
   overlay's title and a status LED code.
2. **Slow, bursty card I/O from FAT walks and single-sector commands** (3.5). Explains very slow
   start-up; could expose timing-sensitive guest code through timer bursts. Confirm: watch the
   overlay's "million instructions a second" during start-up (it includes stalls) or time the
   SD calls; learn the WIN9X cluster size. Rule out: the rate stays near 2.2 during disk-heavy
   phases.
3. **The storage path differs between the good and bad runs** (7). Not a fault by itself, but it
   can make a v2 against v3.6 comparison point at the wrong change. Confirm by asking Plat and by
   an A/B on one card and partition.
4. **PSRAM bit errors at 250 MHz** under the panel's DMA and two busy cores. Random guest crashes
   (fault 0D in VMM) and a corrupt image would follow. v2 ran a full PSRAM test at each boot
   and v3 does not. Confirm: one boot with `CONFIG_SPIRAM_MEMTEST=y`, and an A/B at a lower PSRAM
   speed. [inferred; no evidence either way]
5. **ESP-IDF's masked timeout on PSRAM transfers** (2.4) leading to misdirected I/O. Low
   likelihood; confirm with an `ESP_LOGE` on every `ESP_ERR_TIMEOUT` from `handle_event`.
6. **Unbounded page-table walks** (4) turning a guest fault into host memory damage. Only after
   another fault; a bounds check makes it harmless.
7. **Card power cut at reset during a write** (3.3). Only with a reset during disk activity;
   check the power switch's behaviour at reset.

## 9. Recommended fixes and checks

For the board (Plat):

* After any Windows failure, Ctrl+Alt+F12 and a photo before RESET.
* Note the WIN9X partition's cluster size (`fsck.fat -v` or `minfo`), and which card and partition
  the v2 run used.
* One boot with the PSRAM test on, if a build with it is offered.

For the firmware (Fix phase):

* **Recover from card errors**: on a failed read or write in `bf_read_async/bf_write_async`
  (BUILD_ESP32), close and reopen the image (or `f_close`/`f_open` underneath) and retry once,
  so that a transient error costs one request, not the session; show a count of card errors on the
  overlay and as a status LED code; show the last run's error lines after a button reset too.
* **Fast seek for the image**: open it with a cluster link map. FatFs allows fast seek on a file
  open for writing as long as it is not extended, and `ide.c` never extends it. Either patch
  ESP-IDF's `vfs_fat_open` to build the map for `r+` opens when asked, or have `ide.c` on the
  board open the image with FatFs directly (`f_open`, then `f_lseek(fp, CREATE_LINKMAP)` with a
  table sized from the fragment count). Alternatively map the image's cluster runs once and read
  and write the card with `sdmmc_read_sectors/sdmmc_write_sectors` at those LBAs, keeping FatFs
  for the directory entry only.
* **Fewer card commands**: read ahead (for example 8 sectors into an aligned internal buffer) for
  sequential IDE reads, and keep one aligned internal bounce buffer instead of a `malloc` and
  `free` per sector.
* **ESP-IDF**: keep the first error in `sd_host_slot_sdmmc_do_transaction` (do not overwrite
  `ret` with the M2C result); report upstream.
* **`tlb_refill`**: bound both table addresses against `phys_mem_size` and the 0xA0000 window,
  and treat a bad one as a page fault.
* **Logging**: runtime UART output off by default with an ini key to turn it on; crash report on
  the UART always; consider rate-limiting identical lines (a "read_desc" loop then costs nothing).
* Consider `CONFIG_FREERTOS_HZ=1000` again, or `CONFIG_SD_READY_POLL_PERIOD_START_US` tuned, for
  write latency; measure first.

## 10. Files and how to rerun

In WSL, `~/win95-debug/audit` (mine; the v3.7 tree, the IDF and the pristine image were only
read):

* `harness/`: a copy of pc-repro's headless harness; `harness/sectlog.sed` adds a request logger
  (environment variable `SECTLOG`) to the copy of `ide.c` that `build.sh` makes (`SED_IDE_FILE`).
* `bin/v37-esp-64/headless`: built with
  `SED_IDE_FILE=$PWD/harness/sectlog.sed bash harness/build.sh ~/tiny386-idf-scratch/v3/tiny386 esp bin/v37-esp-64 64`
  (the 32-bit build needs 32-bit kernel headers this Debian lacks).
* `runs/r4m/` and `runs/sect-4m.txt`: the run of section 3.5, made with
  `IMG=/mnt/d/Repos/esp32-s31-linux-release/tiny386/win95/win95.img WALLMAX=900 NODISKCHECK=1 SECTLOG=$PWD/runs/sect-4m.txt bash harness/run.sh bin/v37-esp-64/headless $PWD/runs/r4m 4M 256K -insns 2500 -snap 20 -epoch 0 -dialogs 6 -vga-ms 30`
  (run.sh copies the image into the run folder; the copy is deleted afterwards). 95 s of host
  time.
* `seekmodel.py`, `seek2.py`, `wr.py`: the models and counts of section 3.5
  (`python3 -I seekmodel.py runs/sect-4m.txt 240`).
* `v37k.dis`: the v3.7 Korvo-1 ELF disassembled, for the frame sizes of section 4 (`z19.sh`,
  `z20.sh`).
