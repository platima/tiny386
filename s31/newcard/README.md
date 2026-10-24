# A new card for the Korvo-1 💾

From v4.4 Windows 95 can run from a partition of its own on the card instead of from `win95.img`, a file on a FAT32 partition (`hda = sd:3` in an ini). This folder moves you onto a new card laid out for that, with your Linux and your Windows as they are, and leaves the old card untouched as the fallback.

- **`newcard.sh`** - run in WSL (or any Linux). Builds the new card as an image file: KORVO_SD, Linux's root, and the Windows volume taken out of `win95.img`, sector for sector. It never writes to a card or any other device.
- **`export-root.sh`** - run on the board's Linux with the old card in. Copies Linux's root partition off the old card, so that `newcard.sh` can put it on the new one.

## What the new card holds 🗂️

| Partition | What | Where | Size |
|-|-|-|-|
| p1 | FAT32 `KORVO_SD`: the `tiny386` folder; Linux's `/mnt/sd` | from sector 8192 | 512 MiB (`--p1-mib`) |
| p3 | Windows 95's own FAT16 volume (type 0E), C: for the guest | straight after p1 | the volume's own size, 400 MiB for the pristine install |
| p2 | Linux's root (ext4), copied from the old card | straight after p3 | the old root's size, or more (`--root-mib`) |

The table lists them in the order p1, p2, p3, so Linux finds its root at `/dev/mmcblk0p2` as it always has; on the card p3 comes before p2. That keeps the Windows volume near the start of the card, so the disk the guest sees stays small (it ends where p3 ends) whatever size the card is. The rest of the card is left empty.

Windows' volume is the one from inside `win95.img`, unchanged but for one field of its boot sector, the hidden-sectors count, which `newcard.sh` sets to where p3 starts. Windows 95's boot sector uses that field to find its own partition, and a PC or Linux doesn't care about it, so the same volume boots in tiny386 and mounts on a PC.

## Why a partition 🤔

- **Check and fix it like any drive.** With tiny386 off, p3 is an ordinary FAT16 drive on Windows 10/11 and on the board's Linux: `chkdsk`, `fsck.fat`, copying files to and from it. A file inside a file can't be checked that way.
- **Nothing between the guest and the card.** The guest's sectors are the card's sectors: no FAT chain, no directory entry to keep up to date, no file that can end up in pieces.
- **Safe for the rest of the card.** The guest can write only inside p3. Its disk shows p3 alone; every other sector reads as zeros and refuses writes, the card's own partition table included. tiny386 refuses `sd:1` (where its inis are), Linux's root and anything that isn't FAT16 or FAT32, and it won't start if its own FatFs has p3 mounted.

## What you need 🧰

