# tiny386 on the ESP-Mosaico

The same bare-metal tiny386 as the Korvo-1's, as a board option of this port (`BOARD=mosaico`), for Espressif's ESP-Mosaico, CoreBoard **V1.2** (V1.0 has other pins and is not supported). Windows 95 runs from the board's 1 Gbit NAND, on its 2.16 inch 480x480 AMOLED, with the touch panel as the mouse.

Test 1 has not run on a board yet. Everything below is how it is meant to work.

## The board, as tiny386 uses it

| Part | On the Mosaico | Used for |
|-|-|-|
| ESP32-S31NRV16, 16 MB PSRAM | as the Korvo-1 | the emulator and the guest's 14 MB |
| 16 MB NOR flash | boot flash | the firmware, and the TINY386 FAT volume (8 MB: inis, `bios.bin`, `vgabios.bin`) |
| GD5F1GM7UE 1 Gbit SPI NAND | GPIO20 to 25, SPI3 | the guest's hard disk, `hda = nand:` |
| CO5300 480x480 AMOLED | QSPI on SPI2, 40 MHz | the picture |
| CST9220 touch | I2C 0x5A | the mouse, as a touchpad |
| ES8311 and NS4150B | I2S, I2C 0x19 | sound |
| Type-C (USB 2.0 OTG) | the chip's only USB | flashing, USB disk mode, a keyboard and mouse through an OTG adapter |
| AI button (GPIO7) | | USB disk mode at power-on; the view while running |

Pins are Espressif's, from the ESP-Mosaico BSP (`bsp/esp_mosaico.h`) and user guide.

## The NAND as the Windows disk

The disk is a fixed **119,279,616 bytes** (113.75 MiB, 232,968 sectors). Your image must be that size or smaller; a smaller one leaves the rest unused. Under it is ESP-IDF's `spi_nand_flash` with its Dhara map, which spreads the writes and steps round bad blocks. Pages of zeros are not stored, so the free space inside an image costs nothing until Windows writes there.

Writes get slower as the NAND fills with data, because the map has to move old pages first. Two things keep them quick: less in the image (the free space inside it is free on the NAND too), and Windows' swap file turned off or set to a fixed size (Control Panel, System, Performance, Virtual Memory).

## The picture

The frame is 640x480; the panel shows 480x480 of it:

- **Windows at 480x480** fills the panel 1:1. Windows' display driver (VMDisp9x) refuses sizes under 640 wide as it is released; a one-constant change to your copy of it will come in a later test.
- **Windows at 640x480** (and DOS, and the boot screens): the AI button (a short press) or Ctrl+Alt+F7 changes between the whole picture at 3/4 (480x360, softened) and 480 of its 640 columns 1:1, following the mouse pointer.
- The panel's corners are rounded, so the very corners of the picture are hidden.

## Touch

The panel is a touchpad: move a finger to move the pointer; tap to click; tap with two fingers to right-click; tap, then touch again and move, to drag.

## A keyboard

A USB keyboard (and mouse) through a USB-C OTG adapter, as on the Korvo-1. While it hosts USB the board gives the keyboard its 5 V itself, from its 65 mAh battery unless 5 V comes in through the 5V pad on the back or a header's 5V_IN. So for more than a few minutes, power the board that way.

## Steps

You need esptool 5.4 or later (`pip install esptool`) and an image writer such as Win32 Disk Imager.

1. **Download mode.** Switch the board off (POWER), hold BOOT, press POWER briefly, let go of BOOT. Plug the Type-C into the PC and note the COM port.
2. **Flash.** In this folder: `flash-mosaico.bat COM12` (your port). It saves the whole 16 MB NOR flash to `backup\mosaico-nor-before.bin` first, which takes a few minutes, and then writes tiny386. Keep that `backup` folder. With `net` after the port it writes the network build (see `NET.md`; `[esp] ssid` and `pass` in the ini).
3. **Start.** Switch off and on with POWER. The first start has no ini yet, so it goes into **USB disk mode** by itself and says so on the panel. The PC sees two drives:
   - **TINY386** (8 MB): copy this package's `card\tiny386` folder onto it.
   - **The NAND**, read only, 142,606,336 bytes: the chip as it came, with the factory files. **Save it** with the image writer's Read, as `mosaico-nand-factory.bin`, and keep it with the NOR backup. If Windows offers to format this drive, say Cancel. Never format it.
4. **Set the NAND up.** Eject both drives on the PC. Hold the AI button for 5 seconds, until the panel says "Let go now to erase the NAND and set it up", then let go. It erases the NAND (about 4 s) and comes back to USB disk mode, now with the NAND as **the Windows disk** (119,279,616 bytes).
5. **Write Windows.** Write your image onto that drive with the image writer's Write. Windows may offer to format it first: Cancel. Once written, the PC can open its C: drive too.
6. **Run.** Eject both drives, then press the AI button briefly: tiny386 starts with `win95.ini`.

USB disk mode again later: hold the AI button while switching on.

## Going back to the factory firmware

1. USB disk mode (hold AI while switching on). Hold AI 5 s for **restore mode**: the NAND drive is the raw chip again, writable. Write `mosaico-nand-factory.bin` onto it with the image writer. The panel counts the blocks; all 1,024 done is the whole chip.
2. Download mode (step 1 above), then `restore-mosaico.bat COM12`: the saved 16 MB NOR flash goes back.

## What to tell me

- Photos of the panel at each step (USB disk mode's lines, the boot console, Windows).
- Whether the picture is right: colours, nothing shifted, the 3/4 view readable.
- How long writing the image took, and the bad block count USB disk mode shows.
- Time to the Windows desktop; whether touch, the keyboard and sound work.
- If it stops: a photo, and UART0's output if you can (TX0 GPIO58, RX0 GPIO59 on the right module header, 115200).

## Where it is in the code

| File | What |
|-|-|
| `esp/main/board_mosaico.h` | the board's settings and pins |
| `esp/main/lcd_mosaico.c` | the panel: the 640x480 frame to the CO5300 |
| `esp/main/nand_vol.c`, `nand_disk.c` | the NAND disk; the chip raw for backup and restore |
| `esp/main/usb_disk.c` | USB disk mode |
| `esp/main/touch_mosaico.c` | the touchpad |
| `esp/main/mosaico.c` | start-up, the AI button, Ctrl+Alt+F7 |
| `esp/sdkconfig.mosaico`, `esp/partitions-mosaico.csv`, `esp/mosaico/` | the build: settings, the NOR layout, components |
| `tests/host/nand` | the NAND disk layer tested on the PC |

Build: `idf.py -B build-mosaico -DBOARD=mosaico -DIDF_TARGET=esp32s31 build` in `esp/` (after `make prepare`), with the same ESP-IDF as the Korvo-1 (`s31/README.md`).
