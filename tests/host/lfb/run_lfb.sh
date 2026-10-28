#!/bin/bash
# The linear frame buffer's own path in i386.c (v5.0), on the whole PC
# (test_lfb.c, guest_lfb.S):
#
#  1. test_lfb built against this tree with the Korvo-1's board header: every
#     case's own checks (video memory changed only where the case stores,
#     the marks exactly its blocks, vga_wrote);
#  2. the same test built against v4.6's tree, where every access went
#     through pc.c's iomem calls: its own checks, and its output the same as
#     1's line for line (each 256-byte block of video memory and each page of
#     RAM that changed, by a hash of its bytes; every mark; the flag; the
#     registers);
#  3. with the board's compiler and qemu-riscv32-static, when both are there:
#     both builds again for RV32, and the host instructions of each piece of
#     guest_lfb.S's bench_ cases counted (tbcount.c), old against new (v5.1:
#     pfill and pcopy too, the frame buffer through the page tables);
#  4. (v5.1) the accessed-bit probes (guest_lfb.S's probe_ cases): this tree
#     with the fast interpreter on and off, and v4.6's tree, must leave the
#     same words (the first operand's page table entry above all);
#  5. (v5.1) faults planted in copies of i386.c or i386_fast.h, each of which
#     the cases (against v4.6's output) or the probes must catch: the
#     two-operand rule taken out of REP MOVS and MOVS's destination (N2), and
#     out of CMPS; the last 16-bit store of a word fill dropped; the frame
#     buffer's physical address a page off where a REP piece finds its
#     write tag.
#
#   bash tests/host/lfb/run_lfb.sh [OUT]
#
# Needs gcc and binutils with i386 support (as --32, ld -m elf_i386) for 1
# and 2, and git with v4.6 (tag v4.6 or s31-v4.6, or commit 35a201c) for 2;
# says SKIP for what it cannot do.  3 looks for the compiler in RV32_CC
# (default: the Linux route's riscv32-buildroot-linux-musl-gcc in
# ~/tiny386-scratch/toolchain).  Exit status 0 when everything run passes.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
OUT=${1:-$HERE/build}
mkdir -p "$OUT" || exit 2
OUT=$(cd "$OUT" && pwd)
for t in as ld objcopy nm gcc; do
    command -v $t >/dev/null || { echo "SKIP lfb: no $t"; exit 0; }
done
as --32 -o "$OUT/guest_lfb.o" "$HERE/guest_lfb.S" 2> "$OUT/as.log" ||
    { cat "$OUT/as.log"; echo "SKIP lfb: no 32-bit assembler"; exit 0; }
ld -m elf_i386 -e 0x10000 -Ttext=0x10000 -o "$OUT/guest_lfb.elf" "$OUT/guest_lfb.o" || exit 1
objcopy -O binary "$OUT/guest_lfb.elf" "$OUT/guest_lfb.bin" || exit 1
nm "$OUT/guest_lfb.elf" > "$OUT/guest_lfb.sym" || exit 1

pass=0
fail=0
result() {
    if [ "$1" = 0 ]; then pass=$((pass + 1)); echo "  ok: $2"; else fail=$((fail + 1)); echo "  FAILED: $2"; fi
}

CORE="i386.c fpu.c i8259.c i8254.c ide.c vga.c vgafont.c i8042.c misc.c adlib.c ne2000.c
      i8257.c sb16.c pcspk.c pc.c pci.c"
