#!/usr/bin/env python3
# vbe480check.py RUNDIR SCREEN_W SCREEN_H
#
# Checks a harness run of BVBE480.COM (bvbe480.S, tiny386-idf v4.0; v4.1): for
# each mode, whether the VGA BIOS listed it as it should for the screen's
# width and the video memory, and each picture the program drew, pixel for
# pixel, against the same picture worked out here from the program's
# patterns and stores, as the board's vga.c would put it on the screen
# (RGB565: 1:1 at the screen's size, centred if smaller, shrunk to fit if
# larger).  The harness's pictures (shot-NAME.png) are RGB565 widened to 8
# bits a colour as save_ppm() does it.  Also that every picture was the same
# as a full redraw ("N pixels differ from a full redraw", 0).
#
# Prints one line a picture and a summary; exit status 0 if all is well.
import re, sys, os
import numpy as np
from PIL import Image

run, SW, SH = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
log = open(os.path.join(run, 'log.txt'), errors='replace').read().splitlines()

# v4.1: a guest line split by one of the harness's own lines is put back
# together ("@info[harness] beat ..." then " m0143.listed 0"): the harness
# of v4.0 wrote its lines between the bytes of the BIOS's debug port (the
# v4.0 review's N1; v4.1's harness writes the guest's output a whole line
# at a time)
joined, carry = [], ''
for line in log:
    i = line.find('[harness]')
    if i > 0:
        carry += line[:i]
        joined.append(line[i:])
        continue
    if carry and not line.startswith('[harness]'):
        line, carry = carry + line, ''
    joined.append(line)
log = joined

mem = None
modes = {}
order = []
redraw_diff = {}
for line in log:
    m = re.match(r'@info vbe480\.memory-64k ([0-9a-f]+)', line)
    if m:
        mem = int(m.group(1), 16) * 65536
    m = re.match(r'@info m([0-9a-f]{4})\.(\S+) ?(.*)', line)
    if m:
        mode = int(m.group(1), 16)
        d = modes.setdefault(mode, {})
        if mode not in order:
            order.append(mode)
        d[m.group(2)] = m.group(3).strip()
    m = re.match(r'\[harness\] shot (\S+) at t=\S+: (\d+) pixels differ', line)
    if m:
        redraw_diff[m.group(1)] = int(m.group(2))

TABLE = {
    0x1a0: (800, 480, 8), 0x1a1: (800, 480, 15), 0x1a2: (800, 480, 16),
    0x1a3: (800, 480, 24), 0x1a4: (800, 480, 32),
    0x103: (800, 600, 8), 0x113: (800, 600, 15), 0x114: (800, 600, 16),
    0x115: (800, 600, 24), 0x143: (800, 600, 32),
    0x101: (640, 480, 8), 0x110: (640, 480, 15), 0x111: (640, 480, 16),
    0x112: (640, 480, 24), 0x142: (640, 480, 32),
    0x146: (320, 200, 8), 0x10e: (320, 200, 16),
}

def bytes_of(bpp):
    return (bpp + 7) // 8

def palette(sel):
    i = np.arange(256)
    if sel == 1:
        r, g, b = i & 63, (3 * i) & 63, (7 * i) & 63
    else:
        r, g, b = (5 * i) & 63, (i >> 2) & 63, (255 - i) & 63
    # vga.c's update_palette256() with a 6-bit DAC
    return ((b >> 1) | (g << 5) | ((r >> 1) << 11)).astype(np.uint32)

