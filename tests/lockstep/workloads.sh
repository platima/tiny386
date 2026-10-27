# tests/lockstep: the runs the tool knows by name.  Sourced by run.sh.
#
#   b1       Windows 95 from power-on to the desktop, then 120 s more
#   restart  to the desktop, then Start, Shut Down, Restart, and the desktop
#            again (20 minutes of guest time in all)
#   msdos    to the desktop, then Restart in MS-DOS mode, MEM, DIR, EXIT and
#            the desktop again
#   bench    FreeDOS from power-on, then BENCH.COM to its end (its last mark,
#            0AFh at 0000:04F0)
#   vbe      FreeDOS from power-on, then guest/vbetest.S (VBETEST.COM, built
#            and put on the image copy by run.sh): the VBE linear frame
#            buffer at 800x480x16 and 640x480x32, stored to every way the
#            CPU can, to its last mark (0BFh at 0000:04F0).  The Windows runs
#            never touch the linear frame buffer (the image has the standard
#            VGA driver), so a fast path for it is checked here.
#   t386mon  Windows 95 to the desktop, then T386MON (Plat's benchmark, its
#            T386MON.EXE given in T386MON_EXE and put in the root of the
#            image copy) started from Start, Run at 90 s after the taskbar
#            is first seen, its drawing tests at the image's 640x480 in 16
#            colours, to 320 s after the taskbar (v5.0)
#   t386mon800  Plat's own setup (v5.1): Windows 95 at 800x480 in 16 bits
#            through VMDisp9x's QEMU driver (QEMUMINI.DRV) with his one-byte
#            pointer change (s31/qemumini-pointer), on the VBE linear frame
#            buffer, with 13M and 768K of video memory as his board gives
#            with the card's win95-800x480.ini; the logon box's OK, then
#            T386MON started from Start, Run with /quit, F5 (its tests) to
#            "Tests done", where it closes and writes its log; to 480 s of
#            guest time
#
# The three Windows runs are the ones the v4.6 regression used (its
# regress46.sh): the same options, the PC repro's pristine image, Plat's
# board settings (mem 14M, vga_mem 320K, gen 3, no FPU).  Each run gets a
# copy of the image, made by run.sh and deleted after it.
#
# Environment: W95_IMG (default ~/win95-debug/pc-repro/img/win95.pristine.img)
# and BENCH_IMG (a FreeDOS disk with BENCH.COM started by FDAUTO.BAT; default
# ~/org2/jit/bench/run/bench-hd.img, made by that work's prep.sh).  Neither
# image, nor anything of Windows, may be published.  T386MON_EXE: for
# t386mon and t386mon800, T386MON.EXE (github.com/platima/T386Mon, built by
# its build.sh or from its release).
#
# t386mon800 (v5.1) needs N16_IMG: a Windows 95 image with VMDisp9x
# 1.2025.0.119's qemumini driver installed through Windows' own dialogs at
# 800x480 in 16 bits and Windows shut down from its Start menu (TEST-PLAN.md
# 13), its QEMUMINI.DRV as released (the copy gets the pointer change) or
# already changed; default ~/flicker/repro/h/img/base-n16.img, the image of
# v4.1's install run.  N16_ROMS: the folder with the bios.bin and
# vgabios.bin it runs with (default the image's own folder, where the
# card's are; the harness's vgabios.bin is another build).  The image, the
# driver and the ROMs are never in this tree: the scripts make the copy
# from them.

W95_IMG=${W95_IMG:-$HOME/win95-debug/pc-repro/img/win95.pristine.img}
BENCH_IMG=${BENCH_IMG:-$HOME/org2/jit/bench/run/bench-hd.img}
N16_IMG=${N16_IMG:-$HOME/flicker/repro/h/img/base-n16.img}

