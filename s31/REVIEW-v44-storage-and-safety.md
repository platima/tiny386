# Review of v4.4: storage and safety

Written 2026-10-24 as an independent, sceptical review of v4.4's storage work and its safety:
`hda = sd:N` (partition passthrough, `esp/main/sd_part.c` and its callers), the master boot record
and geometry the guest sees, the refusals, the new-card procedure (`s31/newcard/newcard.sh`,
`export-root.sh`, `README.md`), the dual-boot images and scripts in this folder, and what was pushed
to github.com/platima/tiny386. The code reviewed is the branch `s31-v4.4` in WSL `~/fw/tiny386-s31`
(worktree `~/fw/wt-v44`), at 6d619a0, which is also `esp32-s31` and the tag `s31-v4.4`.

Nothing was flashed, no serial or COM port was opened, nothing was committed or pushed, and the
worktree, the fork clone `D:\Repos\tiny386` and this folder were only read (this file is the one
thing written here). My own work is in WSL `~/org2/review-storage-and-safety` (section 8). Every
claim is marked [verified] (seen in a file or measured in a run named here), [inferred] or
[unknown].

(Added 2026-10-25: the commit and tag hashes in this review are the ones the branch has had since
its history from `s31-v4.4` on was rewritten and force-pushed that day, to take out the nine lines
of finding 3 (`BUILD-NOTES.md` 32.6). The code they name is the same. Finding 3's own row says
"pushed with `s31-v4.4`" where it named that commit's old hash, as the rewritten one no longer
holds the lines. Nothing else in this review was changed.)


## 0. The answer in one screen

* **No guest write can reach p1, p2 or anything outside the partition.** `sd_part_write()` refuses
  any request that does not lie wholly inside the partition before it touches the card, and the
  partition itself is refused if it overlaps any other entry of the card's table [verified: code].
  Two million random reads and writes of 0 to 299 sectors, aimed at the whole guest disk, both
  partition edges, LBA 0, the 32-bit wrap and anywhere at all, put 6,943,017 card writes and
  9,058,941 card reads through `sd_part.c` and not one landed outside p3; UBSan found nothing
  [verified: my probe, section 8]. The branch's own host test 14b, rerun: 12 of 12 [verified].
* **The guest's MBR and geometry are sound.** Our boot code only reads (INT 13h AH=41h, 42h, 02h,
  then INT 10h and 18h); writes to LBA 0 are refused; the disk is 16 heads, 63 sectors, sized to the
  partition's end rounded up to a cylinder (1,876,896 sectors, 916 MiB, for the default layout)
  [verified: `sd_part_mbr.s`, probe]. SeaBIOS takes its translation from CMOS 39h, which tiny386
  never writes, so CHS reaches 504 MiB and the LBA calls everything [verified: SeaBIOS
  `src/block.c` `get_translation()`, `ide_fill_cmos()`].
* **FatFs and the guest never share the partition.** The firmware unmounts `/sdcardN` before the
  PC is built, refuses `/sdcard` itself, and `sd_part_open()` refuses again if the partition is
  still mounted; nothing mounts the card's other partitions again while the PC runs, and the inis
  are only ever read from `/sdcard` or `/spiflash` [verified: code, host test, probe 4a].
