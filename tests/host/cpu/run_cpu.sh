#!/bin/bash
# i386.c's faults during delivery (v3.8): test_double_fault.c with the
# guest in guest_df.S, against i386.c built as the Korvo-1 builds it (its
# board header) and as the Linux route and tiny386's own Makefile build it
# (no board header); then against v3.7's i386.c, where the double fault
# must stop the emulator (the test sees the fault), and (v3.9) against
# v3.8's, where the front end's note comes after the line naming the cause.
#
#   bash tests/host/cpu/run_cpu.sh [OUT]
#
# Needs gcc and binutils with i386 support (as --32, ld -m elf_i386); without
# them it says SKIP.  Exit status 0 when every case passes.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
OUT=${1:-$HERE/build}
mkdir -p "$OUT" || exit 2
for t in as ld objcopy nm gcc; do
    command -v $t >/dev/null || { echo "SKIP cpu: no $t"; exit 0; }
done
as --32 -o "$OUT/guest_df.o" "$HERE/guest_df.S" 2> "$OUT/as.log" ||
    { cat "$OUT/as.log"; echo "SKIP cpu: no 32-bit assembler"; exit 0; }
ld -m elf_i386 -Ttext=0x10000 -o "$OUT/guest_df.elf" "$OUT/guest_df.o" || exit 1
objcopy -O binary "$OUT/guest_df.elf" "$OUT/guest_df.bin" || exit 1
nm "$OUT/guest_df.elf" > "$OUT/guest_df.sym" || exit 1

CFLAGS="-O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare"
pass=0
fail=0
result() {
    if [ "$1" = 0 ]; then pass=$((pass + 1)); echo "  ok: $2"; else fail=$((fail + 1)); echo "  FAILED: $2"; fi
}
build() {       # name i386.c [extra flags]: OUT/test_double_fault-name
    local name=$1 src=$2
    shift 2
    # i386.c's own warnings are not this test's business
    gcc -O2 -g -w -I"$TREE" "$@" -c -o "$OUT/i386-$name.o" "$src" || exit 1
    gcc -O2 -g -w -I"$TREE" "$@" -c -o "$OUT/fpu-$name.o" "$TREE/fpu.c" || exit 1
    gcc $CFLAGS -I"$TREE" "$@" -o "$OUT/test_double_fault-$name" \
        "$HERE/test_double_fault.c" "$OUT/i386-$name.o" "$OUT/fpu-$name.o" -lm || exit 1
}
BOARD="-include $TREE/esp/main/board_korvo1.h -I$TREE/tests/host/shim -DI386_ENABLE_FPU"

build korvo1 "$TREE/i386.c" $BOARD
"$OUT/test_double_fault-korvo1" "$OUT/guest_df.bin" "$OUT/guest_df.sym" > "$OUT/korvo1.log" 2>&1
st=$?
grep -v "^\(IP\|code\|stack\|stkf\|abort\|@\)" "$OUT/korvo1.log" | sed 's/^/    | /'
result $st "i386.c as the Korvo-1 builds it: double faults delivered, the shutdown clean"

# TRY1's way out, which no guest here reaches: read in i386.c, the note
# (cpu_abort_begin()) before its "@ file function line"
body=$(sed -n '/^static void noinline cpu_abort_at(/,/^}/p' "$TREE/i386.c")
nb=$(echo "$body" | grep -n "cpu_abort_begin();" | head -1 | cut -d: -f1)
nd=$(echo "$body" | grep -n 'dolog("@ ' | head -1 | cut -d: -f1)
grep -q '^#define TRY1(f) .*{ cpu_abort_at(cpu, __FILE__, __FUNCTION__, __LINE__); }' "$TREE/i386.c" &&
    [ -n "$nb" ] && [ -n "$nd" ] && [ "$nb" -lt "$nd" ]
result $? "TRY1's way out: the front end's note before its \"@ file function line\" (read in i386.c)"

build plain "$TREE/i386.c" -DI386_ALIGNED_ONLY
"$OUT/test_double_fault-plain" "$OUT/guest_df.bin" "$OUT/guest_df.sym" > "$OUT/plain.log" 2>&1
st=$?
tail -1 "$OUT/plain.log" | sed 's/^/    | /'
result $st "i386.c without the board header: the same"

if git -C "$TREE" rev-parse -q --verify v3.7 >/dev/null; then
    git -C "$TREE" show v3.7:i386.c > "$OUT/i386-v3.7.c"
    build v3.7 "$OUT/i386-v3.7.c" $BOARD
    "$OUT/test_double_fault-v3.7" "$OUT/guest_df.bin" "$OUT/guest_df.sym" before \
        > "$OUT/v3.7.log" 2>&1
    st=$?
    grep -v "^\(IP\|code\|stack\|stkf\)" "$OUT/v3.7.log" | sed 's/^/    | /'
    result $st "v3.7's i386.c: the double fault stops the emulator (the test sees the fault)"
fi

if git -C "$TREE" rev-parse -q --verify v3.8 >/dev/null; then
    # v3.9 calls the front end's note before the line that names the cause;
    # v3.8's i386.c, after it: the note's two checks must fail, and only they
    git -C "$TREE" show v3.8:i386.c > "$OUT/i386-v3.8.c"
    build v3.8 "$OUT/i386-v3.8.c" $BOARD
    "$OUT/test_double_fault-v3.8" "$OUT/guest_df.bin" "$OUT/guest_df.sym" > "$OUT/v3.8.log" 2>&1
    nf=$(grep -c "FAIL line" "$OUT/v3.8.log")
    nn=$(grep "FAIL line" "$OUT/v3.8.log" | grep -c "the front end's note once")
    grep "FAIL line" "$OUT/v3.8.log" | sed 's/^/    | /'
    [ "$nf" = 2 ] && [ "$nn" = 2 ]
    result $? "v3.8's i386.c: the note after the cause, and nothing else wrong (the test sees it)"
fi

echo "cpu: $pass passed, $fail failed"
[ $fail = 0 ]