- The new card (yours is a V30 A1 U3). With the defaults the image is about 1.1 GB; any card from 2 GB up holds it.
- WSL Debian with `python3`, `dosfstools`, `mtools` and `e2fsprogs`. Yours has them all [verified, 2026-10-24]; elsewhere `sudo apt install dosfstools mtools e2fsprogs`.
- A card reader on the PC, and the image writer you always use (Raspberry Pi Imager's "Use custom", balenaEtcher or similar).
- The old card, which stays as it is.

## The steps 🪜

1. **Linux's root off the old card.** Copy `export-root.sh` onto the old card's KORVO_SD drive (`G:\`). Put the card in the board, start Linux as usual, and run:

   ```sh
   sh /mnt/sd/export-root.sh
   ```

   (`/mnt/sd` is where the fork's Linux mounts KORVO_SD; use your own path if yours differs.) It mounts the WIN9X partition at `/mnt/win` if it isn't already, makes the root read-only for the few seconds it takes to copy (and read-write again after), and leaves `korvo-root.img`, `korvo-mbr.bin` and `korvo-export.txt` on the WIN9X drive. If it says the root couldn't be made read-only, stop whatever is writing to it, or skip to the note under step 3. Then `poweroff`, and wait for it to finish.

2. **Everything into WSL.** Card into the PC. In Explorer, make a folder `\\wsl$\Debian\home\owner\newcard` and copy in:
   - from the WIN9X drive: `korvo-root.img`, `korvo-mbr.bin` and `win95.img` (your Windows as it is now, drivers and all)
   - from the KORVO_SD drive: the whole `tiny386` folder, into `newcard\p1\`, so you have `newcard\p1\tiny386\...`
   - from this release's `card\tiny386\`: `win95-sd.ini`, into `newcard\p1\tiny386\`

   Eject the old card and put it away: it's your fallback.

3. **Build the new card.** In WSL, from this folder:

   ```sh
   cd ~/newcard
   bash /path/to/s31/newcard/newcard.sh --win95 win95.img --root korvo-root.img \
       --mbr korvo-mbr.bin --p1 p1 --out new-card.img
   ```

   It prints the layout, writes `new-card.img`, then checks it: the table read back, `fsck.fat -n` on p1, p3 against the volume in `win95.img` sector for sector, every file and folder of the volume as `mtools` lists it, p2 against the root sector for sector, and `e2fsck -fn` on p2. It ends with `new-card.img: ready` only if every check passed. `fsck.fat` says "Label '' stored in boot sector is not valid" about p3: Windows 95 leaves its volume label blank, `fsck.fat` doesn't like that, and it says the same about the volume inside `win95.img`, so `newcard.sh` only checks that p3 gets the same verdict.

   Your Linux root is stock (nothing changed since you wrote the card)? Then `--root` can be the Linux release's own card image instead of `korvo-root.img`: `newcard.sh` takes the first ext4 partition of a whole-card image.

4. **Write it to the new card** with your image writer: `new-card.img`, from `\\wsl$\Debian\home\owner\newcard\`, onto the new card. Eject it properly.

5. **Linux first.** New card into the board, start Linux as usual: it should come up exactly as before, with `/mnt/sd` holding the `tiny386` folder. `poweroff`.

6. **Then Windows from p3.** Hold VOL+ through reset for tiny386, and choose `win95-sd.ini` in the boot menu. The boot console should show `Card partition 3: FAT16, 400 MiB, at /sdcard3.` when the card is mounted, then, after the ini's settings, `Card partition 3 unmounted from /sdcard3: the guest has it whole.` and `hda: card partition 3, FAT16, 400 MiB from sector 1056768, read and written raw: the guest's disk is 916 MiB, this partition alone in its table, nothing outside it writable.` (the numbers are the default layout's). Windows starts as it did from `win95.img`. A yellow line about the hidden-sectors field means p3 didn't come from `newcard.sh`; a red one says what's wrong and the PC doesn't start.

7. **Going back** is putting the old card in. It was never written.

## Afterwards ✅

- **Shut Windows down** before switching off, as you would a PC. If you don't, Windows 10 may offer to scan the drive when the card goes into the PC: let it, or run `chkdsk X: /f` on p3's letter. Without `/f`, `chkdsk` only reads.
- **Copying files** onto p3 from the PC is fine with tiny386 off. Windows 95 sees them at its next start.
- **More room for Linux**: `--root-mib` grows p2 when the image is made (`resize2fs`). Growing p3 isn't offered: the volume stays the size it was in `win95.img`.
- **The Linux route** (tiny386 under Linux) looked for `win95.img` on the WIN9X partition; on the new card there's no such file. Its own ini would need `hda` pointed elsewhere.

## What has been checked 🔍

- [verified] `newcard.sh` with a copy of the pristine `win95.img`, a stand-in root and both p1 sizes (512 MiB and 64 MiB): every check passed (`BUILD-NOTES.md` 30.4).
- [verified] Windows 95 from p3 of both images on the PC harness (the firmware's emulator and `sd_part.c` with a card image standing in for the card), to the desktop with the same writes as from `win95.img`, the card's table, p1 and p2 unchanged, and C: in protected mode, not "MS-DOS compatibility mode" (`BUILD-NOTES.md` 30.5).
- [verified] `export-root.sh` on the PC in its test mode (`EXPORT_DEV`, files standing in for the card), under `dash` and `bash`, and `newcard.sh` taking its output.
- [unknown] `export-root.sh` on the board's Linux, writing the image with your image writer, and the new card on the board: none of these has been run.

## Licence 📄

BSD 3-clause, as the rest of the branch (`../README.md`).