workload() {
    W_NAME=$1
    W_MEM=14M; W_VGA=320K; W_GEN=3; W_FPU=0
    W_STOP=""
    W_PREP=""
    W_POST=""
    W_ROMS=""
    case "$1" in
    b1)
        W_IMG=$W95_IMG
        W_OPTS=(-wall 3600 -ips 2000000 -idle 16 -insns 900 -vsecs 100000 -after-desktop 120
                -dialogs 8 -snap 300)
        ;;
    restart)
        W_IMG=$W95_IMG
        W_OPTS=(-wall 1500 -ips 2000000 -idle 16 -dkd 3:1d -dkd 3.1:01 -dku 3.2:01 -dku 3.3:1d
                -dk 5:16 -dk 7:13 -dk 8:1c -insns 8000 -vsecs 100000 -dialogs 400 -snap 10
                -after-desktop 1200)
        ;;
    msdos)
        W_IMG=$W95_IMG
        W_OPTS=(-wall 1500 -ips 2000000 -idle 16 -dkd 3:1d -dkd 3.1:01 -dku 3.2:01 -dku 3.3:1d
                -dk 5:16 -dk 7:32 -dk 8:1c -ttype 'WINDOWS>@3@mem|' -ttype 'WINDOWS>@9@dir|'
                -ttype 'WINDOWS>@16@exit|' -insns 8000 -vsecs 100000 -dialogs 400 -snap 10
                -after-desktop 1200)
        ;;
    bench)
        W_IMG=$BENCH_IMG
        W_MEM=8M; W_VGA=256K; W_GEN=4
        # the hook ends the run at BENCH's last mark, at 18.6 s of guest
        # time; without the hook, -vsecs does
        W_OPTS=(-wall 600 -ips 2000000 -idle 16 -insns 3000 -vsecs 40)
        W_STOP=0x4f0:0xaf
        ;;
    vbe)
        W_IMG=$BENCH_IMG
        W_VGA=2M
        # the last mark comes at 30.4 s of guest time; without the hook,
        # -vsecs ends the run (the program halts there)
        W_OPTS=(-wall 600 -ips 2000000 -idle 16 -insns 3000 -vsecs 60)
        W_STOP=0x4f0:0xbf
        W_PREP=prep_vbe
        ;;
    t386mon)
        W_IMG=$W95_IMG
        # Two keys up that do nothing (Shift, at 30 s and 300 s) keep the
        # harness's dialog poller paused from 29 s to 310 s: Internet
        # Explorer's setup wizard, which comes up at about 40 s, is left
        # waiting on its first page (pressed on, it brings a page every 9 s
        # and works in the background), and T386MON's window is never sent
        # an Enter.  Ctrl+Esc, R (Run), then T386MON with /top (above the
        # wizard), /bench (its tests 3 s after it starts) and /quit (closed
        # after them, its log written).
        W_OPTS=(-wall 3000 -ips 2000000 -idle 16 -insns 8000 -vsecs 100000 -dialogs 20 -snap 10
                -dku 30:2a -dkd 90:1d -dkd 90.1:01 -dku 90.2:01 -dku 90.3:1d -dk 92:13
                -dtype '94:C:\T386MON.EXE /top /bench /quit|' -dku 300:2a -after-desktop 320)
        W_PREP=prep_t386mon
        W_POST=post_t386mon
        ;;
    t386mon800)
        W_IMG=$N16_IMG
        W_ROMS=${N16_ROMS:-$(dirname "$N16_IMG")}
        W_MEM=13M; W_VGA=768K
        # The keys go at fixed guest times, not from the taskbar: the
        # harness looks for the taskbar and for dialogs only when the BIOS's
        # mode byte is not a text mode, and VMDisp9x sets its mode through
        # the VBE registers alone, so the byte stays 03h.  The times, from a
        # run with pictures every 5 s: the logon box is up from 62 s (Enter
        # at 80 s: its OK, the user's empty password), the taskbar from 88
        # s; Windows is then busy until 325 s, when the Microsoft Network's
        # icon comes up and the desktop is drawn again; Ctrl+Esc at 335 s,
        # R (Run), T386MON with /quit, F5 at 380 s (its window up from
        # about 345 s); "Tests done" and closed at about 445 s, the guest
        # idle after 450 s.  run.sh then reads the log out of the image
        # copy: its summary line in RUNDIR/t386mon.txt shows the tests ran
        # to their end.
        W_OPTS=(-wall 3000 -ips 2000000 -idle 16 -insns 8000 -vsecs 480 -snap 10
                -key 80:1c -kd 335:1d -kd 335.1:01 -ku 335.2:01 -ku 335.3:1d -key 337:13
                -type '339:C:\T386MON.EXE /quit|' -key 380:3f)
        W_PREP=prep_t386mon800
        W_POST=post_t386mon
        ;;
    *)
        echo "unknown workload $1 (b1, restart, msdos, bench, vbe, t386mon, t386mon800)" >&2
        return 1
        ;;
    esac
    [ -f "$W_IMG" ] || { echo "no image $W_IMG for $1" >&2; return 1; }
    if [ "$W_PREP" = prep_t386mon ] || [ "$W_PREP" = prep_t386mon800 ]; then
        [ -f "${T386MON_EXE:-}" ] || { echo "$1: set T386MON_EXE to a T386MON.EXE" >&2; return 1; }
    fi
    if [ -n "$W_ROMS" ]; then
        local r
        for r in bios.bin vgabios.bin; do
            [ -f "$W_ROMS/$r" ] || { echo "no $W_ROMS/$r for $1 (N16_ROMS)" >&2; return 1; }
        done
    fi
}

