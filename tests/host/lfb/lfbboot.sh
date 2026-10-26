#!/bin/bash
# LFBBOOT (lfbboot.S) on the PC harness (pc-repro's headless, not in this
# tree): a disk image for each piece of work, each booted by every headless
# binary named, five times, and the host seconds of each run taken; the
# boot alone (piece 0) is taken off the others.  The guest instructions are
# the same for every build, so the host time is what differs.  Each run's
# last picture must be the same for every build.
#
#   bash lfbboot.sh BIOSDIR OUT NAME=HEADLESS [NAME=HEADLESS...]
#
# BIOSDIR holds bios.bin and vgabios.bin (SeaBIOS and the VGA BIOS the
# harness uses).  Ends by itself: each run stops after 300 virtual seconds,
# the guest halted once its work is done.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BIOS=$(cd "$1" && pwd)
OUT=$2
shift 2
mkdir -p "$OUT" || exit 2
OUT=$(cd "$OUT" && pwd)
as --32 -o "$OUT/lfbboot.o" "$HERE/lfbboot.S" || exit 1
ld -m elf_i386 -e 0x7c00 -Ttext=0x7c00 -o "$OUT/lfbboot.elf" "$OUT/lfbboot.o" || exit 1
objcopy -O binary "$OUT/lfbboot.elf" "$OUT/lfbboot.bin" || exit 1
off=$(( 0x$(nm "$OUT/lfbboot.elf" | awk '$3 == "phase" { print $1 }') - 0x7c00 ))
for p in 0 1 2 3 4; do
    img="$OUT/lfbboot-$p.img"
    head -c $((1 << 20)) /dev/zero > "$img"
    dd if="$OUT/lfbboot.bin" of="$img" conv=notrunc status=none
    printf "\\x0$p" | dd of="$img" bs=1 seek=$off conv=notrunc status=none
done
names=()
for nb in "$@"; do
    name=${nb%%=*}
    bin=${nb#*=}
    names+=("$name")
    for p in 0 1 2 3 4; do
        for k in 1 2 3 4 5; do
            r="$OUT/run-$name-$p-$k"
            rm -rf "$r"
            mkdir -p "$r"
            printf '%s\n' '[pc]' "bios = $BIOS/bios.bin" "vga_bios = $BIOS/vgabios.bin" \
                'mem_size = 4M' 'vga_mem_size = 1M' "hda = $OUT/lfbboot-$p.img" 'fill_cmos = 1' '' \
                '[display]' 'width = 800' 'height = 480' '' '[cpu]' 'gen = 3' 'fpu = 0' > "$r/run.ini"
            t0=$(date +%s.%N)
            "$bin" -ips 2000000 -idle 200000 -vsecs 300 -snap 1000 -out "$r" "$r/run.ini" \
                > "$r/log.txt" 2>&1
            t1=$(date +%s.%N)
            echo "$t0 $t1" | awk '{ printf "%.3f\n", $2 - $1 }' > "$r/time.txt"
        done
    done
done
python3 - "$OUT" "${names[@]}" <<'PY'
import sys, os, re, glob, hashlib
out, names = sys.argv[1], sys.argv[2:]
what = ["the boot alone", "2,000 fills, REP STOSD", "2,000 scrolls, REP MOVSD",
        "200 passes of single stores", "200 passes of single loads"]
best, ok, pics = {}, True, {}
for n in names:
    for p in range(5):
        ts = []
        for k in (1, 2, 3, 4, 5):
            r = f"{out}/run-{n}-{p}-{k}"
            log = open(f"{r}/log.txt", errors="replace").read()
            # the guest's V and D, each at a line's start or after the other
            if not re.search(r"(^|\n)V(\[|\n|D)", log) or not re.search(r"(^|\n|V)D(\[|\n|$)", log):
                print(f"  {n} piece {p} run {k}: the guest did not say VD")
                ok = False
            ts.append(float(open(f"{r}/time.txt").read().split()[-1]))
            last = sorted(glob.glob(f"{r}/*.p[pn][mg]"))
            pics.setdefault(p, {})[f"{n}-{k}"] = (hashlib.sha256(open(last[-1], "rb").read()).hexdigest()
                                               if last else None)
        best[n, p] = min(ts)
print("  %-30s" % "host seconds (best of 5), less the boot" + "".join("%12s" % n for n in names))
for p in range(1, 5):
    print("  %-30s" % what[p] + "".join("%12.2f" % (best[n, p] - best[n, 0]) for n in names))
print("  %-30s" % what[0] + "".join("%12.2f" % best[n, 0] for n in names))
for p, d in pics.items():
    if None in d.values() or len(set(d.values())) != 1:
        print(f"  piece {p}: the last pictures differ or are missing: {d}")
        ok = False
print("  last pictures: the same in every run" if ok else "  NOT ALL RIGHT")
sys.exit(0 if ok else 1)
PY
