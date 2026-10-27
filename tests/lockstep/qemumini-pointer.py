#!/usr/bin/env python3
"""tests/lockstep: Plat's one-byte change to VMDisp9x 1.2025.0.119's
QEMUMINI.DRV, made in a COPY of a Windows 95 hard disk image, as
s31/qemumini-pointer/qemumini-pointer.ps1 makes it on his card's image.

    python3 -I qemumini-pointer.py IMAGE

The driver as released draws the mouse pointer itself; one line of its
source (init.c 207, "mouse_vxd = TRUE;") is the six bytes C7 06 B8 06 01 00
at offset 0x2064 of the file, and making the 01 at 0x2068 a 00 leaves the
pointer to Windows' DIB engine (s31/qemumini-pointer/README.txt).

The checks are the PowerShell script's: the first partition of IMAGE's MBR
holds a FAT12, FAT16 or FAT32 volume; C:\\WINDOWS\\SYSTEM\\QEMUMINI.DRV is
found by name through the directories and the FAT (never by its bytes); it
is 15,534 bytes with the released file's SHA-256 (or the changed one's, and
then nothing is written); the six bytes are where they should be.  The byte
is written in place, in the cluster that holds it (the directory entry is
not touched), and the file is read back through the FAT: its SHA-256 must
then be the changed file's.

Exit status 0: changed, or already changed; 1: refused or failed; 2: not
found.  This script carries none of the driver's bytes but the six it looks
for.  VMDisp9x is Jaroslav Hensl's (github.com/JHRobotics/vmdisp9x, MIT).
"""
import hashlib
import struct
import sys

SIZE = 15534
SHA_ORIG = '0b3584b11420b3bc96652d9ae147df2188572e0d1b967a098ac8513eb97b713d'
SHA_DONE = 'e4c47e9aa983db1cebbeb09e625c53e71986e78868b2ff9094a22abfec0538e9'
AT = 0x2064
LOOK = bytes([0xC7, 0x06, 0xB8, 0x06])   # mov word ptr [06B8h], then 01 00
BYTE = AT + 4
PATH = ('WINDOWS', 'SYSTEM', 'QEMUMINI.DRV')


def fail(msg, code=1):
    print('qemumini-pointer: ' + msg)
    sys.exit(code)


