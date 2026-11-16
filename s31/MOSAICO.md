# tiny386 on the ESP-Mosaico

Windows 95 on Espressif's ESP-Mosaico. [tiny386](https://github.com/hchunhui/tiny386), the little x86 PC emulator, runs bare metal on the board's ESP32-S31, with Windows on the board's 1 Gbit NAND, the picture on its 2.16 inch 480x480 AMOLED, and the touch panel as the mouse. It is a board option (`BOARD=mosaico`) of the same firmware as the ESP32-S31-Korvo-1's ([`README.md`](README.md)).

It is for CoreBoard **V1.2** and was tested on V1.2.1. V1.0 has other pins and is not supported.

## What works

Tested on a V1.2.1 board on 16 November 2026:

- Windows 95 (OSR2.5, with Internet Explorer 4) starts from the NAND and runs at 480x480, filling the panel.
- The boot console, DOS and the boot screens, shown whole at 3/4 size or 480 columns at a time.
- The touch panel as a touchpad, and a USB keyboard and mouse through a USB-C OTG adapter.
- Sound: Windows' Sound Blaster 16 through the board's ES8311 and speaker.
- Wi-Fi, in the network build, through the guest's NE2000.
- USB disk mode: the board as a USB drive on a PC, for the card files and the Windows image.

Not done yet: the battery level, a way into USB disk mode while Windows runs (for now, switch off and use the AI button), and touch that puts the pointer under your finger.

## The board, as tiny386 uses it

| Part | On the Mosaico | Used for |
|-|-|-|
| ESP32-S31NRV16, 16 MB PSRAM | as the Korvo-1 | the emulator and the guest's 14 MB |
| 16 MB NOR flash | boot flash | the firmware, and the TINY386 FAT volume (8 MB: the ini, `bios.bin`, `vgabios.bin`) |
| GD5F1GM7UE 1 Gbit SPI NAND | GPIO20 to 25, SPI3 | the guest's hard disk, `hda = nand:` |
| CO5300 480x480 AMOLED | QSPI on SPI2, 40 MHz | the picture |
| CST9220 touch | I2C 0x5A | the mouse, as a touchpad |
| ES8311 and NS4150B | I2S, I2C 0x19, amplifier on GPIO45 | sound |
| Type-C (USB 2.0 OTG) | the chip's only USB | flashing, USB disk mode, a keyboard and mouse through an OTG adapter |
| AI button (GPIO7) | | USB disk mode at power-on; the view while running |

Pins are Espressif's, from the ESP-Mosaico BSP (`bsp/esp_mosaico.h`) and user guide.

## What you need

- An ESP-Mosaico CoreBoard V1.2, a USB-C cable, and a USB-C OTG adapter for a keyboard.
- On the PC: esptool 5.4 or later (`pip install esptool`), and an image writer. Win32 Disk Imager can save the NAND's factory contents and write the Windows image; balenaEtcher can write the image but not save.
- The firmware. There are no release binaries yet, so build it (below).
- Your own copy of Windows 95 (OSR2 or later), made into an image for the NAND (below). No Windows files are in this repository, and none can be.

## Building the firmware

In Linux or WSL, with ESP-IDF set up as [`README.md`](README.md) ("Building") describes: 188e3e55bb with the two SD card fixes in `s31/esp-idf/`. Add ESP-IDF's ec8ce190c17 too ("fix(esp32s31): temporarily disable MSPI TX underflow interrupt", from ESP-IDF's master), which stops a false PSRAM abort on the S31. The tested builds say `v6.1-dev-8145-g2c7163995d`.

```sh
git clone -b esp32-s31-mosaico https://github.com/platima/tiny386.git
cd tiny386
make prepare HOSTCC=cc
cd esp
idf.py -B build-mosaico -DBOARD=mosaico -DIDF_TARGET=esp32s31 build
idf.py -B build-mosaico-net -DBOARD=mosaico -DIDF_TARGET=esp32s31 -DTINY386_WIFI=1 build
```

`build-mosaico` is the plain build, `build-mosaico-net` the one with Wi-Fi ([`NET.md`](NET.md)). The board's components (the NAND, USB, touch and codec drivers) come from `esp/mosaico/idf_component.yml`, locked in `esp/dependencies.mosaico.lock`, so the Korvo-1's builds stay as they are.

The card files: `win95.ini` is in `s31/card-mosaico/tiny386/`, `bios.bin` in `s31/card/tiny386/`, and `vgabios.bin` comes from `s31/bios/build-bios.sh`, which builds both BIOSes from SeaBIOS's source.

## Flashing

1. **Download mode.** Switch the board off with POWER, hold BOOT, press POWER briefly, then let go of BOOT. Plug the Type-C into the PC and note the COM port in Device Manager.
2. **Save the factory firmware.** This reads the whole 16 MB NOR flash, which takes a few minutes. Keep the file: it is the only way back.
   ```sh
   esptool -c esp32s31 -p COM17 --before no-reset --after no-reset read-flash 0 0x1000000 mosaico-nor-before.bin
   ```
