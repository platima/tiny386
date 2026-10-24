#!/usr/bin/env python3
# v3.6 to v3.7 on the Korvo-1's dual boot, without a board: the flash as
# flash-dualboot.bat COM10 (all) leaves it with the v3.6 images, then the one
# write flash-dualboot.bat COM10 app makes with the v3.7 tiny386.bin, with
# esptool write-flash's semantics as dualboot/scripts/simulate-flash.py models
# them (data padded to 4 bytes with 0xFF, every 4 KiB sector it touches erased
# first).  Reads only; writes nothing.
#
#   python3 -I appupdate.py V3 W
#     V3: the v3 folder (images and previous-v3.6/images);  W: ~/tiny386-idf-scratch/dualboot
import hashlib, os, sys

V3, W = sys.argv[1], sys.argv[2]
SIZE, SEC, MARK = 0x1000000, 0x1000, 0x5A
TEST_OFF, TEST_SIZE = 0xE20000, 0x1E0000
OLD = os.path.join(V3, "previous-v3.6", "images", "korvo1-dualboot")
NEW = os.path.join(V3, "images", "korvo1-dualboot")

def rd(p):
    with open(p, "rb") as f:
        return f.read()

def sha(b):
    return hashlib.sha256(b).hexdigest()

def apply(flash, writes):
    for addr, path in writes:
        data = rd(path)
        if len(data) % 4:
            data += b"\xff" * (4 - len(data) % 4)
        start = addr - addr % SEC
        end = -(-(addr + len(data)) // SEC) * SEC
        assert end <= SIZE
        flash[start:end] = b"\xff" * (end - start)
        flash[addr:addr + len(data)] = data
        print("    write 0x%07x %-62s %7d bytes, erased 0x%07x to 0x%07x" %
              (addr, os.path.relpath(path, V3), len(rd(path)), start, end - 1))

def changed(a, b):
    out, i = [], 0
    while i < SIZE:
        if a[i:i + SEC] != b[i:i + SEC]:
            j = i
            while j < SIZE and a[j:j + SEC] != b[j:j + SEC]:
                j += SEC
            out.append((i, j - 1))
            i = j
        else:
            i += SEC
    return out

fb = rd(os.path.join(W, "inputs", "be5b725", "flash.bin"))
flash = bytearray(fb + bytes([MARK]) * (SIZE - len(fb)))
print("Start: be5b725 flash.bin (0x%x bytes) at 0x0, marker 0x%02X from 0x%x to the end" % (len(fb), MARK, len(fb)))
print("== 1. flash-dualboot.bat COM10 (all) with the v3.6 images (previous-v3.6\\images\\korvo1-dualboot)")
apply(flash, [(0x2000, os.path.join(OLD, "bootloader.bin")), (0x8000, os.path.join(OLD, "partition-table.bin")),
              (TEST_OFF, os.path.join(OLD, "tiny386.bin"))])
before = bytes(flash)
print("== 2. flash-dualboot.bat COM10 app with the v3.7 image (images\\korvo1-dualboot\\tiny386.bin)")
apply(flash, [(TEST_OFF, os.path.join(NEW, "tiny386.bin"))])
after = bytes(flash)
ch = changed(before, after)
print("  bytes changed against step 1 (4 KiB granules):")
for a, b in ch:
    print("    0x%07x to 0x%07x" % (a, b))
inside = all(TEST_OFF <= a and b < TEST_OFF + TEST_SIZE for a, b in ch)
print("  every change inside the tiny386 partition (0xE20000 to 0xFFFFFF): %s" % inside)
print("  0x0000000 to 0x0E1FFFF (the bootloader, the table and all of Linux) the same as after step 1: %s" %
      (before[:TEST_OFF] == after[:TEST_OFF]))
for name, off, f in [("bootloader", 0x2000, "bootloader.bin"),
                     ("partition table", 0x8000, "partition-table.bin")]:
    new, old = rd(os.path.join(NEW, f)), rd(os.path.join(OLD, f))
    got = after[off:off + len(new)]
    print("  %s at 0x%x: sha256 %s; the v3.7 set's %s: %s; v3.6's: %s" % (
        name, off, sha(got)[:16], f, got == new, got == old[:len(new)] and len(old) == len(new)))
t = rd(os.path.join(NEW, "tiny386.bin"))
print("  tiny386 at 0xE20000 is the v3.7 file: %s (%d bytes, sha256 %s)" % (
    after[TEST_OFF:TEST_OFF + len(t)] == t, len(t), sha(t)[:16]))
print("  the same as writing the whole set with the v3.7 images from be5b725: ", end="")
ref = bytearray(fb + bytes([MARK]) * (SIZE - len(fb)))
import io, contextlib
with contextlib.redirect_stdout(io.StringIO()):
    apply(ref, [(0x2000, os.path.join(NEW, "bootloader.bin")), (0x8000, os.path.join(NEW, "partition-table.bin")),
                (TEST_OFF, os.path.join(NEW, "tiny386.bin"))])
print(bytes(ref) == after)