* **The migration cannot destroy the old card or Linux, but the docs overstate it.** `newcard.sh`
  writes only its output file (it refused an existing file, `/dev/null` and a symlink to it in my
  runs) and `export-root.sh` only reads p2 and sector 0. But "the old card is never written" is
  wrong: the procedure adds three files (192 MiB on Plat's layout) to the old card's WIN9X
  partition, copies a script onto KORVO_SD and remounts the old root read-only and back (finding 1).
* **One wrong-partition case gets through.** On Plat's current card, `win95-sd.ini` hands the guest
  the WIN9X partition (FAT32, the one holding `win95.img`) raw, with no warning; the branch's own
  test says so. Nothing boots from it, so nothing is written by itself, but `TEST-PLAN.md` 16a has
  Plat copy `win95-sd.ini` with the other card files (finding 2).
* **The dual boot is untouched.** Every bootloader, partition table and `flash_args` is v4.3's byte
  for byte; the four scripts are the branch's and unchanged since 8d4b758; `SHA256SUMS` 180 of 180
  [verified].
* **What was pushed is clean of addresses and binaries.** 17 commits, author and committer
  Platima <13729856+platima@users.noreply.github.com>, the tagger the same, no binary among the 93
  files, none of the scratch repository's five other addresses anywhere [verified]. One policy
  point: a check file quotes lines of a Windows-written log verbatim (finding 3).
* **Nothing here blocks Plat trying v4.4.** Four minor findings and six notes (section 7).


## 1. Can a guest write reach p1, p2 or outside p3?

**The path of a write** [verified: `ide.c`, `sd_part.c`]. The guest's WRITE SECTORS and WRITE
MULTIPLE go through `ide_sector_write_cb1()` to the block device's `write_async`, which for `sd:N` is
`sdp_write_async()`, which calls `sd_part_write()`. `ide.c` has no DMA and no 48-bit commands, so
that is the only way in. `sd_part_write()` refuses (`SD_PART_REFUSED`) when `lba < start`,
`lba + n > start + count` or `n == 0`, before any card access; `lba` is 64-bit and `n` at most 4 from
`ide.c`, so nothing wraps. Only then does `card_rw()` write, in runs of up to 4 sectors through a
2 KiB DMA buffer, at the same sector numbers. A card that refuses is tried once more and counted.

**The partition can overlap nothing** [verified: `sd_part_plan()`]. It must be type 04, 06, 0E, 0B
or 0C, start after sector 0, end inside the card and at or below 2^28, be at least 2,048 sectors,
FAT16 no more than 4 GiB, and overlap no other entry with a type and a size; a GPT (an EEh entry), a
card with no 55AAh and a card that is one FAT volume are refused. The table is read once, at start,
and the guest cannot change it (LBA 0 is ours and refuses writes).

**Reads** [verified]. Inside the partition, the card's sectors; LBA 0, our record; everything else,
zeros. So the guest also cannot read p1 or p2.

**Tested** [verified].
* Host test 14b, `tests/host/sdpart/run_sdpart.sh`, rerun in my own output folder: 12 results, 0
  failed, including the nine kinds of write outside p3 through the IDE ports with no write reaching
  the card, and sector 0 to p1, p1 and p2 unchanged to the sector.
* My probe (`~/org2/review-storage-and-safety/probe/probe.c`): `sd_part.c` against an in-memory card
  that flags every access outside p3. 2,000,000 random calls (half reads, half writes, 0 to 299
  sectors, starting anywhere from LBA 0 to 2^64): 196,740 writes accepted and 803,350 refused, every
  one of the 6,943,017 card writes and 9,058,941 card reads inside p3; `n = 0` and `n = 2^32 - 1`
  refused. Built with `-fsanitize=undefined -fno-sanitize-recover`: no report.
* The PC harness's Windows runs from p3 (`pp-sd512`, `pp-sd64`, `sd-512`, `sd-512k`, `sd-64` in
  `~/org2/v44sd/ph/runs`) logged no refused write and no card error [verified: their `log.txt`],
  so Windows 95 itself never writes outside its partition in those runs.

**Wording** [verified]. "Every sector of a write ... or none of it" holds for each call of
`sd_part_write()`. `ide.c` splits a guest command into calls of at most 4 sectors (1 for WRITE
SECTORS), so a command that runs across p3's end writes its sectors inside p3 and then fails. That is
still inside the partition (note 8).


## 2. The synthesized MBR and the geometry

* **Boot code** [verified: `sd_part_mbr.s`; the host test assembles it and compares it with
  `sd_part_mbr.h`]. It copies itself to 0600h, finds the active entry, reads one sector by AH=42h
  when AH=41h says the LBA calls are there, else by the entry's CHS bytes (AH=02h), checks 55AAh and
  jumps with DL and DS:SI and DS:BP as a DOS MBR does. It never writes. With the partition past CHS
  reach the CHS bytes are 1023/15/63 (sector 1,032,191, inside p1 on the default layout), which reads
  as zeros, so a BIOS without LBA calls gets "no boot sector", not someone else's boot code.
* **Record** [verified: `sd_part_make_mbr()`, host test]. Entry 1 active with the card's own type,
  start and length; entries 2 to 4 empty; the card's disk signature; 55AAh.
* **Geometry** [verified: `ide.c`, probe]. 16 heads, 63 sectors; the disk ends at the partition's
  end rounded up to a whole cylinder, at least 2 cylinders. IDENTIFY's cylinders are capped at
  16,383; words 60 and 61 carry the whole size. For the default layout: 1,862 cylinders, 1,876,896
  sectors. CMOS gets type 47 with those numbers from `ide_fill_cmos()`, and never byte 39h, so
  SeaBIOS (built with `CONFIG_QEMU=y`, `seabios/config`) uses no translation [verified].
