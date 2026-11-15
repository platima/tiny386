# LOGO.SYS for tiny386 on the ESP-Mosaico: 320x400, 256 colours, shown by
# IO.SYS stretched to 640x400.  Palette 236-255 rotates (biClrImportant = 236,
# as in Windows' own): the bar at the bottom moves.  Lettering in tiny386's
# 8x16 VGA font (vgafont.c, fntcol16, public domain).
#   python3 s31/mklogo-mosaico.py vgafont.c LOGO.SYS
import re, struct, sys
font_src, out = sys.argv[1], sys.argv[2]
body = open(font_src).read()
body = body[body.index("{") + 1:body.index("};")]
font = bytes(int(t, 16) for t in re.findall(r"0x[0-9a-fA-F]+", body))
assert len(font) == 4096

W, H = 320, 400
pix = bytearray(W * H)

pal = [(0, 0, 0)] * 256
pal[1] = (255, 255, 255)        # white
pal[2] = (170, 190, 215)        # pale blue-grey
pal[3] = (90, 200, 255)         # cyan
pal[4] = (255, 150, 60)         # orange
pal[5] = (20, 40, 80)           # shadow
# 16-135: the background, deep blue at the top to near black at the bottom
for i in range(120):
    t = i / 119
    pal[16 + i] = (int(10 * (1 - t)), int(28 * (1 - t) + 4 * t), int(70 * (1 - t) + 14 * t))
# 236-255: one loop of light for the bar, dark to bright and back
for i in range(20):
    t = 1 - abs(i - 10) / 10
    pal[236 + i] = (int(30 + 220 * t), int(60 + 160 * t), int(120 + 135 * t))

for y in range(H):
    c = 16 + min(119, y * 120 // H)
    for x in range(W):
        pix[y * W + x] = c

def text(s, x0, y0, sx, sy, colour, shadow=None):
    if shadow is not None:
        text(s, x0 + max(1, sx // 2), y0 + max(1, sy // 2), sx, sy, shadow)
    for k, ch in enumerate(s):
        g = font[ord(ch) * 16:ord(ch) * 16 + 16]
        for r in range(16):
            bits = g[r]
            for c in range(8):
                if bits & (0x80 >> c):
                    for yy in range(sy):
                        for xx in range(sx):
                            x = x0 + (k * 8 + c) * sx + xx
                            y = y0 + r * sy + yy
                            if 0 <= x < W and 0 <= y < H:
                                pix[y * W + x] = colour

def centred(s, y0, sx, sy, colour, shadow=None):
    text(s, (W - len(s) * 8 * sx) // 2, y0, sx, sy, colour, shadow)

# the screen is shown twice as wide as it is drawn: sx 1 looks square at sy 2
centred("tiny386", 86, 3, 6, 1, 5)
centred("Windows 95", 196, 1, 3, 3)
centred("on the ESP-Mosaico", 250, 1, 2, 2)
centred("ESP32-S31  RISC-V  320 MHz", 290, 1, 2, 4)
# a thin rule under the title
for x in range(70, 250):
    for y in (186, 187):
        pix[y * W + x] = 3
# the bar: the bottom 11 rows, bands of the rotating entries
for y in range(H - 11, H):
    for x in range(W):
        pix[y * W + x] = 236 + (x // 8) % 20

# the BMP: bottom row first, BGR0 palette, biClrImportant 236
rows = b"".join(bytes(pix[y * W:(y + 1) * W]) for y in range(H - 1, -1, -1))
palb = b"".join(bytes((b, g, r, 0)) for r, g, b in pal)
off = 14 + 40 + 1024
hdr = b"BM" + struct.pack("<IHHI", off + len(rows), 0, 0, off)
info = struct.pack("<IiiHHIIiiII", 40, W, H, 1, 8, 0, 0, 0, 0, 0, 236)
open(out, "wb").write(hdr + info + palb + rows)
print(out, len(hdr + info + palb + rows), "bytes")
