# Plat's observations on the 800x480 flicker (v4.1 on the Korvo-1)

This file collects what Plat reports from the board. Agents working on the flicker re-read it before each
major decision; new entries are added at the bottom with the time.

## 2026-10-23, first report

- Firmware v4.1 (dual-boot, app only flashed), Windows 95 with VMDisp9x qemumini at 800x480 in 16-bit
  (win95-800x480.ini, vga_mem_size 768K as shipped).
- Plat: "800x480 at 16-bit is flickering a LOT. I don't recall it flickering previously." Previously means
  Windows at 640x480 in 16 colours with the standard VGA driver, on v3.9 (and v3.6).
- Not yet known: what the flicker looks like (whole screen black, parts of the screen, tearing lines),
  whether a still desktop flickers or only when something changes, whether vbe_blocks = 0 changes it, and
  whether 640x480 in 16 colours on v4.1 flickers. Plat has been asked.

## 2026-10-23, second report

- Plat: "the drawing/redrawing definitely appears a bit odd."
- Plat: it is "definitely slower than the previous resolution at 16 colour. Or at least I'm quite certain."
  (Previous: 640x480 in 16 colours, standard VGA driver.) So the fix should also look at speed: the guest
  moves about five times the bytes at 800x480 in 16-bit (2 bytes a pixel, 25 per cent more pixels) than at
  640x480 in 16 colours (half a byte a pixel, planar), and v4.0 added marking and a fence to each 16-bit store.
  Measure where the time goes (guest work against emulator overhead against core 0 drawing) and say what is
  inherent and what is ours.
- Answers to the four questions (what the flicker looks like, still desktop or not, vbe_blocks = 0, 640x480 on
  v4.1) not yet given.
- Plat also asked for a Windows 95 test app showing FPS, CPU usage and the related performance figures; that is
  a separate piece of work, not this fix.

## 2026-10-23, third report (the most useful so far)

- vbe_blocks: untested; Plat is on the default (1). Changing it means pulling the card, which he finds a pain.
- It did not flicker (that he noticed) until VBE480.REG was imported and Windows restarted at 800x480.
- A STILL DESKTOP DOES NOT FLICKER. It is MOTION: the mouse pointer and anything moving. Plat: "it's motion -
  like tearing, but it does not look like normal frame tearing in modern systems."
- 256 colours (same driver, changed live without a restart) does it too.
- 640x480 through the same driver (changed live, no restart) still flickers.
- Changing to 16 colours forced a restart (the standard VGA path, mode 12h planar): NOT flickering, "fairly
  smooth". 800x480 is not offered in 16 colours, so that comparison is not possible.
- So: every VBE mode shows it on motion, at any depth and at 640x480 too; the planar 16-colour path does not.
  Leads this suggests: pictures taken in the middle of a guest update (the software pointer erased and not yet
  redrawn, a window half moved), with the 1 KiB blocks not lining up with rows so the partial update looks blocky
  rather than like a straight tear line; the standard VGA driver may wait for vertical retrace before moving the
  pointer (check VGA_RETRACE_REALTIME and whether the planar path's pictures are timed against the guest's retrace)
  while qemumini does not. Possible fixes to weigh: take the VBE picture only when the guest has not written video
  memory for a short while (bounded, so the screen still updates during long redraws), snapshot each changed block
  in one go so a block is never half old and half new, or align pictures with the emulated retrace.

## Plat's request for the fix release

- Make settings changeable without pulling the card: at least a runtime toggle for vbe_blocks (a hotkey, or a
  small settings menu from the Ctrl+Alt+F12 overlay) that takes effect at once, ideally with a way to save the
  choice back to the ini on the card. Until then, the ini can be edited from Linux on the dual boot
  (/mnt/sd/tiny386/*.ini, then sync and reboot).