* **Edge** [verified: probe 2]. A partition ending within 1,007 sectors of 2^28 gives a disk of more
  than 2^28 sectors (268,436,448 for one ending at 2^28), past what 28-bit LBA can name; READ NATIVE
  MAX then wraps in `ide_set_sector()`. No write can escape (the bounds are the partition's), and
  nobody will put p3 there on a 128 GiB card soon (note 7).
* **Windows 95's side** [inferred from part 2's notes, not redone]. Part 2 read the OSR2 boot
  sector's bytes and found it picks LBA from its partition entry's type; the guest's entry carries the
  card's type and the real start, and `newcard.sh` sets the hidden-sectors field to that start, so the
  two agree whichever way the boot sector decides.


## 3. Refusals, and a missing or wrong partition

**FatFs mounted** [verified: code; host test; probe 4a]. `check_card_partition()` in `esp_main.c`
calls `storage_part_release()`, which unmounts `/sdcardN` (`f_mount(NULL)`, the VFS path, the logical
drive) and refuses outright if the partition is `/sdcard`. `sd_part_open()`, from `ide_attach()`,
refuses if `storage_part_mounted()` still says so. Nothing remounts: `storage_sd_mount_others()` runs
only in `card_volumes()` at start (and in the no-card retry loop before any ini is read); the DOS
board's early mount is `/sdcard` only; `disk_image.c`'s reopen uses FatFs paths of volumes still
mounted and never mounts [verified: grep of `esp/main`]. The ini, the boot menu's "last" file and the
Ctrl+Alt key saves all live on `/sdcard` (`s_places` names only `/sdcard/tiny386`, `/sdcard` and
`/spiflash`) [verified]. Which partition `/sdcard` is, `storage.c` finds by the volume serial FatFs
mounted, so even a card whose p1 FatFs rejects ends with `/sdcard` marked on the right partition
[verified: code]; only a volume with serial 0 falls back to "first FAT partition", which is also
FatFs's own choice [inferred].

**Missing or malformed** [verified: host test, probe, harness log]. No card, no table, a GPT, an
empty slot, a non-FAT type, an overlap, a start at 0, past the card or 2^28, under 1 MiB, FAT16 over
4 GiB, non-512-byte sectors or a volume bigger than its partition: each a red line and the PC does not
start. `sd:N` named twice is caught by the existing "one image named twice" check (`strcasecmp` of
the two names); `cda`, `fda` and `fdb` naming `sd:N` are refused; another disk, a floppy or a BIOS on
`/sdcardN/` is refused before the release [verified: code; `check_card_partition()` itself is in no
host test, as 30.3 says]. `kernel`, `initrd` and `linuxstart` are path keys not in that list: one
on `/sdcardN` would only fail to open after the release, not corrupt anything [inferred] (note 10).

**A wrong partition that is accepted** [verified: host test "Plat's card as it is", my probe 3]. On
Plat's current card (p1 FAT32 KORVO_SD, p2 ext4, p3 FAT32 WIN9X from sector 532480 to the end of a
31 GB card) `hda = sd:3` is accepted with no yellow line: "card partition 3, FAT32 "WIN9X", 29404 MiB
... the guest's disk is 29664 MiB". That partition holds `win95.img` (and, after `export-root.sh`,
`korvo-root.img`). `win95-sd.ini` has no floppy or CD, and the WIN9X volume's boot code is not an
operating system, so the guest stops at "not a bootable disk" or similar and writes nothing
[inferred: what boot code WIN9X has depends on what formatted it, unknown]. But the guest has raw
write access to the volume with Plat's working Windows in it, and `TEST-PLAN.md` 16a lists
`win95-sd.ini` with the card files to copy, so the boot menu on the old card will offer it
(finding 2). A cheap guard exists: before the release FatFs still has `/sdcardN` mounted, so the
firmware can see `/sdcardN/win95.img` and refuse.

**Type 00 with a size** [verified: probe 1]. The overlap check skips entries whose type is 00 even
when they have a size; Linux registers such an entry as a partition (it skips only size 0). Nobody
makes such a table on purpose (note 6).


## 4. The migration procedure and its helpers, step by step