# build NAME SRCTREE CC ARCHFLAGS: OUT/NAME/test_lfb, the core of SRCTREE as
# the board builds it (its header; -O3 for i386.c, vga.c and pc.c as the
# ESP-IDF build has them, -O2 for the rest), test_lfb.c and stubs.c from here
build() {
    local name=$1 src=$2 cc=$3 arch=$4 f o
    local d="$OUT/$name"
    rm -rf "$d"
    mkdir -p "$d"
    local inc="-include $src/esp/main/board_korvo1.h -I$HERE/shim -I$TREE/tests/host/shim -I$src"
    for f in $CORE; do
        o=-O2
        case $f in i386.c|vga.c|pc.c) o=-O3 ;; esac
        # the core's own warnings are not this test's business
        $cc $arch $o -g -w -fno-jump-tables -fno-tree-switch-conversion $inc \
            -DI386_ENABLE_FPU -c "$src/$f" -o "$d/${f%.c}.o" || return 1
    done
    $cc $arch -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare $inc \
        -c "$HERE/test_lfb.c" -o "$d/test_lfb.o" || return 1
    $cc $arch -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare $inc \
        -c "$HERE/stubs.c" -o "$d/stubs.o" || return 1
    $cc $arch ${STATIC:-} -o "$d/test_lfb" "$d"/*.o -Wl,--wrap=cpui386_new -lm -lpthread
}

# ==== 1. this tree ====
echo "== test_lfb on this tree (Korvo-1 header)"
build new "$TREE" gcc "" || exit 1
"$OUT/new/test_lfb" "$OUT/guest_lfb.bin" "$OUT/guest_lfb.sym" > "$OUT/new.log" 2> "$OUT/new.err"
st=$?
grep "FAIL\|^test_lfb" "$OUT/new.log" | head -20 | sed 's/^/    | /'
result $st "this tree: video memory changed only where each case stores, the marks exactly its blocks, vga_wrote"

# ==== 2. v4.6, and the two the same ====
OLD=
for t in v4.6 s31-v4.6 35a201c38d9252bec51e3258d60dfff95befaffc; do
    if git -C "$TREE" rev-parse -q --verify "$t^{commit}" >/dev/null 2>&1; then
        OLD=$t
        break
    fi
done
if [ -z "$OLD" ]; then
    echo "SKIP lfb: no v4.6 here (tag v4.6, s31-v4.6 or commit 35a201c): no comparison"
else
    echo "== the same against $OLD's tree"
    rm -rf "$OUT/v4.6-tree"
    mkdir -p "$OUT/v4.6-tree"
    git -C "$TREE" archive "$OLD" | tar -x -C "$OUT/v4.6-tree" || exit 1
    build old "$OUT/v4.6-tree" gcc "" || exit 1
    "$OUT/old/test_lfb" "$OUT/guest_lfb.bin" "$OUT/guest_lfb.sym" > "$OUT/old.log" 2> "$OUT/old.err"
    st=$?
    grep "FAIL\|^test_lfb" "$OUT/old.log" | head -20 | sed 's/^/    | /'
    result $st "v4.6's tree: the same checks (the cases' expectations hold for the old path)"
    if cmp -s "$OUT/old.log" "$OUT/new.log"; then st=0; else st=1; fi
    [ $st = 0 ] || diff "$OUT/old.log" "$OUT/new.log" | head -40 | sed 's/^/    | /'
    echo "    | $(grep -c '^== ' "$OUT/new.log") runs, $(wc -l < "$OUT/new.log") lines compared"
    result $st "every byte of video memory and RAM, every mark, vga_wrote and the registers as v4.6 leaves them"
fi

# ==== 3. host instructions on RV32 ====
RV32_CC=${RV32_CC:-$HOME/tiny386-scratch/toolchain/br-output/host/bin/riscv32-buildroot-linux-musl-gcc}
QEMU=$(command -v qemu-riscv32-static || command -v qemu-riscv32 || true)
if [ ! -x "$RV32_CC" ] || [ -z "$QEMU" ] || [ -z "$OLD" ]; then
    echo "SKIP lfb counting: needs RV32_CC ($RV32_CC), qemu-riscv32 and v4.6"
else
    echo "== host instructions, RV32 (the board's compiler, Zba, Zbb, Zbs), counted under qemu"
    gcc -O2 -o "$OUT/tbcount" "$HERE/tbcount.c" || exit 1
    NM=${RV32_CC%gcc}nm
    ARCH="-march=rv32imac_zicsr_zifencei_zba_zbb_zbs -mabi=ilp32"
    STATIC=-static build rv-new "$TREE" "$RV32_CC" "$ARCH" || exit 1
    STATIC=-static build rv-old "$OUT/v4.6-tree" "$RV32_CC" "$ARCH" || exit 1
    st=0
    for b in stores fill copy window pfill pcopy; do
        for v in old new; do
            bin="$OUT/rv-$v/test_lfb"
            mark=$($NM "$bin" | awk '$3 == "perf_mark" { print $1 }')
            "$QEMU" -d in_asm,exec,nochain -D /dev/stderr "$bin" "$OUT/guest_lfb.bin" \
                "$OUT/guest_lfb.sym" bench $b 2>&1 > "$OUT/bench-$b-$v.txt" |
                "$OUT/tbcount" "$mark" > "$OUT/count-$b-$v.txt" || st=1
            grep -q "at done" "$OUT/bench-$b-$v.txt" || st=1
        done
    done
    python3 - "$OUT" <<'PY' || st=1
import sys, re
out = sys.argv[1]
def stretches(b, v):
    r = []
    for line in open(f"{out}/count-{b}-{v}.txt"):
        m = re.match(r"stretch (\d+) total (\d+) loads (\d+) stores (\d+)$", line.strip())
        if m:
            r.append(int(m.group(2)))
    return r
# (bench, stretch, what, divisor, unit)
rows = [
    ("stores", 0, "MOV [EDI],EAX into the frame buffer (a loop of 4 instructions)", 4096, "a store"),
    ("stores", 1, "the same into RAM", 4096, "a store"),
    ("stores", 2, "MOV [EDI],AX into the frame buffer", 4096, "a store"),
    ("stores", 3, "MOV [EDI],AL into the frame buffer", 4096, "a store"),
    ("stores", 4, "ADD EBX,[ESI] from the frame buffer", 4096, "a load"),
    ("stores", 5, "the same from RAM", 4096, "a load"),
    ("fill", 0, "fill 400x300, a REP STOSD of 200 a row", 300 * 200, "a dword"),
    ("fill", 1, "fill 800x480, one REP STOSD", 800 * 480 // 2, "a dword"),
    ("copy", 0, "RAM to the screen, 400x300, REP MOVSD a row", 300 * 200, "a dword"),
    ("copy", 1, "scroll up a row (screen to screen, going up)", 300 * 200, "a dword"),
    ("copy", 2, "scroll down a row (screen to screen, going down)", 300 * 200, "a dword"),
    ("copy", 3, "the screen to RAM, 400x300", 300 * 200, "a dword"),
    ("window", 0, "MOV [EDI],EAX into the bank window", 4096, "a store"),
    ("window", 1, "REP STOSD into the bank window", 0x4000, "a dword"),
    ("window", 2, "REP MOVSD from the bank window into it", 0x2000, "a dword"),
    ("pfill", 0, "paging: fill 400x300, a REP STOSD of 200 a row", 300 * 200, "a dword"),
    ("pfill", 1, "paging: fill 800x480, one REP STOSD", 800 * 480 // 2, "a dword"),
    ("pcopy", 0, "paging: RAM to the screen, 400x300", 300 * 200, "a dword"),
    ("pcopy", 1, "paging: scroll up a row", 300 * 200, "a dword"),
    ("pcopy", 2, "paging: scroll down a row", 300 * 200, "a dword"),
    ("pcopy", 3, "paging: the screen to RAM, 400x300", 300 * 200, "a dword"),
]
print("    | %-58s %10s %10s %7s" % ("host instructions per guest " + "store/dword", "v4.6", "this tree", "times"))
for b, i, what, div, unit in rows:
    o, n = stretches(b, "old"), stretches(b, "new")
    if len(o) <= i or len(n) <= i:
        print("    | %s: no count" % what)
        sys.exit(1)
    print("    | %-58s %10.1f %10.1f %7.2f" % (what, o[i] / div, n[i] / div, o[i] / n[i]))
PY
    result $st "RV32 host instructions counted for the bench pieces, v4.6 and this tree (the table above)"
fi

# ==== 4. the accessed bits: the probes (v5.1) ====
PROBES="abit_rep abit_one abit_ctl abit_win abit_win1 abit_cmps abit_pop abit_push abit_call abit_wsrc abit_wsrc1"
if [ -z "$OLD" ]; then
    echo "SKIP lfb probes: no v4.6 here"
else
    echo "== the accessed-bit probes: the fast interpreter on, off, and v4.6"
    st=0
    for p in $PROBES; do
        a=$("$OUT/new/test_lfb" "$OUT/guest_lfb.bin" "$OUT/guest_lfb.sym" probe $p 1 2>> "$OUT/probe.err") || st=1
        b=$("$OUT/new/test_lfb" "$OUT/guest_lfb.bin" "$OUT/guest_lfb.sym" probe $p 0 2>> "$OUT/probe.err") || st=1
        c=$("$OUT/old/test_lfb" "$OUT/guest_lfb.bin" "$OUT/guest_lfb.sym" probe $p 2>> "$OUT/probe.err") || st=1
        echo "$c" > "$OUT/probe-old-$p.txt"
        if [ "$a" = "$b" ] && [ "$a" = "$c" ]; then
            echo "    | $a"
        else
            st=1
            echo "    | $p differs: fast on: ${a#*: }; off: ${b#*: }; v4.6: ${c#*: }"
        fi
    done
    result $st "the accessed-bit probes (the two-operand rule): the fast interpreter on and off and v4.6 leave the same"
fi

# ==== 5. planted faults (v5.1) ====
# mutant NAME FILE SED: FILE (i386.c or i386_fast.h) with the sed script in
# a copy of the tree's sources, i386.o built from it and linked with the
# rest of build 1; caught when the cases' output is not v4.6's or a probe's
# words are not v4.6's (with the fast interpreter on)
mutant() {
    local name=$1 file=$2 expr=$3 d="$OUT/mut-$1" p st=1
    rm -rf "$d"
    mkdir -p "$d/src/esp/main" "$d/obj"
    cp "$TREE"/*.c "$TREE"/*.h "$TREE"/*.def "$TREE"/*.inc "$d/src/" 2>/dev/null
    cp "$TREE/esp/main/board_korvo1.h" "$d/src/esp/main/"
    sed "$expr" "$TREE/$file" > "$d/src/$file"
    if cmp -s "$TREE/$file" "$d/src/$file"; then
        echo "    | ($name: the sed found nothing)"
        return 1
    fi
    gcc -O3 -g -w -fno-jump-tables -fno-tree-switch-conversion -include "$d/src/esp/main/board_korvo1.h" \
        -I"$HERE/shim" -I"$TREE/tests/host/shim" -I"$d/src" -DI386_ENABLE_FPU \
        -c "$d/src/i386.c" -o "$d/obj/i386.o" || return 1
    gcc -o "$d/test_lfb" "$d/obj/i386.o" $(ls "$OUT"/new/*.o | grep -v '/i386\.o$') \
        -Wl,--wrap=cpui386_new -lm -lpthread || return 1
    "$d/test_lfb" "$OUT/guest_lfb.bin" "$OUT/guest_lfb.sym" > "$d/cases.log" 2> "$d/cases.err"
    if ! cmp -s "$OUT/old.log" "$d/cases.log"; then
        st=0
        echo "    | ($name: the cases' output differs from v4.6's: $(diff "$OUT/old.log" "$d/cases.log" | grep -c '^>') lines)"
    fi
    for p in $PROBES; do
        if [ "$("$d/test_lfb" "$OUT/guest_lfb.bin" "$OUT/guest_lfb.sym" probe $p 1 2>> "$d/probe.err")" != "$(cat "$OUT/probe-old-$p.txt")" ]; then
            st=0
            echo "    | ($name: probe $p differs)"
        fi
    done
    return $st
}
if [ -z "$OLD" ]; then
    echo "SKIP lfb planted faults: no v4.6 here"
else
    echo "== planted faults"
    mutant n2 i386_fast.h 's/1, \&mpaddr, (H1)))) { \\$/1, \&mpaddr, (NULL)))) { \\/'
    result $? "the lfb test catches a planted fault (n2: MOVS's destination refilled over its source's entry)"
    mutant cmpsrule i386_fast.h 's/^\t\tFPTR2(d, SEG_ES, DI_, n, 0, s);$/\t\tFPTR(d, SEG_ES, DI_, n, 0);/'
    result $? "the lfb test catches a planted fault (cmpsrule: the two-operand rule taken out of CMPS)"
    mutant filltail i386.c 's/^\tif ((u8 \*) q < end)$/\tif (0)/'
    result $? "the lfb test catches a planted fault (filltail: a word fill's last 16-bit store dropped)"
    mutant mpaddr i386_fast.h 's/^\(.*\)(uintptr_t) cpu->cb.lfb_mem); \\$/\1(uintptr_t) cpu->cb.lfb_mem) + 0x1000; \\/'
    result $? "the lfb test catches a planted fault (mpaddr: the frame buffer's address a page off on a REP piece's write tag)"
fi

echo "lfb: $pass passed, $fail failed"
[ $fail = 0 ]
