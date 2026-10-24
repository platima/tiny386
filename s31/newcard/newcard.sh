#!/bin/bash
# newcard.sh: lays out the image of a new card for the Korvo-1's dual boot,
# with Windows 95's volume on a partition of its own (v4.4, hda = sd:3):
#
#   p1  FAT32 KORVO_SD from sector 8192, P1_MIB MiB (default 512), with the
#       files of --p1 copied in (the tiny386 folder): Linux's /mnt/sd and
#       tiny386's /sdcard
#   p3  FAT16 (type 0E) straight after p1: the volume from inside the
#       Windows image (win95.img's partition 1), copied sector for sector,
#       with its boot sector's hidden-sectors field set to where p3 starts
#   p2  Linux's root straight after p3: copied sector for sector from
#       --root (an image of the old card's p2, or of a whole card, whose
#       first Linux partition is taken), grown to ROOT_MIB if asked
#
# The table lists them as p1, p2, p3, so Linux finds its root at
# /dev/mmcblk0p2 as before; p3 sits before p2 on the card so that the
# Windows volume is near the start, where nothing about the guest's disk is
# ever large.  The image ends where p2 ends; the rest of the card stays
# empty.  It is written to OUT, a file (never a device): put it on the card
# with your usual image writer (newcard/README.md).
#
#   bash newcard.sh --win95 WIN95.IMG --root ROOT.IMG --out NEW-CARD.IMG
#                   [--p1 DIR] [--p1-mib N] [--root-mib N] [--mbr OLD-MBR.BIN]
#
# --mbr     the old card's first sector (export-root.sh saves it): its disk
#           signature is kept, so anything that names a partition by
#           PARTUUID still finds it.  Without it the signature is new.
#
# Needs bash, python3, mkfs.fat and fsck.fat (dosfstools), mtools, dd and
# sha256sum; e2fsprogs (e2fsck, resize2fs) to check or grow p2.  Writes
# nothing but OUT and a work folder beside it (OUT.work, removed at the
# end).  Reads the inputs only.  Every check it makes is printed; the exit
# status is 0 only if all of them passed.
set -euo pipefail
export PATH=$PATH:/sbin:/usr/sbin
export MTOOLS_SKIP_CHECK=1

WIN=; ROOT=; OUT=; P1DIR=; P1_MIB=512; ROOT_MIB=; MBR=
while [ $# -gt 0 ]; do
    case "$1" in
    --win95) WIN=$2; shift 2 ;;
    --root) ROOT=$2; shift 2 ;;
    --out) OUT=$2; shift 2 ;;
    --p1) P1DIR=$2; shift 2 ;;
    --p1-mib) P1_MIB=$2; shift 2 ;;
    --root-mib) ROOT_MIB=$2; shift 2 ;;
    --mbr) MBR=$2; shift 2 ;;
    -h|--help) sed -n '2,39p' "$0"; exit 0 ;;
    *) echo "newcard.sh: $1 is not an option (--help lists them)"; exit 2 ;;
    esac
done
[ -n "$WIN" ] && [ -n "$ROOT" ] && [ -n "$OUT" ] ||
    { echo "newcard.sh: --win95, --root and --out are needed (--help)"; exit 2; }
for t in python3 mkfs.fat fsck.fat mcopy mdir mmd dd sha256sum truncate; do
    command -v $t >/dev/null || { echo "newcard.sh: no $t here"; exit 2; }