| Step | What it touches | Can it destroy the old card or Linux? |
|-|-|-|
| 1a. Copy `export-root.sh` onto KORVO_SD (`G:\`) on the PC | adds a file to the old card's p1 | No [verified: a file copy] |
| 1b. `sh /mnt/sd/export-root.sh` on the old card's Linux | reads `/dev/mmcblk0p2` and sector 0; may `mkdir /mnt/win` on the root and mount p3 there; remounts `/` read-only, then read-write; writes `korvo-root.img` (p2's size, 192 MiB on Plat's layout), `korvo-mbr.bin`, `korvo-export.txt` onto WIN9X | No: it never writes to `/dev/mmcblk0`; FAT writes add files only; a full WIN9X is refused before copying; a failed `dd` or a failed remount stops with words [verified: code, CRLF-free LF file identical to the branch's]. On the board [unknown] |
| 2. Copy files into `\\wsl$\...\newcard` | reads the old card | No; Windows may add its own folder to the card's FAT volumes when it mounts them [inferred] |
| 3. `newcard.sh` in WSL | writes only `OUT` and `OUT.work` (removed at the end) | No: refuses an `OUT` under `/dev` and any `OUT` that exists, including a symlink to a device [verified: my runs b, c, d]; inputs opened read-only [verified: code] |
| 4. Image writer onto the new card | the target the user picks | Only if the wrong target is picked; README says eject the old card first (note 9) |
| 5. Linux from the new card | p1 at `/mnt/sd`, root `/dev/mmcblk0p2` | No: the fork's `S10sdcard` mounts only p1, its initramfs mounts p2 by number [verified: `D:\Repos\esp32-s31-linux`, read only]; Plat's own root's scripts [unknown] |
| 6. tiny386 with `win95-sd.ini` | p3 raw | No, section 1 |
| 7. Back to the old card | | It still boots; it is not "never written" (finding 1) |

**`export-root.sh` details.**
* It finds where p2 is mounted from `/proc/mounts`. On Plat's Linux and on the fork the initramfs
  mounts `/dev/mmcblk0p2` by name, so it is found and made read-only [verified: the fork's
  `br2-external/board/esp32s31/init`; Plat's be5b725-era initramfs inferred the same]. If the kernel
  mounts the root itself, `/proc/mounts` says `/dev/root`, and the script only recognises it when
  `/proc/cmdline` holds the literal `root=/dev/mmcblk0p2`; with `root=PARTUUID=...` or `root=179:2` it
  would copy p2 live, read-write, without saying so. `newcard.sh`'s `e2fsck -fn` would probably catch
  a torn copy, and the old card is untouched either way (note 5).
* A swap file on the root (Plat's be5b725 root had a 64 MiB one) keeps being written while the root
  is read-only; only swap pages change, so the copy's file system is consistent [inferred].
* "FOLDER must be on a FAT partition" is not checked; a folder on the root itself just fails the
  `dd` (read-only) and stops [inferred: code].

**`newcard.sh` details** [verified: code, my runs].
* Layout p1 at 8192, p3 straight after, p2 after p3; table order p1, p2, p3; no overlaps; p4 empty;
  checks `fsck.fat` on p1, the same `fsck.fat` verdict on p3 as on the source volume, p3 against the
  source sector for sector but the hidden-sectors field, every file listed by `mtools`, p2 sector for
  sector and `e2fsck -fn`. Run a: "ready", table as described.
* It copies the Windows image's partition type as it is. With a type 06 image and the default p1
  (p3 from sector 1,056,768, past the 1,032,192 sectors CHS reaches in the guest) it still says
  "ready" (my run e), although by 30.2's own reading the OSR2 boot sector then reads by CHS and
  cannot boot. Reads only, so no harm, but "ready" misleads (finding 4). The pristine image is 0E
  [verified: `checks\partition-passthrough-v4.4.txt`]; Plat's own image comes from the same install
  [inferred].
* It keeps the old card's disk signature (good for anything that names PARTUUIDs), so both cards
  carry the same one (note 9).


## 5. The dual-boot images and scripts

* `images\*\bootloader.bin`, `partition-table.bin` and `flash_args`: identical to
  `previous-v4.3\images\` in all four sets; only `tiny386.bin` (and the ELFs) changed [verified:
  `cmp`]. The dual-boot app is 929,456 bytes in a 1,966,080-byte partition at 0xE20000 [verified:
  `dualboot\partitions-korvo1-dualboot.csv`].
* `flash-dualboot.bat`, `flash-standalone.bat`, `back-to-linux.bat`, `flash-paths.cmd`: identical
  to the branch's `s31/` copies, which `git diff 8d4b758 esp32-s31` does not touch; nothing under
  `s31/dualboot`, `esp/partitions*`, `esp/CMakeLists.txt` or `esp/sdkconfig*` changed either
  [verified].
* `SHA256SUMS`: 180 of 180 OK; each image folder's own list OK [verified: `sha256sum -c`].
* The app-update simulation (`checks\app-update-v4.4.txt`) shows every changed byte inside
  0xE20000 to 0xF02FFF [verified: read; not rerun].


## 6. What was pushed to the fork

Checked in `D:\Repos\tiny386` (read only) and against GitHub with `git ls-remote` [verified]:
* Remote `refs/heads/esp32-s31` is 6d619a0, a fast-forward from 8d4b758 (17 commits); remote tag
  `s31-v4.4` is 7d7febe (annotated, pointing at 6d619a0). No `s31-v4.4` branch on the remote, and
  only `s31-*` tags there: none of the 19 tags the fetch pulled in by mistake.
* Every one of the 17 commits: author and committer Platima
  <13729856+platima@users.noreply.github.com>; the tagger the same; the only address in any message
  is noreply@anthropic.com.
* The five addresses in the scratch repository's history other than noreply ones (two of them Plat's
  personal ones): none appears in the `s31-v4.4` tree or in v4.4's commit data. (Two of their local
  parts are common words that match unrelated text.) The whole branch past Plat's master carries
  only Plat's noreply address and upstream's own authors (hchunhui, yejun), whose commits came from
  upstream.
* The 93 files changed are all text (`.c`, `.h`, `.s`, `.sh`, `.py`, `.md`, `.txt`, `.ini`,
  `.diff`, `.log`); `git diff`'s numstat output shows no binary.
* Windows content: no image, picture or binary. But `s31/checks/partition-passthrough-v4.4.txt`
  (section 2, and its copy in this folder's `checks\`) quotes nine lines of `IOS.LOG` verbatim, a log
  Windows 95 writes on C: (finding 3). `BUILD-NOTES.md` 30.2 describes the boot sector's behaviour
  without copying it, which is fine.


## 7. Findings

| # | Severity | Where | What | Fix |
|-|-|-|-|-|
| 1 | minor | `s31/newcard/README.md` (intro and step 7), `TEST-PLAN.md` 16e, `BUILD-NOTES.md` 30.0, `FIRMWARE.md`'s table row for `newcard\` | "The old card is never written" and "leaves the old card untouched" are false: step 1 copies `export-root.sh` onto KORVO_SD, and `export-root.sh` writes three files (192 MiB) onto WIN9X, may make `/mnt/win` on the root and remounts the root read-only and back. Nothing is destroyed, but Plat is told something that is not so, about the card he keeps as his fallback | Say what happens: "only files are added to its two FAT partitions; its table, Linux's root and `win95.img` are not changed", and mention deleting `korvo-root.img` from WIN9X afterwards |
| 2 | minor | `esp_main.c` `check_card_partition()`, `sd_part_plan()`; `TEST-PLAN.md` 16a | On the old card `win95-sd.ini` hands the guest the WIN9X partition (the one holding `win95.img`) raw, accepted silently [verified: the branch's own test, my probe]; 16a has Plat copy `win95-sd.ini` with the other card files, so the old card's boot menu offers it | Before `storage_part_release()`, refuse if `/sdcardN/win95.img` exists (or the label is WIN9X): "this is the WIN9X partition of a card laid out for the Linux route". And in 16a: copy `win95-sd.ini` only to the new card |
| 3 | minor | `s31/checks/partition-passthrough-v4.4.txt` section 2 (pushed with `s31-v4.4`), `checks\` here | Nine verbatim lines of `IOS.LOG`, a file Windows 95 writes inside its image. Trivial machine-written text, but the rule is to publish nothing from Windows images or files from them | Paraphrase ("units 00 and 01 through real-mode drivers") in a later commit; history keeps the old text unless Plat wants it rewritten, which he decides |
| 4 | minor | `s31/newcard/newcard.sh`, the layout and table steps | The Windows image's partition type is copied as it is; a type 04 or 06 volume placed past sector 1,032,192 (the default puts p3 at 1,056,768) gets "ready" although, by `BUILD-NOTES.md` 30.2, its boot sector then reads by CHS and cannot boot [verified: my run e with a stand-in image] | Write 0E (FAT16) or 0C (FAT32) for p3 whenever it ends past 1,032,192, or refuse with words; `sd_part_plan()` could add the same yellow note |
| 5 | note | `s31/newcard/export-root.sh`, lines 51 to 61 | A root the kernel mounted (`/dev/root`) is recognised only by a literal `root=/dev/mmcblk0p2` in `/proc/cmdline`; any other form would copy p2 live and read-write without a word. Fine on Plat's and the fork's Linux, where the initramfs mounts p2 by name | Compare device numbers (`/sys/class/block/mmcblk0p2/dev` against `/`'s), or stop if `/` is `/dev/root` and cannot be tied to p2 |
| 6 | note | `sd_part.c` line 193 | The overlap check skips entries with type 00 even if they have a size; Linux counts those as partitions | Skip only entries with count 0 |
| 7 | note | `sd_part.c` line 207 | A partition ending within 1,007 sectors of 2^28 gives a guest disk over 2^28 sectors (268,436,448), past 28-bit LBA; READ NATIVE MAX wraps. No write escapes | Refuse an end past 2^28 minus one cylinder, or cap `disk_sectors` at 2^28 |
| 8 | note | `sd_part.h`, `BUILD-NOTES.md` 30.2, part 2's report | "All of a request or none of it" is per call of at most 4 sectors, not per guest command | Say "per transfer of up to four sectors" |
| 9 | note | `s31/newcard/README.md` | Not said: both cards carry the same disk signature (Windows takes a disk with a colliding MBR signature offline and rewrites it if it is brought online, so do not put both cards in the PC at once); `korvo-root.img` holds the Linux root's secrets (Wi-Fi settings, SSH host keys, history) on WIN9X and in `~/newcard`; check the image writer's target by its size (Plat has a backup drive, `E:`) | Three lines in "Afterwards" and step 4 |
| 10 | note | `esp_main.c` lines 1012 to 1064 | `check_card_partition()` (steps 1 to 4 of 30.3) is in no host test; `kernel`, `initrd` and `linuxstart` are not among the paths checked against `/sdcardN/` (they would fail to open, not corrupt) | A host test of `check_config()` with the four steps; add the three keys |


## 8. What I ran, and where

Everything is in WSL `~/org2/review-storage-and-safety`; scripts as written are also in the
session's scratch folder `rvss\` on Windows.
* `sdpart/`, `sdpart.log`: the branch's host test 14b with my own output folder: 12 passed, 0
  failed; the worktree stayed clean (`git status` empty).
* `probe/`: `probe.c` (my own; `sd_part.c` against an in-memory card, the cases in sections 1 to 3),
  `run.sh`, `probe-ub.out`. Result above.
* `nc/`, `nc-test.sh`: `newcard.sh` with a stand-in FAT16 "Windows image" made by `mkfs.fat` and a
  32 MiB ext4 made by `mkfs.ext4` (no Windows content): an ordinary run (ready), an existing `OUT`
  (refused, untouched), `/dev/null` (refused), a symlink to `/dev/null` (refused), a type 06 image
  (ready, finding 4); the work folder gone after each.
* `s5.sh`: the address search (the addresses themselves are only in `personal-addrs.txt` there,
  never printed here).
* Read only: `~/fw/wt-v44`, `~/fw/tiny386-s31`, `~/org2/v44sd/ph/runs/*/log.txt`, `D:\Repos\tiny386`
  (plus one `git ls-remote`), `D:\Repos\SeaBIOS`, `D:\Repos\esp32-s31-linux`, this folder.

**An accident of mine, put right.** At about 14:09 a rerun of my probe built with AddressSanitizer
hit ASan's known start-up failure (it prints `AddressSanitizer:DEADLYSIGNAL` without end) and wrote
12.9 GB of that one line into `~/org2/review-storage-and-safety/probe/probe.err`, filling WSL's disk
to 100% (618 MB free) for a few minutes. I stopped that process by its PID, after checking its
executable was my own probe binary and its parent my own command, and removed only that file; free
space went back to 13 GB (95% used). Anything else writing in WSL between about 14:12 and 14:17 may
have met "no space left on device" [unknown]. My later runs use UBSan only, with `ulimit -f` and a
timeout. Nothing I started is still running.

WSL's disk is at 95%: part 1's `~/org2/v44disp` (11 GB) and part 2's `~/org2/v44sd` (6.7 GB) are the
throw-away scratch the earlier reports already offered to remove; that is Plat's call.