3. **Write tiny386**, from `esp/` (or `build-mosaico-net` for Wi-Fi):
   ```sh
   esptool -c esp32s31 -p COM17 --before no-reset --after no-reset write-flash 0x2000 build-mosaico/bootloader/bootloader.bin 0x8000 build-mosaico/partition_table/partition-table.bin 0x10000 build-mosaico/tiny386.bin
   ```

`flash-mosaico.bat` does both steps for a package folder laid out with `images\mosaico\` (and `images\mosaico-net\`) holding those three files and their `SHA256SUMS`. It skips step 2 if `backup\mosaico-nor-before.bin` is there already. In PowerShell: `.\flash-mosaico.bat COM17`, or `.\flash-mosaico.bat COM17 net` for Wi-Fi.

## The first start

1. Switch the board off and on with POWER, without holding BOOT. The first start has no ini yet, so it goes into **USB disk mode** and says so on the panel. Connect the Type-C to the PC: the panel says when the PC has set the drives up, and two drives appear.
2. **TINY386** (8 MB): first format it in Windows (right-click, Format, FAT, Quick Format), because the volume the firmware makes the first time can look full to Windows. Then copy a `tiny386` folder onto it holding `win95.ini`, `bios.bin` and `vgabios.bin`.
3. **The NAND** (142,606,336 bytes, read only): the chip as it came, with the factory files. Save it with Win32 Disk Imager's Read, as `mosaico-nand-factory.bin`, and keep it with the NOR backup. If Windows offers to format this drive, click Cancel. Never format it.
4. **Set up the NAND.** Eject both drives on the PC. Hold the AI button for 5 seconds, until the panel says "Let go now to erase the NAND and set it up", then let go. It erases the NAND (about 4 seconds) and comes back to USB disk mode, now with the NAND as **the Windows disk** (119,279,616 bytes).
5. **Write Windows.** Write your image onto that drive with the image writer. If Windows offers to format it, click Cancel. Once it is written, the PC can open the image's C: drive too.
6. **Run.** Eject both drives, then press the AI button briefly: tiny386 starts with `win95.ini`.

## Using it

- **The picture.** Windows at 480x480 fills the panel. Wider pictures (640x480 Windows, DOS, the boot screens) are shown whole at 3/4 size, or 480 of their 640 columns following the mouse pointer: a short press of the AI button, or Ctrl+Alt+F7, changes between the two. The panel's corners are rounded, so the very corners of the picture are hidden.
- **Touch** works as a touchpad, as on a laptop: move a finger to move the pointer, tap to click, tap with two fingers to right-click, and tap, then touch again and move, to drag. The pointer moves by how far your finger moves, so it does not sit under your finger.
- **A keyboard and mouse:** USB, through a USB-C OTG adapter. While it hosts USB, the board powers the keyboard itself, from its 65 mAh battery unless 5 V comes in on the 5V pad on the back or a header's 5V_IN. For more than a few minutes, power the board that way.
- **Switching off:** shut Windows down first (Start, Shut Down) and wait for "It's now safe to turn off your computer". Writes to the NAND can still be waiting until then.
- **USB disk mode again:** switch off, hold the AI button, and press POWER. To start Windows from there, eject both drives on the PC and press AI briefly.
- **The boot console** stays up 2 seconds (`console` under `[esp]` in the ini), or 10 seconds when it shows a warning or an error, so it can be read. Ctrl+Alt+F12 shows it again while Windows runs, with the speed.

## Wi-Fi

Flash the network build, then add these under `[esp]` in `tiny386\win95.ini` on the TINY386 drive:

```ini
ssid = your network's name
pass = its password
```

It needs a 2.4 GHz network. At start-up it joins and waits up to 10 seconds for an address, then starts the PC either way. Windows sees an NE2000 at I/O 300h, IRQ 9. [`NET.md`](NET.md) has the details.

## Making the Windows 95 image

You need Linux or WSL with Python 3 and mtools, and a Windows 95 install (OSR2 or later, on FAT16) that already runs under tiny386, copied out as a folder of its C: drive's files. That install needs VMDisp9x 1.2025.0.119's QEMU display driver, `QEMUMINI.DRV` ([`TEST-PLAN.md`](TEST-PLAN.md) section 13, with the pointer fix in [`qemumini-pointer/`](qemumini-pointer/)), and its Sound Blaster 16 driver.

1. **Make it small.** The disk is 113.75 MiB, and the fuller the NAND, the slower its writes. The image tested here went from 106 MB to 71 MB, losing most fonts and screensavers, Java, RealPlayer, IE4's tour and Connection Wizard, and the sound files. Some things must stay:
   - `WINDOWS\SYSTEM\JAVASUP.VXD`: the registry loads it at every start, even with Java gone.
   - `WINDOWS\SYSBCKUP\OLE2.DLL` and `STORAGE.DLL`: without them, a "System File Error" at every start.
   - the swap file (Windows' default): with 14 MB of memory, Explorer fails without it.
   - in `WINDOWS\MEDIA`, the sounds you want, such as `The Microsoft Sound.wav` and `DING.WAV`.
2. **No logon box, and 480x480.** Copy [`mosaico-win95/AUTOEXEC.BAT`](mosaico-win95/AUTOEXEC.BAT) and [`mosaico-win95/MOSAICO.REG`](mosaico-win95/MOSAICO.REG) into the top of the folder (if your install has an `AUTOEXEC.BAT`, add these lines to it). At the first start, DOS's REGEDIT puts `MOSAICO.REG` into the registry once and deletes it: Windows then starts without the network logon box, at 480x480 in 16-bit colour.
3. **The display driver at 480x480.** VMDisp9x, as released, turns any size under 640 wide into 640x480. This changes that one constant in the folder's `WINDOWS\SYSTEM\QEMUMINI.DRV`, only for the 1.2025.0.119 driver (it checks), and keeps the original in the holding folder:
   ```sh
   python3 s31/qemumini-480.py WIN95_FOLDER HOLDING_FOLDER
   ```
4. **The start-up logo** (optional), drawn in tiny386's own VGA font:
   ```sh
   python3 s31/mklogo-mosaico.py vgafont.c WIN95_FOLDER/LOGO.SYS
   ```
5. **The image:**
   ```sh
   python3 s31/mkimage-mosaico.py --windows WIN95_FOLDER --boot win95.img --out mosaico-win95.img
   ```
   `--boot` is an existing Windows 95 disk image to take the boot sector's code and `IO.SYS` from. Without one, use `--vbr WIN95_FOLDER/WINDOWS/COMMAND/SYS.COM --io-sys WINBOOT.SYS`, with `WINBOOT.SYS` taken from the setup files' `PRECOPY1.CAB` (7-Zip opens it). If the folder is on Windows, `--attrs` keeps the files' DOS attributes; `python3 s31/mkimage-mosaico.py --help` and the script's first lines say how.

The image is the NAND disk's exact size: tiny386's own boot record, one FAT16 partition with 2 KiB clusters lined up with the NAND's pages, and its free space as zeros, which the NAND does not store. Write it in USB disk mode (step 5 of the first start).

## Going back to the factory firmware

1. Start USB disk mode (hold AI and press POWER). Hold AI 5 seconds for **restore mode**: the NAND drive is the raw chip again, and writable. Write `mosaico-nand-factory.bin` onto it with the image writer. The panel counts the blocks: all 1,024 done is the whole chip.
2. Put the board in download mode, then write the saved NOR flash back: `restore-mosaico.bat COM17`, or
   ```sh
   esptool -c esp32s31 -p COM17 --before no-reset --after no-reset write-flash 0 mosaico-nor-before.bin
   ```

## If something is wrong

- **A black panel and no USB drives:** look at UART0's output (TX0 is GPIO58 on the right module header, 115200 baud) with a 3.3 V USB serial adapter.
- **A red crash screen:** photograph it. It is also shown again at the next start.
- **The boot console waits 10 seconds:** it shows a warning or an error. Read it: that is the thing to fix.
- **Writes get slow:** the NAND is getting full. Its map has to move old pages before it can write, and the fuller it is, the more it moves. Keep the image small, and Windows' swap file at a fixed size if it grows.

## Where it is in the code

| File | What |
|-|-|
| `esp/main/board_mosaico.h` | the board's settings and pins |
| `esp/main/mosaico.c` | start-up, the AI button, Ctrl+Alt+F7 |
| `esp/main/lcd_mosaico.c` | the panel: the 640x480 frame to the CO5300 |
| `esp/main/nand_vol.c`, `nand_disk.c` | the NAND disk, and the chip raw for backup and restore |
| `esp/main/usb_disk.c` | USB disk mode |
| `esp/main/touch_mosaico.c` | the touchpad |
| `esp/sdkconfig.mosaico`, `esp/sdkconfig.mosaico-wifi`, `esp/partitions-mosaico.csv`, `esp/mosaico/` | the build: settings, the NOR layout, components |
| `s31/card-mosaico/`, `s31/mosaico-win95/` | the ini, and the Windows set-up files |
| `s31/mkimage-mosaico.py`, `qemumini-480.py`, `mklogo-mosaico.py` | the Windows image |
| `s31/flash-mosaico.bat`, `restore-mosaico.bat` | flashing a package, and going back |
| `tests/host/nand` | the NAND disk layer, tested on the PC |