done
[ -f "$WIN" ] || { echo "newcard.sh: no file $WIN"; exit 2; }
[ -f "$ROOT" ] || { echo "newcard.sh: no file $ROOT"; exit 2; }
[ -z "$MBR" ] || [ -f "$MBR" ] || { echo "newcard.sh: no file $MBR"; exit 2; }
[ -z "$P1DIR" ] || [ -d "$P1DIR" ] || { echo "newcard.sh: no folder $P1DIR"; exit 2; }
case "$OUT" in
/dev/*) echo "newcard.sh: $OUT is a device; this writes an image file only"; exit 2 ;;
esac
[ ! -e "$OUT" ] || { echo "newcard.sh: $OUT is there already; give a new name"; exit 2; }
[ "$P1_MIB" -ge 64 ] 2>/dev/null || { echo "newcard.sh: --p1-mib must be 64 or more"; exit 2; }
WORK=$OUT.work
[ ! -e "$WORK" ] || { echo "newcard.sh: $WORK is there already"; exit 2; }
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

fails=0
check() {       # status what
    if [ "$1" = 0 ]; then echo "  ok: $2"; else echo "  FAILED: $2"; fails=$((fails + 1)); fi
}

# ==== what the inputs hold ====
echo "== the inputs"
# the Windows image: its MBR's active (or first) FAT partition, and the
# volume's own size from its boot sector
read -r WSTART WCOUNT WTYPE WFS <<<"$(python3 -I - "$WIN" <<'PY'
import struct, sys
f = open(sys.argv[1], 'rb')
m = f.read(512)
if m[510:512] != b'\x55\xaa':
    sys.exit('the Windows image has no partition table (no MBR)')
best = None
for i in range(4):
    e = m[446 + 16 * i:462 + 16 * i]
    t = e[4]
    s, n = struct.unpack('<II', e[8:16])
    if t in (0x04, 0x06, 0x0e, 0x0b, 0x0c) and n:
        if best is None or e[0] == 0x80:
            best = (s, n, t)
if not best:
    sys.exit('the Windows image has no FAT partition')
s, n, t = best
f.seek(s * 512)
v = f.read(512)
bps, = struct.unpack('<H', v[11:13])
tot, = struct.unpack('<H', v[19:21])
if not tot:
    tot, = struct.unpack('<I', v[32:36])
if v[510:512] != b'\x55\xaa' or bps != 512 or not tot or tot > n:
    sys.exit('the Windows image\'s partition holds no FAT volume this can copy')
print(s, n, '%02x' % t, tot)
PY
)" || { echo "newcard.sh: $WIN: see above"; exit 1; }
echo "  $WIN: partition from sector $WSTART, $WCOUNT sectors, type $WTYPE; its volume $WFS sectors ($((WFS / 2048)) MiB)"

# the root: a bare ext4 image, or a card image whose first Linux partition
# (type 83) is taken
read -r RSTART RCOUNT RKIND <<<"$(python3 -I - "$ROOT" <<'PY'
import os, struct, sys
f = open(sys.argv[1], 'rb')
size = os.path.getsize(sys.argv[1])
f.seek(1080)
if f.read(2) == b'\x53\xef':
    print(0, size // 512, 'ext')
    sys.exit(0)
f.seek(0)
m = f.read(512)
if m[510:512] == b'\x55\xaa':
    for i in range(4):
        e = m[446 + 16 * i:462 + 16 * i]
        s, n = struct.unpack('<II', e[8:16])
        if e[4] == 0x83 and n and (s + n) * 512 <= size:
            f.seek(s * 512 + 1080)
            if f.read(2) == b'\x53\xef':
                print(s, n, 'card%d' % (i + 1))
                sys.exit(0)
sys.exit('neither an ext4 image nor a card image with an ext4 partition')
PY
)" || { echo "newcard.sh: $ROOT: see above"; exit 1; }
if [ "$RKIND" = ext ]; then
    echo "  $ROOT: an ext4 image, $RCOUNT sectors ($((RCOUNT / 2048)) MiB)"
else
    echo "  $ROOT: a card image; its partition ${RKIND#card} is ext4, from sector $RSTART, $RCOUNT sectors"
fi

# ==== the layout ====
align() { echo $(( ($1 + 2047) / 2048 * 2048 )); }
P1=8192
P1N=$((P1_MIB * 2048))
P3=$(align $((P1 + P1N)))
P3N=$(align "$WFS")
P2=$(align $((P3 + P3N)))
RN=$RCOUNT
if [ -n "$ROOT_MIB" ]; then
    [ $((ROOT_MIB * 2048)) -ge "$RCOUNT" ] || { echo "newcard.sh: --root-mib $ROOT_MIB is smaller than the root ($((RCOUNT / 2048)) MiB)"; exit 2; }
    RN=$((ROOT_MIB * 2048))
fi
P2N=$(align "$RN")
TOT=$((P2 + P2N))
echo "== the new card's layout (sectors of 512 bytes)"
printf '  p1 FAT32 KORVO_SD  from %10d, %10d sectors (%d MiB)\n' $P1 $P1N $((P1N / 2048))
printf '  p3 Windows (%s)    from %10d, %10d sectors (%d MiB)\n' "$WTYPE" $P3 $P3N $((P3N / 2048))
printf '  p2 Linux root (83) from %10d, %10d sectors (%d MiB)\n' $P2 $P2N $((P2N / 2048))
printf '  the image: %d sectors, %d MiB; the card must be at least that\n' $TOT $(( (TOT + 2047) / 2048 ))
[ $((P3 + P3N)) -le $((1 << 28)) ] || { echo "newcard.sh: p3 would end past 128 GiB"; exit 2; }

truncate -s $((TOT * 512)) "$OUT"

# ==== the table ====
python3 -I - "$OUT" "${MBR:-}" $P1 $P1N $P2 $P2N $P3 $P3N "$WTYPE" <<'PY'
import os, struct, sys
out, old = sys.argv[1], sys.argv[2]
p1, p1n, p2, p2n, p3, p3n = map(int, sys.argv[3:9])
wtype = int(sys.argv[9], 16)
m = bytearray(512)
if old:
    o = open(old, 'rb').read(512)
    m[440:444] = o[440:444]
else:
    m[440:444] = os.urandom(4)
def chs(lba):
    c, h, s = lba // (255 * 63), lba // 63 % 255, lba % 63 + 1
    if c > 1023:
        c, h, s = 1023, 254, 63
    return bytes((h, s | (c >> 8) << 6, c & 255))
for i, (t, s, n) in enumerate(((0x0c, p1, p1n), (0x83, p2, p2n), (wtype, p3, p3n))):
    e = 446 + 16 * i
    m[e + 1:e + 4] = chs(s)
    m[e + 4] = t
    m[e + 5:e + 8] = chs(s + n - 1)
    m[e + 8:e + 16] = struct.pack('<II', s, n)
m[510:512] = b'\x55\xaa'
with open(out, 'r+b') as f:
    f.write(m)
print('  the table written: p1 0C, p2 83, p3 %02X; disk signature %s' % (wtype, m[440:444][::-1].hex()))
PY

# ==== p1 ====
echo "== p1: FAT32 KORVO_SD"
# below 260 MiB mkfs.fat's own cluster size leaves FAT32 too few clusters
# to be FAT32 (65525): one sector a cluster there, as the old card's p1
S1=
[ "$P1_MIB" -lt 260 ] && S1="-s 1"
mkfs.fat -F 32 $S1 -n KORVO_SD -h $P1 --offset=$P1 "$OUT" $((P1N / 2)) > "$WORK/mkfs.log" 2>&1 ||
    { cat "$WORK/mkfs.log"; exit 1; }
if [ -n "$P1DIR" ]; then
    n=$(find "$P1DIR" -mindepth 1 -maxdepth 1 | wc -l)
    if [ "$n" -gt 0 ]; then
        mcopy -s -p -m -i "$OUT@@$((P1 * 512))" "$P1DIR"/* :: ||
            { echo "newcard.sh: copying $P1DIR onto p1 failed"; exit 1; }
    fi
    echo "  $(find "$P1DIR" -type f | wc -l) files from $P1DIR copied"
else
    mmd -i "$OUT@@$((P1 * 512))" ::/tiny386
    echo "  empty but for a tiny386 folder (no --p1)"
fi

# ==== p3: the Windows volume ====
echo "== p3: the Windows volume, from $WIN"
dd if="$WIN" of="$OUT" bs=512 skip="$WSTART" seek=$P3 count="$WFS" conv=notrunc,sparse status=none
python3 -I - "$OUT" $P3 <<'PY'
import struct, sys
out, p3 = sys.argv[1], int(sys.argv[2])
with open(out, 'r+b') as f:
    f.seek(p3 * 512)
    v = bytearray(f.read(512))
    old, = struct.unpack('<I', v[28:32])
    v[28:32] = struct.pack('<I', p3)
    f.seek(p3 * 512)
    f.write(v)
    fat32 = v[82:87] == b'FAT32'
    msg = '  the boot sector\'s hidden-sectors field: %d (was %d)' % (p3, old)
    if fat32:
        bk, = struct.unpack('<H', v[50:52])
        if bk:
            f.seek((p3 + bk) * 512)
            b = bytearray(f.read(512))
            b[28:32] = struct.pack('<I', p3)
            f.seek((p3 + bk) * 512)
            f.write(b)
            msg += ', and in the backup boot sector (sector %d)' % bk
    print(msg)
PY

# ==== p2: Linux's root ====
echo "== p2: Linux's root, from $ROOT"
dd if="$ROOT" of="$OUT" bs=512 skip="$RSTART" seek=$P2 count="$RCOUNT" conv=notrunc,sparse status=none
if [ "$RN" != "$RCOUNT" ]; then
    command -v resize2fs >/dev/null && command -v e2fsck >/dev/null ||
        { echo "newcard.sh: growing p2 needs e2fsprogs (e2fsck, resize2fs)"; exit 1; }
    dd if="$OUT" of="$WORK/p2.img" bs=512 skip=$P2 count=$P2N conv=sparse status=none
    e2fsck -fy "$WORK/p2.img" > "$WORK/e2fsck-grow.log" 2>&1 || true
    resize2fs "$WORK/p2.img" > "$WORK/resize2fs.log" 2>&1 || { cat "$WORK/resize2fs.log"; exit 1; }
    dd if="$WORK/p2.img" of="$OUT" bs=512 seek=$P2 conv=notrunc,sparse status=none
    rm -f "$WORK/p2.img"
    echo "  grown from $((RCOUNT / 2048)) MiB to $((P2N / 2048)) MiB with resize2fs"
fi

# ==== the checks ====
echo "== checks"
set +e
python3 -I - "$OUT" $P1 $P1N $P2 $P2N $P3 $P3N "$WTYPE" <<'PY'
import struct, sys
out = sys.argv[1]
p1, p1n, p2, p2n, p3, p3n = map(int, sys.argv[2:8])
m = open(out, 'rb').read(512)
want = [(0x0c, p1, p1n), (0x83, p2, p2n), (int(sys.argv[8], 16), p3, p3n), (0, 0, 0)]
ok = m[510:512] == b'\x55\xaa'
for i, (t, s, n) in enumerate(want):
    e = m[446 + 16 * i:462 + 16 * i]
    ok = ok and e[4] == t and struct.unpack('<II', e[8:16]) == (s, n)
ex = sorted((s, s + n) for t, s, n in want if n)
ok = ok and all(a[1] <= b[0] for a, b in zip(ex, ex[1:])) and ex[0][0] > 0
print('  the table: %s' % ('p1, p2, p3 where they were put, none overlapping, p4 empty' if ok else 'NOT as it was written'))
sys.exit(0 if ok else 1)
PY
check $? "the table read back"
dd if="$OUT" of="$WORK/p1.img" bs=512 skip=$P1 count=$P1N conv=sparse status=none
fsck.fat -n "$WORK/p1.img" > "$WORK/fsck-p1.log" 2>&1
check $? "fsck.fat -n finds p1 clean ($(tail -1 "$WORK/fsck-p1.log"))"
rm -f "$WORK/p1.img"
dd if="$OUT" of="$WORK/p3.img" bs=512 skip=$P3 count=$P3N conv=sparse status=none
# fsck.fat -n on p3 and on the image's own volume: the same findings.
# (Windows 95 leaves its volume label blank, eleven spaces, which fsck.fat
# calls not valid and so exits 1 on a volume Windows has nothing against;
# that is on the image too, and newcard.sh changes nothing in it.)
dd if="$WIN" of="$WORK/src.img" bs=512 skip="$WSTART" count="$WFS" conv=sparse status=none
fsck.fat -n "$WORK/src.img" > "$WORK/fsck-src.log" 2>&1
s1=$?
fsck.fat -n "$WORK/p3.img" > "$WORK/fsck-p3.log" 2>&1
s2=$?
rm -f "$WORK/src.img"
[ $s1 = $s2 ] && diff -q <(sed 1d "$WORK/fsck-src.log" | sed 's#^[^ ]*src\.img:##') \
                       <(sed 1d "$WORK/fsck-p3.log" | sed 's#^[^ ]*p3\.img:##') >/dev/null
check $? "fsck.fat -n finds on p3 what it finds on the image's volume (exit $s2: $(grep -v '^fsck.fat\|^$\|Leaving' "$WORK/fsck-p3.log" | tr '\n' ' ' | sed 's/  */ /g'))"
# p3 against the image's volume, sector for sector, the hidden-sectors
# field (and FAT32's backup of it) left out
python3 -I - "$WIN" "$WSTART" "$WFS" "$WORK/p3.img" "$P3" <<'PY'
import hashlib, struct, sys
win, ws, n, p3img, p3 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4], int(sys.argv[5])
a, b = open(win, 'rb'), open(p3img, 'rb')
a.seek(ws * 512)
first = a.read(512)
fat32 = first[82:87] == b'FAT32'
bk = struct.unpack('<H', first[50:52])[0] if fat32 else 0
a.seek(ws * 512)
ha, hb = hashlib.sha256(), hashlib.sha256()
hid_ok = True
for i in range(0, n, 2048):
    k = min(2048, n - i)
    x, y = bytearray(a.read(k * 512)), bytearray(b.read(k * 512))
    for s in (0, bk) if bk else (0,):
        if i <= s < i + k:
            o = (s - i) * 512
            hid_ok &= struct.unpack('<I', y[o + 28:o + 32])[0] == p3
            x[o + 28:o + 32] = y[o + 28:o + 32] = b'\0\0\0\0'
    ha.update(x); hb.update(y)