# vbe: VBETEST.COM built from guest/vbetest.S, and an FDAUTO.BAT that runs
# it, put on the copy of the FreeDOS image (its first partition) with mtools.
# The files' times are fixed (and mcopy keeps them), so that every copy is
# the same bytes: two runs from images that differ in a directory entry's
# time are two different runs.
prep_vbe() {  # IMAGE RUNDIR
    local img=$1 dir=$2 src
    src=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/guest/vbetest.S
    as --32 -o "$dir/vbetest.o" "$src" &&
        ld -m elf_i386 -Ttext 0x100 --oformat binary -o "$dir/VBETEST.COM" "$dir/vbetest.o" ||
        return 1
    local start
    start=$(python3 -I -c 'import struct, sys
m = open(sys.argv[1], "rb").read(512)
print(struct.unpack("<I", m[454:458])[0] * 512)' "$img") || return 1
    printf '@echo off\r\nC:\\VBETEST.COM\r\n' > "$dir/FDAUTO.BAT"
    touch -d '2026-01-01 00:00:00' "$dir/VBETEST.COM" "$dir/FDAUTO.BAT"
    local f
    for f in VBETEST.COM FDAUTO.BAT; do
        SOURCE_DATE_EPOCH=1767225600 MTOOLS_SKIP_CHECK=1 \
            mcopy -m -o -i "$img@@$start" "$dir/$f" "::/$f" || return 1
    done
}

# t386mon: T386MON.EXE put in the root of the copy of the Windows image (its
# first partition) with mtools, with a fixed time, as for vbe.
prep_t386mon() {  # IMAGE RUNDIR
    local img=$1 dir=$2 start
    [ -f "${T386MON_EXE:-}" ] || { echo "t386mon: set T386MON_EXE to a T386MON.EXE"; return 1; }
    start=$(python3 -I -c 'import struct, sys
m = open(sys.argv[1], "rb").read(512)
print(struct.unpack("<I", m[454:458])[0] * 512)' "$img") || return 1
    cp "$T386MON_EXE" "$dir/T386MON.EXE"
    touch -d '2026-01-01 00:00:00' "$dir/T386MON.EXE"
    SOURCE_DATE_EPOCH=1767225600 MTOOLS_SKIP_CHECK=1         mcopy -m -o -i "$img@@$start" "$dir/T386MON.EXE" "::/T386MON.EXE" || return 1
}

# t386mon800 (v5.1): Plat's pointer change made in the copy of the 800x480
# image (qemumini-pointer.py beside this file: the one byte, in place, after
# the checks of s31/qemumini-pointer), then T386MON.EXE put in its root as
# for t386mon.
prep_t386mon800() {  # IMAGE RUNDIR
    local here
    here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
    python3 -I "$here/qemumini-pointer.py" "$1" || return 1
    prep_t386mon "$1" "$2"
}

# after a t386mon run: C:\T386MON.LOG read out of the image copy (before
# run.sh deletes it) into RUNDIR/T386MON.LOG, and its tests' summary line
# into RUNDIR/t386mon.txt ("no summary" when the tests did not end).  Only
# reads the image.
post_t386mon() {  # IMAGE RUNDIR
    local img=$1 dir=$2 start
    start=$(python3 -I -c 'import struct, sys
m = open(sys.argv[1], "rb").read(512)
print(struct.unpack("<I", m[454:458])[0] * 512)' "$img") || return 1
    rm -f "$dir/T386MON.LOG"
    MTOOLS_SKIP_CHECK=1 mcopy -n -i "$img@@$start" ::/T386MON.LOG "$dir/T386MON.LOG" 2> /dev/null
    if grep -q '^# summary' "$dir/T386MON.LOG" 2> /dev/null; then
        grep '^# summary' "$dir/T386MON.LOG" | tr -d '\r' > "$dir/t386mon.txt"
    else
        echo "no summary" > "$dir/t386mon.txt"
    fi
}
