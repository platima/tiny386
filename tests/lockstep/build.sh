#!/bin/bash
# tests/lockstep: build the PC harness's headless driver for one tree, with
# the lockstep hook in it.
#
#   bash tests/lockstep/build.sh TREE OUTDIR
#
# TREE    any tiny386 tree the harness can build (this one, a worktree of a
#         branch, a clone of another commit); it is only read
# OUTDIR  where the program goes: OUTDIR/headless, OUTDIR/build.txt (the
#         harness's note of what it was built from) and OUTDIR/lockstep.txt
#
# The hook is one #include at the end of copies of five of the tree's files
# (i386.c, i8259.c, i8254.c, fpu.c, pc.c), made as a patch against the
# tree's own files, and the link wraps cpui386_step and pc_new.  The tree's
# files are never changed.  Without LS_OUT in its environment the program
# runs exactly as the harness's own build does.
#
# Environment:
#   HARNESS   the PC harness (default ~/org2/pcm/canary/harness, the copy
#             of ~/win95-debug/pc-repro/harness that builds v4.x trees)
#   FLAVOUR   the harness's flavour (esp: the core as the board builds it)
#   ARCH      the harness's arch (rv: -m32 -funsigned-char, as on RISC-V)
#   PATCHES, SED_I386, SED_VGA, SED_PC, SED_IDE, SED_I8042, SED_I8254,
#   SED_MISC, EXTRA_CFLAGS, BOARD, BOARD_DEFINE, BOARD_UNDEF
#             passed to the harness's build.sh as they are (a deliberate
#             bug for selftest.sh goes in through SED_I386)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
[ $# -eq 2 ] || { echo "usage: build.sh TREE OUTDIR"; exit 2; }
TREE=$(cd "$1" && pwd)
mkdir -p "$2"
OUT=$(cd "$2" && pwd)
HARNESS=${HARNESS:-$HOME/org2/pcm/canary/harness}
[ -f "$HARNESS/build.sh" ] || { echo "no PC harness at $HARNESS (set HARNESS)"; exit 2; }

P=$OUT/lockstep-hook.patch
: > "$P"
for pair in i386.c:lockstep_cpu.inc i8259.c:lockstep_pic.inc i8254.c:lockstep_pit.inc \
            fpu.c:lockstep_fpu.inc pc.c:lockstep.c; do
    f=${pair%%:*}
    inc=${pair##*:}
    [ -f "$TREE/$f" ] || { echo "$TREE has no $f"; exit 2; }
    { cat "$TREE/$f"; printf '\n/* tests/lockstep */\n#include "%s/%s"\n' "$HERE" "$inc"; } > "$OUT/.$f.new"
    diff -u --label "a/$f" --label "b/$f" "$TREE/$f" "$OUT/.$f.new" >> "$P" || true
    rm -f "$OUT/.$f.new"
done

export PATCHES="${PATCHES:-} $P"
export EXTRA_CFLAGS="${EXTRA_CFLAGS:-} -Wl,--wrap=cpui386_step -Wl,--wrap=pc_new"
bash "$HARNESS/build.sh" "$TREE" "${FLAVOUR:-esp}" "$OUT" "${ARCH:-rv}" > "$OUT/build.log" 2>&1 || {
    tail -40 "$OUT/build.log"; echo "build failed: $OUT/build.log"; exit 1; }
# the hook must not warn: a warning there is as likely a mistake in a field read
if grep -n "lockstep" "$OUT/compile.log" | grep -i "warning\|error" > "$OUT/hook-warnings.txt"; then
    echo "the hook compiled with warnings ($OUT/hook-warnings.txt):"; cat "$OUT/hook-warnings.txt"
fi
{
    echo "tree: $TREE"
    git -C "$TREE" rev-parse HEAD 2>/dev/null || echo "(not a git tree)"
    # read only: no index refresh in a repository that is not ours to change
    GIT_OPTIONAL_LOCKS=0 git -C "$TREE" status --porcelain --untracked-files=no --no-renames \
        2>/dev/null | sed 's/^/  changed: /' || true
    echo "tool: $HERE"
    git -C "$HERE" rev-parse HEAD 2>/dev/null || true
    echo "harness: $HARNESS flavour ${FLAVOUR:-esp} arch ${ARCH:-rv}"
    echo "SED_I386: ${SED_I386:-}"
    echo "SED_VGA: ${SED_VGA:-}  SED_PC: ${SED_PC:-}"
    echo "PATCHES: $PATCHES"
    echo "EXTRA_CFLAGS: $EXTRA_CFLAGS"
} > "$OUT/lockstep.txt"
echo "built $OUT/headless"