def pattern(w, h, bpp, s, nlines, bpl):
    """lines 0..nlines-1 of the pattern, laid out bpl bytes a line"""
    out = np.zeros((nlines, bpl), dtype=np.uint8)
    x = np.arange(w, dtype=np.int64)
    for y in range(nlines):
        if bpp == 8:
            px = ((x + 2 * y + 77 * s) & 255).astype(np.uint8)
            row = px
        elif bpp in (15, 16):
            v = (33 * x + 1031 * y + 12345 * s) & 0xffff
            if bpp == 15:
                v = (v & 0x7fff) | ((x & 1) << 15)
            row = np.empty(2 * w, dtype=np.uint8)
            row[0::2] = v & 255
            row[1::2] = v >> 8
        else:
            n = bytes_of(bpp)
            row = np.empty(n * w, dtype=np.uint8)
            row[0::n] = (x ^ y) & 255
            row[1::n] = y & 255
            row[2::n] = (x + 64 * s) & 255
            if n == 4:
                row[3::n] = (3 * x + y) & 255
        row = row.copy()
        n = bytes_of(bpp)
        white = np.full(n, 255, dtype=np.uint8)
        if bpp == 15:
            white[1] = 0x7f
        if y == 0 or y == h - 1:
            row[:] = np.tile(white, w)
        else:
            row[0:n] = white
            row[(w - 1) * n:w * n] = white
        out[y, :len(row)] = row
    return out.reshape(-1)

def put(vram, addr, size, val):
    for k in range(size):
        vram[addr + k] = (val >> (8 * k)) & 255

def lfb_pokes(vram, h, bpl):
    y = h // 4
    for k in range(37):
        put(vram, y * bpl + 7 * k, 1, (29 * k + 5) & 255)
    y = h // 2
    for k in range(23):
        put(vram, y * bpl + 2 + 5 * k, 2, (0x1234 + 0x0f0f * k) & 0xffff)
    y = (h * 3 & 0xffff) // 4
    for k in range(19):
        put(vram, y * bpl + 1 + 13 * k, 4, 0x89abcdef ^ ((k * 0x01010101) & 0xffffffff))
    y = h - 2
    for k in range(50):
        put(vram, y * bpl + 40 + 4 * k, 4, 0x5a3c96f0)

