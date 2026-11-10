#!/bin/bash
# v5.3: ne2000.c's receive ring and the ESP32 builds' frame filter, on the
# PC, driven through the card's ports as a guest's driver drives them
# (common.h: Linux's 8390 set-up, frames read back with remote DMA).
#
# 1. ring.c, with ne2000.c built as the headless and SDL builds have it and
#    with BUILD_ESP32, each under AddressSanitizer and UBSan: frames of 14
#    to 1514 bytes read back whole when the guest keeps up and when it falls
#    behind; the ring filled to the page before BNRY; the ring registers at
#    and past the ends of the ring and of the card's memory (12 named cases,
#    then every mix of 11 values, 14641 cases); a register written while a
#    frame is copied in, as the guest on the other core can in the ESP32
#    builds.  Each case runs in a child process: an endless copy loop is
#    seen at once (stubs.c counts memcpy() calls), and a write past the
#    card's memory by a poisoned guard after its state.
# 2. route.c: where the ESP32 builds' filter sends 20 frames from the Wi-Fi
#    (the guest, the board's lwIP or both), with and without IP options, as
#    first and as later fragments.
# 3. hang.c: CURR at PSTOP on a plain -O2 build, no hooks, under timeout.
# 4. Upstream's ne2000.c at a080c5d, which v5.3 started from (when the tree
#    has that commit): a guest that keeps up must read the same bytes from
#    it as from this tree's, and the same tests have to see its endless
#    loops, its writes past the card's memory, the frames it loses when the
#    guest falls behind, the frames its filter sends the wrong way and the
#    hang.
#
#   bash tests/host/ne2000/run_ne2000.sh [OUT]
#
# Needs gcc with AddressSanitizer, setarch (the ASan runs are made under
# setarch -R) and timeout; without them it says SKIP.  Exit status 0 when
# every check passes.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
OUT=${1:-$HERE/build}
mkdir -p "$OUT" || exit 2
OUT=$(cd "$OUT" && pwd)
for t in gcc setarch timeout; do
    command -v $t > /dev/null || { echo "SKIP ne2000: no $t"; exit 0; }
done
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
HOOK="-Dmemcpy=ne_memcpy -Dmalloc=ne_malloc -U_FORTIFY_SOURCE"
export ASAN_OPTIONS=detect_leaks=0:exitcode=20
export UBSAN_OPTIONS=halt_on_error=1:exitcode=21:print_stacktrace=1
echo 'int main(void) { return 0; }' > "$OUT/san.c"
{ gcc $SAN -o "$OUT/san" "$OUT/san.c" && timeout 30 setarch -R "$OUT/san"; } > "$OUT/san.log" 2>&1 ||
    { cat "$OUT/san.log"; echo "SKIP ne2000: no AddressSanitizer here"; exit 0; }

pass=0
fail=0
result() {
    if [ "$1" = 0 ]; then pass=$((pass + 1)); echo "  ok: $2"; else fail=$((fail + 1)); echo "  FAILED: $2"; fi
}

# build NAME DIR: OUT/NAME's ring-host, ring-esp, route and hang, from DIR's
# ne2000.c and ne2000.h
build() {
    local o=$OUT/$1 d=$2 f D
    mkdir -p "$o"
    gcc -g -O1 $SAN -Wall -I"$HERE" -c -o "$o/stubs.o" "$HERE/stubs.c" || exit 1
    for f in host esp; do
        case $f in host) D= ;; esp) D="-DBUILD_ESP32 -I$HERE/shim" ;; esac
        # ne2000.c's own warnings are not this test's business
        gcc -g -O1 $SAN $HOOK $D -w -I"$d" -I"$TREE" -c -o "$o/ne2000-$f.o" "$d/ne2000.c" || exit 1
        gcc -g -O1 $SAN $D -Wall -Wno-unused-function -I"$d" -I"$HERE" -c -o "$o/ring-$f.o" \
            "$HERE/ring.c" || exit 1
        gcc $SAN -o "$o/ring-$f" "$o/ring-$f.o" "$o/ne2000-$f.o" "$o/stubs.o" || exit 1
    done
    gcc -g -O1 $SAN -Wall -Wno-unused-function -I"$d" -I"$HERE" -c -o "$o/route.o" "$HERE/route.c" || exit 1
    gcc $SAN -o "$o/route" "$o/route.o" "$o/ne2000-esp.o" "$o/stubs.o" || exit 1
    gcc -O2 -w -I"$d" -I"$TREE" -I"$HERE" -o "$o/hang" "$HERE/hang.c" "$d/ne2000.c" || exit 1
}
# run NAME SECONDS: the four programs side by side, each one's output in
# OUT/NAME/PROGRAM.txt and its exit status in OUT/NAME/PROGRAM.st; hang
# under timeout SECONDS
run() {
    local o=$OUT/$1 f
    for f in host esp; do
        rm -rf "$o/log-$f"
        mkdir -p "$o/log-$f"
        ( timeout 900 setarch -R "$o/ring-$f" "$o/log-$f" > "$o/ring-$f.txt" 2> "$o/ring-$f.err"
          echo $? > "$o/ring-$f.st" ) &
    done
    ( timeout 60 setarch -R "$o/route" > "$o/route.txt" 2> "$o/route.err"; echo $? > "$o/route.st" ) &
    ( timeout "$2" "$o/hang" > "$o/hang.txt" 2>&1; echo $? > "$o/hang.st" ) &
    wait
}
what() {
    case $1 in host) echo "as the headless and SDL builds have it" ;; esp) echo "with BUILD_ESP32" ;; esac
}