same = ha.digest() == hb.digest()
print('  %s: p3 is the image\'s volume, every one of its %d sectors, but the hidden-sectors field (now %d%s)' % (
    'ok' if same and hid_ok else 'FAILED', n, p3, ', in the backup too' if bk else ''))
sys.exit(0 if same and hid_ok else 1)
PY
check $? "p3 against the volume in $WIN"
mdir -/ -a -i "$WIN@@$((WSTART * 512))" :: 2>/dev/null | grep -v "^ *Volume\|^ *Directory for\|bytes free\|^$" | sed 's/ \{2,\}/ /g' > "$WORK/dir-win.txt" || true
mdir -/ -a -i "$OUT@@$((P3 * 512))" :: 2>/dev/null | grep -v "^ *Volume\|^ *Directory for\|bytes free\|^$" | sed 's/ \{2,\}/ /g' > "$WORK/dir-p3.txt" || true
cmp -s "$WORK/dir-win.txt" "$WORK/dir-p3.txt" && [ -s "$WORK/dir-p3.txt" ]
check $? "every file and folder of the volume as mtools lists it, the same on p3 ($(grep -c . "$WORK/dir-p3.txt") lines)"
rm -f "$WORK/p3.img"
if [ "$RN" = "$RCOUNT" ]; then
    a=$(dd if="$ROOT" bs=512 skip="$RSTART" count="$RCOUNT" status=none | sha256sum | cut -c1-64)
    b=$(dd if="$OUT" bs=512 skip=$P2 count="$RCOUNT" status=none | sha256sum | cut -c1-64)
    [ "$a" = "$b" ]
    check $? "p2 is the root, every sector (SHA-256 ${b:0:16})"
fi
if command -v e2fsck >/dev/null; then
    dd if="$OUT" of="$WORK/p2.img" bs=512 skip=$P2 count=$P2N conv=sparse status=none
    e2fsck -fn "$WORK/p2.img" > "$WORK/e2fsck-p2.log" 2>&1
    check $? "e2fsck -fn finds p2 clean ($(tail -1 "$WORK/e2fsck-p2.log"))"
    rm -f "$WORK/p2.img"
else
    echo "  (no e2fsck here: p2 not checked as a file system)"
fi

echo "== $OUT: $( [ $fails = 0 ] && echo "ready" || echo "$fails check(s) FAILED: do not use it")"
echo "   SHA-256 of the image: $(sha256sum "$OUT" | cut -c1-64)"
[ $fails = 0 ]