def win_pokes(vram, h, bpl):
    def poke(y, off, size, val):
        a = y * bpl + off
        if (a & 0xffff) + size > 0x10000:
            return
        put(vram, a, size, val)
    for k in range(31):
        poke(h // 5, 5 + 11 * k, 1, (41 * k + 3) & 255)
    for k in range(21):
        poke(2 * h // 5, 1 + 7 * k, 2, (0xa5a5 + 0x0123 * k) & 0xffff)
    for k in range(16):
        poke(3 * h // 5, 4 + 17 * k, 4, (0x13579bdf + 0x02040810 * k) & 0xffffffff)

def to565(vram, start, w, h, bpp, bpl, pal):
    """the picture's h lines from byte start, as RGB565 (h x w)"""
    n = bytes_of(bpp)
    out = np.zeros((h, w), dtype=np.uint32)
    for y in range(h):
        a = start + y * bpl
        line = vram[a:a + n * w].astype(np.uint32)
        if bpp == 8:
            out[y] = pal[line]
        elif bpp in (15, 16):
            v = line[0::2] | (line[1::2] << 8)
            if bpp == 15:
                v = (v & 0x1f) | ((v & 0x7fe0) << 1)
            out[y] = v
        else:
            b, g, r = line[0::n], line[1::n], line[2::n]
            out[y] = (b >> 3) | ((g >> 2) << 5) | ((r >> 3) << 11)
    return out

def screen(pic, w, h):
    """the picture on the screen, as vga.c places it"""
    scr = np.zeros((SH, SW), dtype=np.uint32)
    if w <= SW and h <= SH:
        ox, oy = (SW - w) // 2, (SH - h) // 2
        scr[oy:oy + h, ox:ox + w] = pic
        return scr
    if SW * h <= SH * w:
        sw, sh = SW, h * SW // w
    else:
        sh, sw = SH, w * SH // h
    ox, oy = (SW - sw) // 2, (SH - sh) // 2
    xs = np.arange(sw) * w // sw
    ys = np.arange(sh) * h // sh
    scr[oy:oy + sh, ox:ox + sw] = pic[ys][:, xs]
    return scr

def widen(scr):
    r = ((scr >> 11) & 31) * 255 // 31
    g = ((scr >> 5) & 63) * 255 // 63
    b = (scr & 31) * 255 // 31
    return np.stack([r, g, b], axis=-1).astype(np.uint8)

bad = 0
pics = 0
lines = []
if mem is None:
    print('no memory size in the log')
    sys.exit(1)
for mode in TABLE:
    w, h, bpp = TABLE[mode]
    d = modes.get(mode, {})
    # bochsvga_setup(): wider than the emulator's width (800), deeper than
    # 32 bits or more than video memory: removed
    want_listed = w <= 800 and w * h * bytes_of(bpp) <= mem
    got = d.get('listed')
    if got is None:
        lines.append('m%04x: not looked at' % mode)
        bad += 1
        continue
    if (got == '1') != want_listed:
        lines.append('m%04x: listed %s, should be %s' % (mode, got, int(want_listed)))
        bad += 1
        continue
    if got != '1':
        lines.append('m%04x %dx%dx%d: not listed (%d bytes of video memory), as it should be'
                     % (mode, w, h, bpp, mem))
        continue
    if 'mode' not in d or 'lines' not in d:
        lines.append('m%04x: the program did not get through it' % mode)
        bad += 1
        continue
    f = d['mode'].split()
    mw, mh, mbpp, mbpl = int(f[0], 16), int(f[1], 16), int(f[2], 16), int(f[3], 16)
    if (mw, mh, mbpp) != (w, h, bpp) or mbpl != w * bytes_of(bpp):
        lines.append('m%04x: 4F01h says %dx%dx%d, %d bytes a line' % (mode, mw, mh, mbpp, mbpl))
        bad += 1
        continue
    for k in ('4f01', '4f02'):
        if d.get(k) != '004f':
            lines.append('m%04x: %s gave %s' % (mode, k, d.get(k)))
            bad += 1
    bpl = mbpl
    pal1, pal2 = palette(1), palette(2)
    vram = np.zeros(mem + 16384, dtype=np.uint8)
    expect = {}
    # A: cleared by the mode set, pattern A, lines 0..h-1
    vram[:h * bpl] = 0
    vram[:h * bpl] = pattern(w, h, bpp, 0, h, bpl)
    expect['lfb'] = screen(to565(vram, 0, w, h, bpp, bpl, pal1), w, h)
    lfb_pokes(vram, h, bpl)
    expect['poke'] = screen(to565(vram, 0, w, h, bpp, bpl, pal1), w, h)
    # B: cleared again, pattern B, nl lines
    nl = int(d['lines'], 16)
    vram[:h * bpl] = 0
    vram[:nl * bpl] = pattern(w, h, bpp, 1, nl, bpl)
    win_pokes(vram, h, bpl)
    expect['win'] = screen(to565(vram, 0, w, h, bpp, bpl, pal1), w, h)
    if nl > h:
        expect['start16'] = screen(to565(vram, 16 * bpl, w, h, bpp, bpl, pal1), w, h)
    if bpp == 8:
        expect['pal2'] = screen(to565(vram, 0, w, h, bpp, bpl, pal2), w, h)
    for name, scr in expect.items():
        shot = 'm%04x-%s' % (mode, name)
        path = os.path.join(run, 'shot-%s.png' % shot)
        pics += 1
        if not os.path.exists(path):
            lines.append('%s: no picture' % shot)
            bad += 1
            continue
        img = np.asarray(Image.open(path).convert('RGB'))
        want_img = widen(scr)
        diff = np.any(img != want_img, axis=-1)
        n = int(diff.sum())
        rd = redraw_diff.get(shot)
        ok = n == 0 and rd == 0
        if n:
            ys, xs = np.nonzero(diff)
            where = ' first at (%d,%d) got %s want %s' % (xs[0], ys[0], img[ys[0], xs[0]],
                                                         want_img[ys[0], xs[0]])
        else:
            where = ''
        lines.append('%s %dx%dx%d: %s (%d pixels differ%s; %s from a full redraw)'
                     % (shot, w, h, bpp, 'right' if ok else 'WRONG', n, where,
                        'none' if rd == 0 else rd))
        if not ok:
            bad += 1
            Image.fromarray(want_img).save(os.path.join(run, 'want-%s.png' % shot))
print('\n'.join(lines))
print('%s %dx%d, %d KiB of video memory: %d pictures, %s' %
      (os.path.basename(run), SW, SH, mem // 1024, pics,
       'every one right' if bad == 0 else '%d WRONG' % bad))
sys.exit(1 if bad else 0)