class Fat:
    def __init__(self, f, off):
        self.f = f
        self.off = off
        s = self.read(off, 512)
        if s[510:512] != b'\x55\xaa' or s[0] not in (0xEB, 0xE9):
            fail('no FAT boot sector at %d' % off)
        bps, spc, rsvd, nfats, roots, tot16, _, fat16 = struct.unpack('<HBHBHHBH', s[11:24])
        tot = tot16 or struct.unpack('<I', s[32:36])[0]
        fatsz = fat16
        self.rootclus = 0
        if fatsz == 0:
            fatsz, = struct.unpack('<I', s[36:40])
            self.rootclus, = struct.unpack('<I', s[44:48])
        if bps not in (512, 1024, 2048, 4096) or spc < 1 or spc & (spc - 1) or nfats < 1 or rsvd < 1:
            fail('not a FAT volume I know')
        self.bps, self.spc, self.rsvd = bps, spc, rsvd
        self.rootsecs = (roots * 32 + bps - 1) // bps
        self.roots = roots
        self.fatoff = off + rsvd * bps
        self.rootoff = self.fatoff + nfats * fatsz * bps
        self.data = rsvd + nfats * fatsz + self.rootsecs
        clusters = (tot - self.data) // spc
        self.kind = 12 if clusters < 4085 else 16 if clusters < 65525 else 32
        self.cbytes = bps * spc

    def read(self, at, n):
        self.f.seek(at)
        b = self.f.read(n)
        if len(b) != n:
            fail('the image ends at %d' % (at + len(b)))
        return b

    def next(self, c):
        if self.kind == 12:
            x, = struct.unpack('<H', self.read(self.fatoff + c + c // 2, 2))
            x = x >> 4 if c & 1 else x & 0xFFF
            return -1 if x >= 0xFF8 else x
        if self.kind == 16:
            x, = struct.unpack('<H', self.read(self.fatoff + 2 * c, 2))
            return -1 if x >= 0xFFF8 else x
        x = struct.unpack('<I', self.read(self.fatoff + 4 * c, 4))[0] & 0x0FFFFFFF
        return -1 if x >= 0x0FFFFFF8 else x

    def coff(self, c):   # the image offset of cluster c
        return self.off + (self.data + (c - 2) * self.spc) * self.bps

    def chain(self, c):
        out = []
        while c >= 2 and c != -1:
            if len(out) > 1 << 22:
                fail('a FAT chain that does not end')
            out.append(c)
            c = self.next(c)
        return out

    def entries(self, clus):   # a directory's 32-byte entries (clus 0: FAT12/16 root)
        if clus == 0 and self.kind != 32:
            raw = self.read(self.rootoff, self.roots * 32)
        else:
            raw = b''.join(self.read(self.coff(c), self.cbytes)
                           for c in self.chain(clus or self.rootclus))
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                return
            if e[0] == 0xE5 or e[11] == 0x0F or e[11] & 0x08:
                continue
            name = e[0:8].decode('latin-1').rstrip()
            ext = e[8:11].decode('latin-1').rstrip()
            first = struct.unpack('<H', e[26:28])[0]
            if self.kind == 32:
                first |= struct.unpack('<H', e[20:22])[0] << 16
            size, = struct.unpack('<I', e[28:32])
            yield (name + ('.' + ext if ext else '')).upper(), e[11], first, size


def find(fat):
    clus = 0
    for i, part in enumerate(PATH):
        for name, attr, c, size in fat.entries(clus):
            if name == part:
                last = i == len(PATH) - 1
                if bool(attr & 0x10) == last:
                    fail('%s is not a %s' % (part, 'file' if last else 'directory'), 2)
                clus = c
                break
        else:
            fail('no %s' % '\\'.join(PATH[:i + 1]), 2)
    return clus, size


def content(fat, clus, size):
    chain = fat.chain(clus)
    if len(chain) * fat.cbytes < size:
        fail('QEMUMINI.DRV\'s clusters hold less than its size')
    data = b''.join(fat.read(fat.coff(c), fat.cbytes) for c in chain)[:size]
    return data, chain


def main():
    if len(sys.argv) != 2:
        print('usage: qemumini-pointer.py IMAGE')
        sys.exit(1)
    with open(sys.argv[1], 'r+b') as f:
        mbr = f.read(512)
        if len(mbr) != 512 or mbr[510:512] != b'\x55\xaa':
            fail('no MBR')
        start, = struct.unpack('<I', mbr[454:458])
        fat = Fat(f, start * 512)
        clus, size = find(fat)
        if size != SIZE:
            fail('QEMUMINI.DRV is %d bytes, not %d' % (size, SIZE))
        data, chain = content(fat, clus, size)
        sha = hashlib.sha256(data).hexdigest()
        if sha == SHA_DONE:
            print('qemumini-pointer: already changed (SHA-256 %s); nothing written' % sha[:16])
            return
        if sha != SHA_ORIG:
            fail('QEMUMINI.DRV is not the released 1.2025.0.119 file (SHA-256 %s)' % sha[:16])
        if data[AT:AT + 4] != LOOK or data[BYTE:BYTE + 2] != b'\x01\x00':
            fail('the six bytes are not at 0x%x' % AT)
        at = fat.coff(chain[BYTE // fat.cbytes]) + BYTE % fat.cbytes
        f.seek(at)
        f.write(b'\x00')
        f.flush()
        data, _ = content(fat, clus, size)
        sha = hashlib.sha256(data).hexdigest()
        if sha != SHA_DONE:
            fail('read back, QEMUMINI.DRV has SHA-256 %s, not the changed file\'s: '
                 'the image copy is now wrong' % sha[:16])
        print('qemumini-pointer: C:\\WINDOWS\\SYSTEM\\QEMUMINI.DRV changed at 0x%x (image offset %d), '
              'SHA-256 now %s' % (BYTE, at, sha[:16]))


main()