echo "== this tree's ne2000.c"
build tree "$TREE"
run tree 30
for f in host esp; do
    sed 's/^/    | /' "$OUT/tree/ring-$f.txt"
    [ "$(cat "$OUT/tree/ring-$f.st")" = 0 ] && tail -1 "$OUT/tree/ring-$f.txt" | grep -q ": all ok$"
    result $? "ring, $(what $f): frames whole whether the guest keeps up or falls behind, and for any ring registers, or one written mid-frame, no endless loop and nothing written past the card's memory"
done
sed 's/^/    | /' "$OUT/tree/route.txt"
[ "$(cat "$OUT/tree/route.st")" = 0 ] && [ "$(grep -c '^  .* ok$' "$OUT/tree/route.txt")" = 20 ]
result $? "the ESP32 filter: every frame goes where its rules say, IP options and later fragments too"
sed 's/^/    | /' "$OUT/tree/hang.txt"
[ "$(cat "$OUT/tree/hang.st")" = 0 ] && grep -q "returned" "$OUT/tree/hang.txt"
result $? "a plain -O2 build with CURR at PSTOP: ne2000_receive() returns"

if git -C "$TREE" rev-parse -q --verify "a080c5d^{commit}" > /dev/null 2>&1; then
    echo "== upstream's ne2000.c at a080c5d"
    mkdir -p "$OUT/a080c5d"
    git -C "$TREE" show a080c5d:ne2000.c > "$OUT/a080c5d/ne2000.c"
    git -C "$TREE" show a080c5d:ne2000.h > "$OUT/a080c5d/ne2000.h"
    build up "$OUT/a080c5d"
    run up 5
    st=0
    for f in host esp; do
        grep "^  keeps up" "$OUT/tree/ring-$f.txt" > "$OUT/keeps-tree-$f.txt"
        grep "^  keeps up" "$OUT/up/ring-$f.txt" > "$OUT/keeps-up-$f.txt"
        [ "$(grep -c ' ok; 4000 taken and read whole, 0 dropped' "$OUT/keeps-tree-$f.txt")" = 4 ] &&
            cmp -s "$OUT/keeps-tree-$f.txt" "$OUT/keeps-up-$f.txt" || st=1
        echo "    | $(what $f): hashes $(sed 's/.*hash //' "$OUT/keeps-up-$f.txt" | tr '\n' ' ')"
    done
    result $st "a guest that keeps up reads the same bytes as from a080c5d's ne2000.c, 4 ring layouts, both builds"
    for f in host esp; do
        L=$OUT/up/ring-$f.txt
        nloop=$(grep -c "endless copy loop" "$L")
        npast=$(grep -c "AddressSanitizer\|wrote past" "$L")
        nlost=$(grep "^  falls behind\|^  filled" "$L" | grep -c "frames wrong")
        echo "    | $(what $f): an endless loop in $nloop of the ring test's lines, a write past the card's memory in $npast, frames lost or wrong in $nlost"
        [ "$(cat "$OUT/up/ring-$f.st")" != 0 ] && [ "$nloop" -gt 0 ] && [ "$npast" -gt 0 ] && [ "$nlost" -gt 0 ]
        result $? "the ring test sees a080c5d's ne2000.c, $(what $f), loop for ever, write past the card's memory and lose frames when the guest falls behind"
    done
    nwrong=$(grep -c "WRONG" "$OUT/up/route.txt")
    grep "WRONG" "$OUT/up/route.txt" | sed 's/^ */    | /'
    [ "$(cat "$OUT/up/route.st")" != 0 ] && [ "$nwrong" -gt 0 ]
    result $? "the route test sees a080c5d's filter send $nwrong of 20 frames the wrong way"
    [ "$(cat "$OUT/up/hang.st")" = 124 ] && ! grep -q "returned" "$OUT/up/hang.txt"
    result $? "the hang test sees a080c5d's ne2000.c still in ne2000_receive() after 5 s"
else
    echo "(no a080c5d here: the comparison with upstream's ne2000.c is skipped)"
fi

echo "ne2000: $pass passed, $fail failed"
[ $fail = 0 ]
